#include "term_graphics.h"

#include "Graphics/Graphics.h"
#include "presenter.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <optional>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <unistd.h>
#endif

namespace {

std::unique_ptr<sr::Sink> sink;
std::optional<sr::Presenter> presenter;
std::optional<sr::TickClock> ticker;
sr::Clock::time_point deadline;
bool haveVsync = false;
sr::Scale scale = sr::Scale::Auto;
bool mute = false;
bool stats = false;
unsigned partIndex = 0;
bool firstFrame = false;
int width = SCREEN_WIDTH;
int height = SCREEN_HEIGHT;

#ifdef __EMSCRIPTEN__
EM_JS(void, configureWebAudio, (), {
    Module['configureAudio'](Module['SDL3']);
});
EM_JS(void, reportWebPart, (unsigned index), {
    Module['terminalPart'](index);
});
EM_JS(int, webHalfScale, (), {
    return Module['terminalHalfScale']();
});
#endif

void waitForTick()
{
#ifdef __EMSCRIPTEN__
    if (sink->quitting())
        throw sr::PlaybackStopped{};
#endif
    AudioPlayer::Update(false);
    // Some parts wait on a tracker cue before their first retrace. As in SR++,
    // rebase after a long wait instead of running through its missed ticks.
    if (!ticker || sr::Clock::now() - deadline > std::chrono::seconds(1))
        ticker.emplace();
    deadline = ticker->wait();
#ifdef __EMSCRIPTEN__
    // Some original effects wait for a cue without checking demo_wantstoquit().
    if (sink->quitting())
        throw sr::PlaybackStopped{};
    scale = webHalfScale() ? sr::Scale::Half : sr::Scale::Full;
#endif
}

void tracePart(const char* phase)
{
    if (!stats)
        return;
#ifndef __EMSCRIPTEN__
    if (isatty(STDERR_FILENO))
        return;
#endif
    std::fprintf(stderr, "%s part %02u\n", phase, partIndex);
}

} // namespace

namespace sr {

Graphics graphics;

void configureGraphics(Scale requestedScale, bool requestedMute, bool requestedStats)
{
    scale = requestedScale;
    mute = requestedMute;
    stats = requestedStats;
}

void startGraphicsPart(unsigned index)
{
    if (!presenter)
        presenter.emplace(*sink);
    presenter->refreshLayout(width, height, scale);
    if (stats)
        presenter->startPart(index);
    partIndex = index;
    firstFrame = true;
    tracePart("Starting");
    ticker.reset();
    haveVsync = false;
#ifdef __EMSCRIPTEN__
    reportWebPart(index);
#endif
}

} // namespace sr

bool Graphics::Init(WindowType)
{
    // ALKU waits for tracker orders even with muted output, so keep its clock running.
    if (mute)
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
        return false;
    }
#ifdef __EMSCRIPTEN__
    configureWebAudio();
#endif
    sink = sr::openSink();
    if (!sink) {
        SDL_Quit();
        return false;
    }
    return true;
}

void Graphics::Close()
{
    sink->close();
    if (stats && presenter)
        presenter->printPartStats();
    presenter.reset();
    sink.reset();
    SDL_Quit();
}

bool Graphics::WantsToQuit()
{
    if (sink->quitting())
        return true;
    if (presenter)
        presenter->refreshLayout(width, height, scale);
    return false;
}

void Graphics::ChangeMode(int x, int y, float)
{
    width = x;
    height = y;
    Common::reset();
    if (presenter)
        presenter->refreshLayout(width, height, scale);
}

void Graphics::Update()
{
    // Some sections call demo_blit directly while waiting for music cues.
    if (!haveVsync)
        waitForTick();
    haveVsync = false;
    if (WantsToQuit())
        return;

    constexpr size_t vramSize = Shim::VRAM_X * Shim::VRAM_Y;
    const size_t count = static_cast<size_t>(width) * height;
    if (count > vramSize || Shim::startpixel > vramSize - count)
        return;

    sr::Palette palette;
    for (size_t i = 0; i < palette.size(); ++i) {
        const unsigned packed = Shim::palette[i];
        palette[i] = {static_cast<uint8_t>((packed >> 18) & 63),
                      static_cast<uint8_t>((packed >> 10) & 63),
                      static_cast<uint8_t>((packed >> 2) & 63)};
    }
    if (presenter->present({{Shim::vram + Shim::startpixel, count}, palette, width, height, scale},
                           deadline) && firstFrame) {
        firstFrame = false;
        tracePart("Drawing");
    }
}

void demo_changemode(int x, int y, float jsss)
{
    sr::graphics.ChangeMode(x, y, jsss);
}

void demo_blit()
{
    sr::graphics.Update();
}

bool demo_wantstoquit()
{
    return sr::graphics.WantsToQuit();
}

int demo_vsync(bool)
{
    waitForTick();
    haveVsync = true;
    // Some parts advance one simulation step regardless of the return value.
    // Run each tick, catching up without presenting expired frames.
    return 1;
}
