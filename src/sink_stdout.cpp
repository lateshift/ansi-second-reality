#include "presenter.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace sr {
namespace {

constexpr std::string_view enter = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[0m\x1b[2J\x1b[H";
constexpr std::string_view leave = "\x18\x1b[?2026l\x1b]104\x1b\\\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l";
volatile std::sig_atomic_t interrupted = 0;
volatile std::sig_atomic_t resized = 0;
constexpr int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGWINCH};

void handleSignal(int signal)
{
    if (signal == SIGWINCH)
        resized = 1;
    else
        interrupted = 1;
}

bool writeAll(const void* buffer, size_t length, bool restoring = false)
{
    auto data = static_cast<const uint8_t*>(buffer);
    while (length > 0) {
        const ssize_t written = ::write(STDOUT_FILENO, data, length);
        if (written > 0) {
            data += written;
            length -= static_cast<size_t>(written);
        } else if (written == 0 || errno != EINTR || (interrupted && !restoring)) {
            return false;
        }
    }
    return true;
}

class StdoutSink final : public Sink {
public:
    ~StdoutSink() override { close(); }

    bool start()
    {
        if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
            std::fputs("ansi-second-reality needs a terminal on stdin and stdout.\n", stderr);
            return false;
        }
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            std::perror("Reading terminal mode");
            return false;
        }

        interrupted = 0;
        resized = 1;
        struct sigaction action = {};
        action.sa_handler = handleSignal;
        sigemptyset(&action.sa_mask);
        for (int signal : signals) {
            action.sa_flags = signal == SIGWINCH ? SA_RESTART : 0;
            if (sigaction(signal, &action, &oldActions_[actionCount_]) != 0) {
                std::perror("Installing terminal shutdown handler");
                return false;
            }
            ++actionCount_;
        }

        termios raw = original_;
        cfmakeraw(&raw);
        raw.c_lflag |= ISIG;
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
            std::perror("Setting terminal mode");
            return false;
        }
        raw_ = true;
        active_ = true;
        viewport();
        return writeAll(enter.data(), enter.size());
    }

    bool quitting() override
    {
        uint8_t input[64];
        const ssize_t count = ::read(STDIN_FILENO, input, sizeof(input));
        for (ssize_t i = 0; i < count; ++i) {
            if (input[i] == 0x1b || input[i] == 'q' || input[i] == 'Q')
                stopped_ = true;
        }
        return stopped_ || interrupted;
    }

    bool ready() override { return !stopped_ && !interrupted; }

    std::optional<Viewport> viewport() override
    {
        bool updated = false;
        if (resized) {
            resized = 0;
            winsize size = {};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
                viewport_ = {size.ws_col, size.ws_row};
                updated = true;
            } else if (errno == EINTR) {
                resized = 1;
            }
        }
        return Viewport{viewport_, updated};
    }

    bool writeFrame(std::span<const uint8_t> bytes, double) override
    {
        if (writeAll(bytes.data(), bytes.size()))
            return true;
        writeError_ = interrupted ? 0 : errno;
        stopped_ = true;
        return false;
    }

    void close() override
    {
        if (active_) {
            writeAll(leave.data(), leave.size(), true);
            active_ = false;
        }
        if (raw_) {
            while (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_) != 0 && errno == EINTR) {}
            raw_ = false;
        }
        for (size_t i = 0; i < actionCount_; ++i)
            sigaction(signals[i], &oldActions_[i], nullptr);
        actionCount_ = 0;

        if (writeError_) {
            std::fprintf(stderr, "Terminal write failed: %s\n", std::strerror(writeError_));
            writeError_ = 0;
        }
    }

private:
    Grid viewport_ = {};
    termios original_ = {};
    struct sigaction oldActions_[std::size(signals)] = {};
    size_t actionCount_ = 0;
    bool raw_ = false, active_ = false, stopped_ = false;
    int writeError_ = 0;
};

} // namespace

std::unique_ptr<Sink> openSink()
{
    auto sink = std::make_unique<StdoutSink>();
    if (!sink->start())
        return {};
    return sink;
}

} // namespace sr
