#include "presenter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

int main(int argc, char** argv)
{
    sr::Scale scale = sr::Scale::Full;
    bool paletteOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--scale" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            if (value == "full" || value == "half") {
                scale = value == "full" ? sr::Scale::Full : sr::Scale::Half;
                continue;
            }
        } else if (arg == "--palette") {
            paletteOnly = true;
            continue;
        }
        std::fputs("Usage: stress [--scale full|half] [--palette]\n", stderr);
        return 2;
    }

    auto sink = sr::openSink();
    if (!sink)
        return 1;
    constexpr int width = 320, height = 200;
    sr::Palette palette;
    for (unsigned i = 0; i < palette.size(); ++i)
        palette[i] = {static_cast<uint8_t>(i & 63), static_cast<uint8_t>((i * 5) & 63),
                      static_cast<uint8_t>(((i >> 6) * 16 + i * 13) & 63)};

    std::vector<uint8_t> pixels(width * height);
    uint32_t random = 0xcafe1234;
    auto fill = [&](unsigned parity) {
        for (uint8_t& pixel : pixels) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            // A new parity guarantees every cell changes between submitted frames,
            // even when intermediate ticks are skipped.
            pixel = static_cast<uint8_t>((random & 254) | parity);
        }
    };
    if (paletteOnly)
        fill(0);

    unsigned parity = 0;
    sr::Presenter presenter(*sink);
    sr::TickClock clock;
    for (int tick = 0; tick < 700 && !sink->quitting(); ++tick) {
        const auto deadline = clock.wait();
        if (sink->quitting())
            break;
        if (presenter.refreshLayout(width, height, scale) && presenter.ready(deadline)) {
            if (!paletteOnly)
                fill(parity);
            if (presenter.present({pixels, palette, width, height, scale}, deadline))
                parity ^= 1;
        }
        if (paletteOnly)
            std::rotate(palette.begin(), palette.begin() + 1, palette.end());
    }
    sink->close();
#ifndef __EMSCRIPTEN__
    presenter.printStats(paletteOnly ? "Palette rotation" : "Random pixels");
#endif
}
