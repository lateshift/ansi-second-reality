#include "term_graphics.h"

#include "Graphics/Graphics.h"

#include <charconv>
#include <cstdio>
#include <iterator>
#include <string_view>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define PART(name) namespace name { void main(); }
PART(Alku)
PART(U2A)
PART(OUTTA)
PART(Beg)
PART(Glenz)
PART(Tunneli)
PART(KOE)
PART(Shutdown)
PART(Forest)
PART(Lens)
PART(PLZ)
PART(Dots)
PART(Water)
PART(Coman)
PART(JPLogo)
PART(U2E)
PART(End)
PART(Credits)
PART(EndScrl)
#undef PART

namespace {

constexpr struct PartInfo {
    Music::Song song;
    int order;
    int width, height;
    void (*run)();
} parts[] = {
#include "parts.inc"
};

#ifdef __EMSCRIPTEN__
EM_JS(void, detachWebAudioCallback, (), {
    // A queued Web Audio event can still reach a disconnected processor.
    const node = Module['SDL3']?.audio_playback?.scriptProcessorNode;
    if (node) node.onaudioprocess = null;
});
#endif

void waitForForest()
{
    // Preserve the two tracker transitions after Panicfake.
    while (!demo_wantstoquit() && Music::getPlusFlags() >= 0) {
        demo_vsync();
        demo_blit();
    }
    while (!demo_wantstoquit() && Music::getPlusFlags() < 0) {
        demo_vsync();
        demo_blit();
    }
}

void playParts(unsigned firstPart)
{
    Music::Song song = Music::Song::COUNT;
    for (unsigned index = firstPart; index < std::size(parts) && !demo_wantstoquit(); ++index) {
        const auto& part = parts[index];
        if (song != part.song) {
#ifdef __EMSCRIPTEN__
            detachWebAudioCallback();
#endif
            song = part.song;
            Music::start(song, part.order);
        }
        demo_changemode(part.width, part.height);
        sr::startGraphicsPart(index);
        part.run();
        if (demo_wantstoquit())
            break;
        Shim::finishedDemoFirstPart();
        if (index == 7)
            waitForForest();
    }
}

int usage(int status)
{
    std::fprintf(status ? stderr : stdout,
                 "Usage: ansi-second-reality [--scale full|half] [--part 0..18] [--no-audio] [--stats]\n");
    return status;
}

} // namespace

int main(int argc, char** argv)
{
    sr::Scale scale = sr::Scale::Auto;
    bool mute = false;
    bool stats = false;
    unsigned firstPart = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help")
            return usage(0);
        if (arg == "--scale" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            if (value != "full" && value != "half")
                return usage(2);
            scale = value == "full" ? sr::Scale::Full : sr::Scale::Half;
        } else if (arg == "--part" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            const auto end = value.data() + value.size();
            const auto result = std::from_chars(value.data(), end, firstPart);
            if (result.ec != std::errc{} || result.ptr != end || firstPart >= std::size(parts))
                return usage(2);
        } else if (arg == "--no-audio") {
            mute = true;
        } else if (arg == "--stats") {
            stats = true;
        } else {
            return usage(2);
        }
    }

    sr::configureGraphics(scale, mute, stats);
    if (!sr::graphics.Init(Graphics::WindowType::Windowed))
        return 1;
#ifdef __EMSCRIPTEN__
    try {
        playParts(firstPart);
    } catch (const sr::PlaybackStopped&) {
        // A stopped mount follows the same SDL shutdown path as a finished demo.
    }
    detachWebAudioCallback();
#else
    playParts(firstPart);
#endif
    Music::end();
    sr::graphics.Close();
    return 0;
}
