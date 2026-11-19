#pragma once

#include "encoder.h"

#include <chrono>
#include <memory>
#include <string_view>

namespace sr {

using Clock = std::chrono::steady_clock;
inline constexpr auto tickPeriod = std::chrono::nanoseconds(1'000'000'000 / 70);

class TickClock {
public:
    Clock::time_point wait();
private:
    Clock::time_point next_ = Clock::now();
};

enum class SkipReason { Deadline, Backpressure };

struct Viewport {
    Grid size;
    bool resized;
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual bool quitting() = 0;
    virtual bool ready() = 0;
    // Native reports its fixed PTY size. The web sink sizes itself to the picture.
    virtual std::optional<Viewport> viewport() { return std::nullopt; }
    virtual void prepare(Grid) {}
    virtual bool writeFrame(std::span<const uint8_t> bytes, double encodeMs) = 0;
    virtual void skipped(SkipReason) {}
    virtual void close() = 0;
};

std::unique_ptr<Sink> openSink();

class Presenter {
public:
    explicit Presenter(Sink& sink);
    bool present(Frame frame, Clock::time_point deadline);
    bool refreshLayout(int width, int height, Scale requested);
    bool ready(Clock::time_point deadline);
    void skip(SkipReason reason);
    void printStats(std::string_view label) const;
    void startPart(unsigned index);
    void printPartStats() const;

private:
    struct Sample {
        Clock::time_point time;
        size_t bytes;
        double encodeMs, writeMs;
    };
    struct Part {
        unsigned index;
        size_t sample, skipped;
        Clock::time_point started;
    };
    void printStats(std::string_view label, std::span<const Sample> samples,
                    size_t skipped, Clock::duration duration) const;

    Sink& sink_;
    Clock::time_point started_ = Clock::now();
    Grid mode_ = {};
    Grid viewport_ = {};
    Scale requested_ = Scale::Auto;
    std::optional<Scale> selected_;
    State state_;
    std::vector<uint8_t> bytes_;
    std::vector<Sample> samples_;
    std::vector<Part> parts_;
    size_t skipped_ = 0;
};

} // namespace sr
