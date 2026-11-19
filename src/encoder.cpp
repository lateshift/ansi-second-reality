#include "encoder.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>

namespace sr {
namespace {

using Bytes = std::vector<uint8_t>;
constexpr std::string_view beginFrame = "\x1b[?2026h";
constexpr std::string_view clearScreen = "\x1b[0m\x1b[2J\x1b[H";
constexpr std::string_view endFrame = "\x1b[?2026l";

struct Sampling {
    int x, y;
};

Sampling sampling(int width, int height, Scale scale)
{
    assert(scale != Scale::Auto);
    const int factor = scale == Scale::Half ? 2 : 1;
    return {factor * (width == 640 ? 2 : 1),
            factor * (height == 400 ? 2 : 1)};
}

void append(Bytes& out, std::string_view sequence)
{
    out.insert(out.end(), sequence.begin(), sequence.end());
}

void decimal(Bytes& out, int value)
{
    char buffer[12];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.insert(out.end(), buffer, result.ptr);
}

void cup(Bytes& out, int row, int col)
{
    append(out, "\x1b[");
    decimal(out, row);
    out.push_back(';');
    decimal(out, col);
    out.push_back('H');
}

int digits(int value)
{
    int count = 1;
    while (value >= 10) {
        value /= 10;
        ++count;
    }
    return count;
}

int relativeLength(int distance)
{
    return distance == 0 ? 0 : 3 + (distance == 1 ? 0 : digits(distance));
}

void relative(Bytes& out, int delta, char positive, char negative)
{
    if (delta == 0)
        return;
    append(out, "\x1b[");
    if (std::abs(delta) != 1)
        decimal(out, std::abs(delta));
    out.push_back(delta > 0 ? positive : negative);
}

void move(Bytes& out, State& state, int row, int col, int lastCol)
{
    const int dy = row - state.row;
    const int dx = col - state.col;
    const int cupLength = 4 + digits(row) + digits(col);
    if (state.col != 0 && relativeLength(std::abs(dy)) + relativeLength(std::abs(dx)) <= cupLength) {
        relative(out, dy, 'B', 'A');
        relative(out, dx, 'C', 'D');
    } else {
        cup(out, row, col);
    }
    state.row = row;
    // With wrap disabled, writing at the terminal's right edge does not advance.
    state.col = col == lastCol ? 0 : col + 1;
}

void color(Bytes& out, uint8_t value)
{
    constexpr std::string_view hex = "0123456789abcdef";
    const int dac = value & 63;
    const int scaled = (dac << 2) | (dac >> 4);
    out.push_back(hex[scaled >> 4]);
    out.push_back(hex[scaled & 15]);
}

void syncPalette(const Palette& palette, State& state, Bytes& out, bool first)
{
    // vte 0.15 exposes at most 16 OSC fields: the command and seven full pairs.
    constexpr int maxPalettePairs = 7;
    int pairs = 0;
    for (size_t i = 0; i < palette.size(); ++i) {
        const Rgb rgb = palette[i];
        if (!first && rgb == state.palette[i])
            continue;
        if (pairs == maxPalettePairs) {
            append(out, "\x1b\\");
            pairs = 0;
        }
        if (pairs == 0)
            append(out, "\x1b]4");
        ++pairs;
        out.push_back(';');
        decimal(out, static_cast<int>(i));
        append(out, ";rgb:");
        color(out, rgb.red);
        out.push_back('/');
        color(out, rgb.green);
        out.push_back('/');
        color(out, rgb.blue);
        state.palette[i] = rgb;
    }
    if (pairs != 0)
        append(out, "\x1b\\");
}

void pen(Bytes& out, State& state, int fg, int bg)
{
    const bool changeFg = fg != state.fg;
    const bool changeBg = bg != state.bg;
    if (!changeFg && !changeBg)
        return;
    append(out, "\x1b[");
    if (changeFg) {
        append(out, "38;5;");
        decimal(out, fg);
        state.fg = fg;
    }
    if (changeBg) {
        if (changeFg)
            out.push_back(';');
        append(out, "48;5;");
        decimal(out, bg);
        state.bg = bg;
    }
    out.push_back('m');
}

} // namespace

Grid terminalGrid(int width, int height, Scale scale)
{
    const Sampling sample = sampling(width, height, scale);
    return {width / sample.x, height / (2 * sample.y)};
}

std::optional<Scale> fitScale(Grid viewport, Grid full, Grid half, Scale requested)
{
    if (requested != Scale::Half && viewport.cols >= full.cols && viewport.rows >= full.rows)
        return Scale::Full;
    if (requested != Scale::Full && viewport.cols >= half.cols && viewport.rows >= half.rows)
        return Scale::Half;
    return std::nullopt;
}

Offset centeringOffset(Grid viewport, Grid picture)
{
    assert(viewport.cols >= picture.cols && viewport.rows >= picture.rows);
    return {(viewport.cols - picture.cols) / 2, (viewport.rows - picture.rows) / 2};
}

void encodeFitMessage(Grid viewport, Grid full, Grid half, Bytes& out)
{
    auto size = [](Grid grid) {
        return std::to_string(grid.cols) + "×" + std::to_string(grid.rows);
    };
    const std::string text = "needs " + size(full) + " cells (half: " + size(half) +
                             "); current " + size(viewport) +
                             ". Enlarge the window or reduce the font size";
    // The warning contains ASCII and multiplication signs, all one cell wide.
    auto firstByte = [](unsigned char byte) { return (byte & 0xc0) != 0x80; };
    struct Line { std::string_view text; int cols; };
    std::vector<Line> lines;
    std::string_view remaining = text;
    while (viewport.cols > 0 && !remaining.empty() && static_cast<int>(lines.size()) < viewport.rows) {
        size_t end = 0;
        int cols = 0;
        while (end < remaining.size() && cols < viewport.cols) {
            ++cols;
            do { ++end; } while (end < remaining.size() && !firstByte(remaining[end]));
        }
        if (end < remaining.size() && remaining[end] != ' ') {
            const size_t space = remaining.rfind(' ', end);
            if (space != std::string_view::npos) {
                end = space;
                cols = static_cast<int>(std::count_if(remaining.begin(), remaining.begin() + end, firstByte));
            }
        }
        lines.push_back({remaining.substr(0, end), cols});
        remaining.remove_prefix(end);
        if (remaining.starts_with(' '))
            remaining.remove_prefix(1);
    }

    out.clear();
    append(out, beginFrame);
    append(out, clearScreen);
    int row = (viewport.rows - static_cast<int>(lines.size())) / 2 + 1;
    for (const auto& line : lines) {
        cup(out, row++, (viewport.cols - line.cols) / 2 + 1);
        append(out, line.text);
    }
    append(out, endFrame);
}

void encode(const Frame& frame, State& state, Bytes& out)
{
    assert(frame.width > 0 && frame.height > 0);
    assert(frame.pixels.size() >= static_cast<size_t>(frame.width) * frame.height);
    const Sampling sample = sampling(frame.width, frame.height, frame.scale);
    const Grid grid = terminalGrid(frame.width, frame.height, frame.scale);
    const Grid viewport = frame.viewport == Grid{} ? grid : frame.viewport;
    const Offset offset = centeringOffset(viewport, grid);

    const bool first = state.width == 0;
    const bool reset = state.width != frame.width || state.height != frame.height ||
                       state.scale != frame.scale || state.viewport != frame.viewport || state.grid != grid;
    out.clear();
    append(out, beginFrame);
    syncPalette(frame.palette, state, out, first);
    if (reset) {
        append(out, clearScreen);
        state.width = frame.width;
        state.height = frame.height;
        state.scale = frame.scale;
        state.viewport = frame.viewport;
        state.grid = grid;
        state.cells.resize(static_cast<size_t>(grid.cols) * grid.rows);
        state.fg = state.bg = -1;
        state.row = state.col = 1;
    }

    size_t cell = 0;
    for (int y = 0; y < grid.rows; ++y) {
        const auto top = frame.pixels.data() + static_cast<size_t>(y * 2 * sample.y) * frame.width;
        const auto bottom = top + sample.y * frame.width;
        for (int x = 0; x < grid.cols; ++x, ++cell) {
            const int fg = top[x * sample.x];
            const int bg = bottom[x * sample.x];
            const auto pair = static_cast<uint16_t>((fg << 8) | bg);
            if (!reset && state.cells[cell] == pair)
                continue;
            state.cells[cell] = pair;
            move(out, state, offset.row + y + 1, offset.col + x + 1, viewport.cols);
            pen(out, state, fg, bg);
            append(out, "\xe2\x96\x80");
        }
    }
    append(out, endFrame);
}

} // namespace sr
