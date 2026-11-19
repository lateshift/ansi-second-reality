#include "presenter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <thread>
#endif

namespace sr {
namespace {

double ms(Clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

struct Distribution {
    double mean = 0, p95 = 0;
};

Distribution distribution(std::vector<double>& values)
{
    if (values.empty())
        return {};
    double sum = 0;
    for (double value : values)
        sum += value;
    const auto position = values.begin() + static_cast<ptrdiff_t>(std::ceil(values.size() * .95)) - 1;
    std::nth_element(values.begin(), position, values.end());
    return {sum / values.size(), *position};
}

} // namespace

Clock::time_point TickClock::wait()
{
    const auto target = next_;
    next_ += tickPeriod;
#ifdef __EMSCRIPTEN__
    // A timer yield lets Puzzle drain output and keeps browser input responsive.
    // Also yield during catch-up after the tab has been suspended.
    const double remaining = ms(target - Clock::now());
    if (remaining > 0)
        emscripten_sleep(static_cast<unsigned>(std::ceil(remaining)));
    else
        emscripten_sleep(0);
#else
    std::this_thread::sleep_until(target);
#endif
    return next_;
}

Presenter::Presenter(Sink& sink) : sink_(sink)
{
    bytes_.reserve(1'500'000);
    samples_.reserve(70 * 60);
}

void Presenter::skip(SkipReason reason)
{
    ++skipped_;
    sink_.skipped(reason);
}

bool Presenter::ready(Clock::time_point deadline)
{
    if (Clock::now() >= deadline) {
        skip(SkipReason::Deadline);
        return false;
    }
    if (!sink_.ready()) {
        skip(SkipReason::Backpressure);
        return false;
    }
    return true;
}

bool Presenter::refreshLayout(int width, int height, Scale requested)
{
    const auto view = sink_.viewport();
    const Grid mode{width, height};
    const bool resized = view && view->resized;
    if (mode == mode_ && !resized && requested == requested_)
        return selected_.has_value();

    mode_ = mode;
    requested_ = requested;
    if (!view) {
        selected_ = requested == Scale::Auto ? Scale::Full : requested;
        return true;
    }
    viewport_ = view->size;
    // A resize can reflow the screen even if the reported cell count is unchanged.
    // The warning can also erase it before the previous size returns.
    state_.grid = {};
    const Grid full = terminalGrid(width, height, Scale::Full);
    const Grid half = terminalGrid(width, height, Scale::Half);
    selected_ = fitScale(viewport_, full, half, requested);
    if (!selected_) {
        encodeFitMessage(viewport_, full, half, bytes_);
        sink_.writeFrame(bytes_, 0);
    }
    return selected_.has_value();
}

bool Presenter::present(Frame frame, Clock::time_point deadline)
{
    if (!refreshLayout(frame.width, frame.height, frame.scale) || !ready(deadline))
        return false;
    frame.scale = *selected_;
    frame.viewport = viewport_;
    sink_.prepare(terminalGrid(frame.width, frame.height, frame.scale));
    const auto begin = Clock::now();
    encode(frame, state_, bytes_);
    const auto encoded = Clock::now();
    const auto encodeMs = ms(encoded - begin);
    if (!sink_.writeFrame(bytes_, encodeMs))
        return false;
    const auto written = Clock::now();
    samples_.push_back({written, bytes_.size(), encodeMs, ms(written - encoded)});
    return true;
}

void Presenter::printStats(std::string_view label) const
{
    printStats(label, samples_, skipped_, Clock::now() - started_);
}

void Presenter::startPart(unsigned index)
{
    parts_.push_back({index, samples_.size(), skipped_, Clock::now()});
}

void Presenter::printPartStats() const
{
    const auto all = std::span<const Sample>(samples_);
    const auto now = Clock::now();
    const Part end{0, all.size(), skipped_, now};
    for (size_t i = 0; i < parts_.size(); ++i) {
        const Part& part = parts_[i];
        const Part& next = i + 1 < parts_.size() ? parts_[i + 1] : end;
        char label[16];
        std::snprintf(label, sizeof(label), "Part %02u", part.index);
        printStats(label, all.subspan(part.sample, next.sample - part.sample),
                   next.skipped - part.skipped, next.started - part.started);
    }
    printStats("Demo");
}

void Presenter::printStats(std::string_view label, std::span<const Sample> samples,
                           size_t skipped, Clock::duration duration) const
{
    std::vector<double> bytes, encodeMs, writeMs;
    size_t total = 0, peak = 0, inWindow = 0, first = 0;
    const double seconds = std::max(1e-9, std::chrono::duration<double>(duration).count());
    for (const auto& sample : samples) {
        bytes.push_back(static_cast<double>(sample.bytes));
        encodeMs.push_back(sample.encodeMs);
        writeMs.push_back(sample.writeMs);
        total += sample.bytes;
        inWindow += sample.bytes;
        while (sample.time - samples[first].time >= std::chrono::seconds(1))
            inWindow -= samples[first++].bytes;
        peak = std::max(peak, inWindow);
    }
    const auto size = distribution(bytes);
    const auto encode = distribution(encodeMs);
    const auto write = distribution(writeMs);
    std::fprintf(stderr,
                 "%.*s | %.3f s | %zu presented | %zu skipped | %.2f fps | %.0f B/s | "
                 "%zu peak B/s | %.1f avg / %.0f p95 B/frame | "
                 "%.3f avg / %.3f p95 encode ms | %.3f avg / %.3f p95 write ms\n",
                 static_cast<int>(label.size()), label.data(), seconds, samples.size(), skipped,
                 samples.size() / seconds, total / seconds, peak, size.mean, size.p95,
                 encode.mean, encode.p95, write.mean, write.p95);
}

} // namespace sr
