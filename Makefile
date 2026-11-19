CXXFLAGS ?= -O3
CPPFLAGS += -Isrc
BUILD := build
CXX_STANDARD := -std=c++20
WARNINGS := -Wall -Wextra -Wpedantic

.PHONY: test
test: $(BUILD)/encoder-tests
	./$(BUILD)/encoder-tests

$(BUILD)/encoder-tests: tests/encoder.cpp src/encoder.cpp src/encoder.h
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CXX_STANDARD) $(WARNINGS) $(filter %.cpp,$^) -o $@

EMXX ?= em++
PUZZLE_PKG ?= ../puzzle-term/dist/pkg
WEB_BUILD := $(BUILD)/web
WEB_FLAGS := -sASYNCIFY -sALLOW_MEMORY_GROWTH -sMODULARIZE -sEXPORT_ES6 \
	-sENVIRONMENT=web --post-js web/runtime.js --extern-post-js web/player.js
BASE := src/encoder.cpp src/presenter.cpp
BASE_HEADERS := src/encoder.h src/presenter.h
PROBE := probe/stress.cpp

.PHONY: probe web-probe web-host
probe: $(BUILD)/probe/stress
web-probe: web-host $(WEB_BUILD)/stress.js

$(BUILD)/probe/stress: $(PROBE) $(BASE) src/sink_stdout.cpp $(BASE_HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CXX_STANDARD) $(WARNINGS) $(filter %.cpp,$^) -o $@

$(WEB_BUILD)/stress.js: $(PROBE) $(BASE) src/sink_web.cpp $(BASE_HEADERS) web/runtime.js web/player.js Makefile
	@mkdir -p $(@D)
	$(EMXX) $(CPPFLAGS) $(CXXFLAGS) $(CXX_STANDARD) $(WARNINGS) $(filter %.cpp,$^) $(WEB_FLAGS) -o $@

web-host:
	@test -f "$(PUZZLE_PKG)/puzzle_web.js" && test -f "$(PUZZLE_PKG)/puzzle_web_bg.wasm" && \
		test -d "$(PUZZLE_PKG)/assets" || \
		{ echo "Missing Puzzle package at $(PUZZLE_PKG)" >&2; exit 1; }
	@mkdir -p $(WEB_BUILD)
	cp web/index.html web/app.js $(WEB_BUILD)/
	ln -sfn "$(abspath $(PUZZLE_PKG))" $(WEB_BUILD)/puzzle

SRPP := third_party/SecondRealityPlusPlus
SRPP_CPP := $(shell find $(addprefix $(SRPP)/,Blob Music Parts) -name '*.cpp' -print 2>/dev/null) \
	$(SRPP)/Graphics/shims.cpp
SRPP_HEADERS := $(shell find $(addprefix $(SRPP)/,Blob Music Parts Graphics) -name '*.h' -print 2>/dev/null)
DEMO := src/main.cpp src/term_graphics.cpp
DEMO_HEADERS := src/term_graphics.h $(BASE_HEADERS)
PARTS := $(BUILD)/parts.inc
SDL3_CFLAGS = $(shell pkg-config --cflags sdl3)
SDL3_LIBS = $(shell pkg-config --libs sdl3)

.PHONY: native web
native: $(BUILD)/ansi-second-reality
web: web-host $(WEB_BUILD)/ansi-second-reality.js $(WEB_BUILD)/stress.js

# SR++ keeps its scene table private to main.cpp. Read the numbered rows
# through the table's first terminator, excluding the hidden part.
$(PARTS): $(SRPP)/main.cpp Makefile
	@mkdir -p $(@D)
	@awk '/main_parts\[\] =/ { table = 1; next } table && /Song::COUNT/ { table = 0 } table && /\/\* [0-9][0-9] / { line = $$0; sub(/^.*\*\/[[:space:]]*/, "", line); print line; found++ } END { if (found != 19) exit 1 }' "$<" > "$@.tmp"
	@mv "$@.tmp" "$@"

$(BUILD)/ansi-second-reality: $(DEMO) $(BASE) src/sink_stdout.cpp $(SRPP_CPP) $(DEMO_HEADERS) $(SRPP_HEADERS) $(PARTS)
	$(CXX) $(CPPFLAGS) -I$(SRPP) -I$(BUILD) $(SDL3_CFLAGS) $(CXXFLAGS) $(CXX_STANDARD) \
		$(filter %.cpp,$^) $(SDL3_LIBS) -o $@

$(WEB_BUILD)/ansi-second-reality.js: $(DEMO) $(BASE) src/sink_web.cpp $(SRPP_CPP) $(DEMO_HEADERS) $(SRPP_HEADERS) $(PARTS) web/runtime.js web/player.js
	@mkdir -p $(@D)
	$(EMXX) $(CPPFLAGS) -I$(SRPP) -I$(BUILD) $(CXXFLAGS) $(CXX_STANDARD) \
		$(filter %.cpp,$^) $(WEB_FLAGS) -fexceptions --use-port=sdl3 -o $@
