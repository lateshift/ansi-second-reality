#include "presenter.h"

#include <emscripten.h>

namespace sr {
namespace {

EM_JS(int, webQuitting, (), { return Module['terminalQuitting'](); });
EM_JS(int, webReady, (), { return Module['terminalReady'](); });
EM_JS(void, webResize, (int cols, int rows), { Module['terminalResize'](cols, rows); });
EM_JS(int, webWrite, (const uint8_t* bytes, size_t length, double encodeMs), {
    return Module['terminalWrite'](HEAPU8.subarray(bytes, bytes + length), encodeMs);
});
EM_JS(void, webSkipped, (int busy), { Module['terminalSkipped'](busy); });

class WebSink final : public Sink {
public:
    bool quitting() override { return webQuitting(); }
    bool ready() override { return webReady(); }

    void prepare(Grid grid) override
    {
        if (grid != grid_) {
            webResize(grid.cols, grid.rows);
            grid_ = grid;
        }
    }

    bool writeFrame(std::span<const uint8_t> bytes, double encodeMs) override
    {
        return webWrite(bytes.data(), bytes.size(), encodeMs);
    }

    void skipped(SkipReason reason) override { webSkipped(reason == SkipReason::Backpressure); }

    void close() override {}

private:
    Grid grid_ = {};
};

} // namespace

std::unique_ptr<Sink> openSink()
{
    return std::make_unique<WebSink>();
}

} // namespace sr
