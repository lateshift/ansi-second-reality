#include "encoder.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view begin = "\x1b[?2026h";
constexpr std::string_view end = "\x1b[?2026l";
constexpr std::string_view clear = "\x1b[0m\x1b[2J\x1b[H";
constexpr std::string_view block = "\xe2\x96\x80";

void check(bool condition, std::string_view label)
{
    if (!condition) {
        std::cerr << "Failed: " << label << '\n';
        std::exit(1);
    }
}

void golden(const std::vector<uint8_t>& actual, const std::string& expected,
            std::string_view label)
{
    check(std::string(actual.begin(), actual.end()) == expected, label);
}

void smallFrames()
{
    sr::Palette palette = {};
    palette[1] = {63, 32, 1};
    std::array<uint8_t, 8> pixels = {1, 1, 1, 1, 0, 0, 0, 1};
    sr::Frame frame{pixels, palette, 4, 2};
    sr::State state;
    std::vector<uint8_t> out;

    std::string expected(begin);
    for (int i = 0; i < 256; ++i) {
        if (i % 7 == 0) {
            if (i != 0)
                expected += "\x1b\\";
            expected += "\x1b]4";
        }
        expected += ';' + std::to_string(i);
        expected += i == 1 ? ";rgb:ff/82/04" : ";rgb:00/00/00";
    }
    expected += "\x1b\\";
    expected += clear;
    expected += "\x1b[38;5;1;48;5;0m";
    expected += block;
    expected += block;
    expected += block;
    expected += "\x1b[48;5;1m";
    expected += block;
    expected += end;
    sr::encode(frame, state, out);
    golden(out, expected, "initial palette and half-block packing");

    sr::encode(frame, state, out);
    golden(out, std::string(begin) + std::string(end), "unchanged frame");

    palette[0] = {0, 16, 0};
    palette[255] = {15, 31, 47};
    sr::encode(frame, state, out);
    golden(out, "\x1b[?2026h\x1b]4;0;rgb:00/41/00;255;rgb:3c/7d/be\x1b\\\x1b[?2026l",
           "batched palette update without cell redraw");

    pixels[0] = pixels[2] = 0;
    sr::encode(frame, state, out);
    golden(out,
           "\x1b[?2026h\x1b[1;1H\x1b[38;5;0;48;5;0m\xe2\x96\x80\x1b[C\xe2\x96\x80\x1b[?2026l",
           "CUP, relative cursor movement, and pen reuse");

    frame.width = 2;
    frame.height = 4;
    sr::encode(frame, state, out);
    golden(out,
           "\x1b[?2026h\x1b[0m\x1b[2J\x1b[H\x1b[38;5;0;48;5;0m\xe2\x96\x80"
           "\x1b[38;5;1;48;5;1m\xe2\x96\x80\x1b[2;1H\x1b[38;5;0;48;5;0m\xe2\x96\x80"
           "\x1b[48;5;1m\xe2\x96\x80\x1b[?2026l",
           "mode change clears and resets the cursor, pen and cell state");

    frame.viewport = {6, 6};
    sr::encode(frame, state, out);
    const std::string centered =
        "\x1b[?2026h\x1b[0m\x1b[2J\x1b[H\x1b[3;3H\x1b[38;5;0;48;5;0m\xe2\x96\x80"
        "\x1b[38;5;1;48;5;1m\xe2\x96\x80\x1b[4;3H\x1b[38;5;0;48;5;0m\xe2\x96\x80"
        "\x1b[48;5;1m\xe2\x96\x80\x1b[?2026l";
    golden(out, centered, "resize clears and redraws a centered picture");

    frame.viewport = {7, 7};
    sr::encode(frame, state, out);
    golden(out, centered, "resize clears even when centering offsets are unchanged");
}

void fitAndCenter()
{
    using sr::Scale;
    const auto full = sr::terminalGrid(320, 200, Scale::Full);
    const auto half = sr::terminalGrid(320, 200, Scale::Half);
    check(sr::fitScale({320, 100}, full, half, Scale::Auto) == Scale::Full,
          "automatic full on exact fit");
    check(sr::fitScale({319, 100}, full, half, Scale::Auto) == Scale::Half,
          "automatic half when full is too wide");
    check(sr::fitScale({320, 99}, full, half, Scale::Auto) == Scale::Half,
          "automatic half when full is too tall");
    check(sr::fitScale({160, 50}, full, half, Scale::Auto) == Scale::Half,
          "automatic half on exact fit");
    check(!sr::fitScale({159, 50}, full, half, Scale::Auto) &&
              !sr::fitScale({160, 49}, full, half, Scale::Auto),
          "neither scale fits");
    check(sr::fitScale({400, 150}, full, half, Scale::Full) == Scale::Full &&
              !sr::fitScale({319, 100}, full, half, Scale::Full),
          "forced full does not fall back to half");
    check(sr::fitScale({400, 150}, full, half, Scale::Half) == Scale::Half &&
              !sr::fitScale({160, 49}, full, half, Scale::Half),
          "forced half does not switch or crop");

    const auto scrollFull = sr::terminalGrid(640, 350, Scale::Full);
    const auto scrollHalf = sr::terminalGrid(640, 350, Scale::Half);
    check(sr::fitScale({320, 175}, scrollFull, scrollHalf, Scale::Auto) == Scale::Full &&
              sr::fitScale({320, 100}, scrollFull, scrollHalf, Scale::Auto) == Scale::Half &&
              sr::fitScale({160, 87}, scrollFull, scrollHalf, Scale::Auto) == Scale::Half &&
              !sr::fitScale({320, 86}, scrollFull, scrollHalf, Scale::Auto),
          "end-scroller full and half boundaries");

    const auto centered = sr::centeringOffset({401, 182}, scrollFull);
    check(centered.col == 40 && centered.row == 3, "center rounds odd margins down");
    const auto exact = sr::centeringOffset(full, full);
    check(exact.col == 0 && exact.row == 0, "exact fit has no offset");

    std::vector<uint8_t> out;
    sr::encodeFitMessage({100, 37}, full, half, out);
    golden(out, "\x1b[?2026h\x1b[0m\x1b[2J\x1b[H\x1b[19;4H"
                "needs 320×100 cells (half: 160×50); current 100×37. "
                "Enlarge the window or reduce the font size\x1b[?2026l",
           "warning displays both required sizes and the current size, centered");
}

void paletteBatchBoundary()
{
    sr::Palette palette = {};
    const std::array<uint8_t, 2> pixels = {0, 0};
    sr::State state;
    std::vector<uint8_t> out;
    const sr::Frame frame{pixels, palette, 1, 2};
    sr::encode(frame, state, out);
    for (int i = 8; i <= 15; ++i)
        palette[i] = {63, 0, 0};
    sr::encode(frame, state, out);
    golden(out,
           "\x1b[?2026h\x1b]4;8;rgb:ff/00/00;9;rgb:ff/00/00;10;rgb:ff/00/00"
           ";11;rgb:ff/00/00;12;rgb:ff/00/00;13;rgb:ff/00/00;14;rgb:ff/00/00"
           "\x1b\\\x1b]4;15;rgb:ff/00/00\x1b\\\x1b[?2026l",
           "eighth changed entry starts a second OSC within synchronized output");
}

void mode(int width, int height, sr::Scale scale, int dx, int dy, sr::Grid grid)
{
    sr::Palette palette = {};
    std::vector<uint8_t> pixels(width * height);
    auto pixel = [](int x, int y) { return (7 * x + 31 * y) & 255; };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[y * width + x] = pixel(x, y);
    sr::State state;
    std::vector<uint8_t> out;
    sr::encode({pixels, palette, width, height, scale}, state, out);
    check(state.grid == grid, "mode grid");
    for (int y = 0; y < grid.rows; ++y)
        for (int x = 0; x < grid.cols; ++x)
            check(state.cells[y * grid.cols + x] ==
                      ((pixel(dx * x, 2 * dy * y) << 8) | pixel(dx * x, 2 * dy * y + dy)),
                  "mode decimation and top/bottom order");
}

} // namespace

int main()
{
    smallFrames();
    fitAndCenter();
    paletteBatchBoundary();
    using sr::Scale;
    mode(320, 200, Scale::Full, 1, 1, {320, 100});
    mode(320, 200, Scale::Half, 2, 2, {160, 50});
    mode(320, 400, Scale::Full, 1, 2, {320, 100});
    mode(320, 400, Scale::Half, 2, 4, {160, 50});
    mode(640, 350, Scale::Full, 2, 1, {320, 175});
    mode(640, 350, Scale::Half, 4, 2, {160, 87});
    std::cout << "Encoder tests passed\n";
}
