#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sr {

enum class Scale { Full, Half, Auto };

struct Grid {
    int cols;
    int rows;
    bool operator==(const Grid&) const = default;
};

struct Offset {
    int col, row;
};

struct Rgb {
    uint8_t red, green, blue;
    bool operator==(const Rgb&) const = default;
};

using Palette = std::array<Rgb, 256>;

struct Frame {
    // Tightly packed visible VGA rows, starting at the display start address.
    std::span<const uint8_t> pixels;
    const Palette& palette;
    int width, height;
    Scale scale = Scale::Full;
    // An empty viewport means the terminal is sized to the picture.
    Grid viewport = {};
};

struct State {
    int width = 0, height = 0;
    Scale scale = Scale::Full;
    Grid grid = {}, viewport = {};
    Palette palette = {};
    std::vector<uint16_t> cells;
    int row = 0, col = 0;
    int fg = -1, bg = -1;
};

Grid terminalGrid(int width, int height, Scale scale);
std::optional<Scale> fitScale(Grid viewport, Grid full, Grid half, Scale requested);
Offset centeringOffset(Grid viewport, Grid picture);
void encodeFitMessage(Grid viewport, Grid full, Grid half, std::vector<uint8_t>& out);
void encode(const Frame& frame, State& state, std::vector<uint8_t>& out);

} // namespace sr
