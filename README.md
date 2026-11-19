# ANSI Second Reality

**Future Crew's *Second Reality* (1993), played entirely through terminal escape sequences.**

This is an ANSI/terminal port of the legendary Assembly '93 demo. Every frame of every part is turned into text: half-block characters, 256-colour indices and live palette updates. Any modern terminal can play it. It runs natively in Puzzle Terminal, Ghostty and others, and in the browser inside a web terminal. The music plays alongside, in sync.

No effect was rewritten for this port. The demo itself runs unchanged from [SecondReality++](https://github.com/XorJS/SecondRealityPlusPlus). Only the screen output was replaced: where SR++ would upload a VGA frame to OpenGL, this project encodes it as escape sequences.

> "ANSI" here means escape-sequence graphics: a 256-colour palette, OSC 4 palette updates, half-blocks and synchronized output. It is not classic 16-colour BBS ANSI art.

---

## Credits

*Second Reality* is the work of **Future Crew**. All the art in it is theirs: the effects, the graphics and the music. This project only changes how the pixels reach your screen.

### Future Crew: *Second Reality*, Assembly '93

| Handle | Name | Role |
|---|---|---|
| **Psi** | Sami Tammilehto | Code |
| **Trug** | Mika Tuomi | Code |
| **Wildfire** | Arto Vuori | Code |
| **Purple Motion** | Jonne Valtonen | Music |
| **Skaven** | Peter Hajba | Music |
| **Marvel** | Aki Määttä | Graphics |
| **Pixel** | Mikko Iho | Graphics |
| **GORE** | Samuli Syvähuoko | Organizer |
| **Abyss** | Jussi Laakkonen | BBS coordinator |

Member list as published by Future Crew in `FCINFO10.TXT` (1993), shipped with the demo. In 2013, for the demo's 20th anniversary, Future Crew released the [original source code and data](https://github.com/mtuomi/SecondReality) into the public domain (Unlicense).

### The ports this project stands on

- **[SecondRealityW32](https://github.com/ConspiracyHu/SecondRealityW32)** by **Gargaj / Conspiracy** (2025): the reconstruction of the original DOS code (C, Pascal and assembly) for Win32 and modern PCs.
- **[SecondReality++](https://github.com/XorJS/SecondRealityPlusPlus)** by **XorJS** (Jean-Sebastien): based on the Win32 port. It moves the remaining assembly and Pascal to portable C++ and emulates the VGA framebuffer and palette in software. That emulation is what makes this terminal port possible. Unlicense.
- **[Fabien Sanglard's Second Reality code review](https://fabiensanglard.net/second_reality/index.php):** recommended reading on how the original works.

This project uses SR++ unmodified, as a pinned git submodule (`third_party/SecondRealityPlusPlus`).

### Browser terminal

The browser player was built and tested with **Puzzle Terminal** (MIT) as its web terminal. The native player needs no particular terminal.

---

## How it works

### The pipeline

```
SecondReality++ part code (unchanged)
   │  writes 8-bit palette indices into an emulated VGA framebuffer (Shim::vram)
   │  and colours into an emulated 256-entry DAC palette (Shim::palette)
   ▼
demo_blit()  ── replaced here (src/term_graphics.cpp) ──►  Presenter (src/presenter.cpp)
                                                            │  70 Hz demo clock, frame skipping
                                                            ▼
                                                  Encoder (src/encoder.cpp)
                                                            │  VGA frame + palette → escape-sequence bytes
                                              ┌─────────────┴─────────────┐
                                              ▼                           ▼
                                  stdout sink (native)          web sink (emscripten)
                                  any terminal, via a PTY       terminal.write() on a web page
```

SR++ keeps its effects, its demo flow and its music player (SDL3 audio). This project supplies its own `Graphics` backend and `demo_*` entry points, so the part code never knows it is drawing into a terminal.

### Pixels: two per cell, using half-blocks

Each terminal cell shows **two vertically stacked pixels** with the character `▀` (U+2580, upper half block):

- the **foreground** colour paints the upper half, which is the top pixel;
- the **background** colour paints the lower half, which is the bottom pixel.

A 320×200 VGA frame therefore becomes **320 columns × 100 rows**. Terminal cells are about twice as tall as they are wide, so each half-cell is roughly square, like a pixel.

### Colours: the terminal palette is the VGA palette

Second Reality is a palette-indexed demo. Every pixel is an index from 0 to 255 into a 256-colour palette, and many effects (fades, flashes, colour cycling) only change the palette, not the pixels.

The encoder mirrors that directly:

- Cells reference colours **by index** (`ESC[38;5;Nm` for the top pixel, `ESC[48;5;Mm` for the bottom), never as RGB.
- Each frame, every palette entry that changed since the last frame is loaded into the terminal's own 256-colour palette with **OSC 4** (`ESC]4;i;rgb:rr/gg/bb ST`). The terminal then recolours every cell using that index, with no cell rewritten.
- VGA DAC values are 6-bit. They are expanded to 8-bit as `(v << 2) | (v >> 4)`, so 63 maps to 255, not 252.
- Palette updates are batched as **seven index/colour pairs per OSC 4 sequence**. Parsers based on the Rust `vte` crate (used by several terminals) accept at most 16 OSC parameters: the command plus seven pairs. An eighth pair would be dropped silently.

A full-screen fade therefore costs only a few hundred bytes per frame, the same way it cost almost nothing on real VGA hardware.

### Only what changed

- The encoder remembers the (top, bottom) index pair of every cell and **only emits cells whose pair changed**.
- **Cursor movement** uses whichever is shorter for each jump: an absolute `CUP` (`ESC[row;colH`) or relative moves (`ESC[nA/B/C/D`). Runs of adjacent changed cells need no movement at all.
- **The SGR pen is tracked:** colour parameters are sent only when the foreground or background index actually changes.
- **Each frame is wrapped in synchronized output** (`ESC[?2026h` … `ESC[?2026l`), so terminals that support mode 2026 show it atomically, without tearing.

### Video modes and scaling

The demo switches between VGA modes. The encoder samples pixels: every Nth pixel is taken, with no averaging, because palette indices can't be averaged.

| Demo mode | Used by | Full scale (cells) | Half scale (cells) |
|---|---|---|---|
| 320×200 | most parts | 320×100 | 160×50 |
| 320×400 | e.g. intro, logo, Glenz, credits | 320×100 (every 2nd row) | 160×50 |
| 640×350 | end scroller | 320×175 (every 2nd column) | 160×87 |

The native player reads the terminal size (`TIOCGWINSZ`) and **picks full or half scale automatically**, centring the picture. It re-checks when the terminal is resized or zoomed and when the demo changes mode. If neither scale fits, it shows the required size in the middle of the screen and resumes drawing once it fits. The music and demo clock keep running meanwhile.

### Timing and sync

- **The demo runs on its original 70 Hz VGA clock.** SR++ advances its parts by elapsed 70 Hz ticks and drives the music from the same clock.
- **When output falls behind,** whether the terminal is busy or the display is slower than 70 Hz, the presenter **skips presenting** frames rather than queueing them. The demo keeps advancing, so picture and music never drift apart.
- **In the browser,** a frame is written only when the previous `terminal.write()` promise has settled, which provides backpressure.

### Terminal hygiene

On start, the native player switches to the alternate screen, hides the cursor and disables line wrap. On exit it restores all of that, including the palette (`OSC 104`) and the tty mode. Exit can come from Esc or q, SIGINT/SIGTERM or the end of the demo.

---

## Numbers

Measured on an Apple M5 Max running macOS, with Puzzle Terminal (release build), Ghostty 1.3.1 and Chrome 157.

### Real demo

- **Part 00** (intro credits, mostly text and palette fades): **6.8 KB per frame** on average, p95 106 KB (scroll and redraw moments). It peaks at about 1 MB/s, with 0 skipped frames over 78.7 s in both Puzzle and Ghostty.
- **The whole demo, end to end in the browser** (Puzzle Terminal in Chrome, 120 Hz display): 214 skipped ticks over the full run, all from display timing and none from the terminal falling behind (zero pending-write skips).

### Stress probe (worst and best case)

`probe/stress` pushes synthetic frames through the same encoder, 700 frames at 70 Hz over 10 seconds.

- **Random:** every cell changes every frame, the worst case for the diff. That is 32,000 cells per frame at full scale.
- **Palette:** a static picture with all 256 palette entries rotating every frame, the best case.

| Terminal | Workload | Scale | Frames/s | Throughput | Skips |
|---|---|---|---:|---:|---:|
| Puzzle Terminal (native) | random | full | **70.06** | **49.4 MB/s** | 0 |
| Ghostty | random | full | **70.03** | **49.4 MB/s** | 0 |
| Puzzle Terminal (native) | random | half | 70.06 | 12.4 MB/s | 0 |
| Ghostty | random | half | 70.06 | 12.4 MB/s | 0 |
| Puzzle Terminal (native) | palette | full | 69.99 | 0.38 MB/s | 1 |
| Ghostty | palette | full | 70.08 | 0.38 MB/s | 0 |
| Puzzle Terminal in Chrome, 60 Hz display | random | full | 59.7 | — | display-rate only |
| Puzzle Terminal in Chrome, 60 Hz display | random | half | 70.1 | — | 0 |

The other measurements from these runs:

- **Throughput gap:** a fully random frame is about 700 KB; a palette-only frame is about 5 KB. The gap between the two is what OSC 4 buys.
- **Encoding cost:** about 0.4 ms per full random frame natively, and 0.7 ms (p95 0.8 ms) in the browser (WebAssembly). The budget per frame is 14.3 ms.
- **Native writes:** writing a full random frame to Puzzle's PTY took 4.5 ms on average (p95 5.3 ms).
- **Browser writes:** `terminal.write()` latency was 2.4 / 2.7 ms (p50/p95) for a full random frame and 0.1 / 0.2 ms for a palette frame.
- **Browser frame rate:** a 60 Hz display can show at most 60 of the demo's 70 frames per second. The remaining ticks are skipped by design. That is not the terminal falling behind.

---

## Build and run

You need a C++20 compiler and SDL3: `brew install sdl3` on macOS, or your distribution's SDL3 development package on Linux. Start by initialising the pinned SR++ submodule.

```sh
git submodule update --init third_party/SecondRealityPlusPlus
make test      # encoder unit tests
make native
./build/ansi-second-reality
```

| Option | Effect |
|---|---|
| `--part N` | Start at part N (0–18). |
| `--scale full\|half` | Lock the scale; the default picks automatically. |
| `--no-audio` | Use SDL's dummy audio output; the music player keeps time. |
| `--stats` | Print per-part frame, byte and timing statistics on exit. |

Esc or q quits.

**Terminal requirements:** 256 colours, OSC 4 palette changes, UTF-8 and ideally synchronized output (mode 2026); without mode 2026 the picture may tear. Full scale needs 320×100 cells (320×175 for the end scroller), so use a small font and a large window. Half scale needs 160×50 (160×87).

### The parts

| # | Part | # | Part |
|---|---|---|---|
| 00 | Alkutekstit I (intro) | 10 | Plasmacube |
| 01 | Alkutekstit II | 11 | MiniVectorBalls |
| 02 | Alkutekstit III | 12 | Peilipalloscroll |
| 03 | Logo | 13 | 3D-Sinusfield |
| 04 | Glenz | 14 | Jellypic |
| 05 | Dottitunneli | 15 | Vector Part II |
| 06 | Techno | 16 | Endpictureflash |
| 07 | Panicfake | 17 | Credits / Greetings |
| 08 | Vuori-Scrolli | 18 | End scroller |
| 09 | Rotazoomer | | |

### Browser

The browser build uses Emscripten and SDL3's emscripten port. By default it uses Puzzle Terminal's browser package from `../puzzle-term/dist/pkg`; set `PUZZLE_PKG` to use another location.

```sh
source /path/to/emsdk/emsdk_env.sh
make web
python3 -m http.server 48179 --bind 127.0.0.1 --directory build/web
```

Open `http://127.0.0.1:48179/` in a browser with WebGPU and click **Click to start**; browsers only allow sound after a user gesture.
- `?part=N` starts at a part, and `?scale=half` forces half scale.
- **Stats** shows live throughput.
- `?puzzleUrl=…` loads the terminal module from elsewhere.

### Embedding in a web page

```js
import { mountSecondReality } from "./ansi-second-reality.js"; // keep ansi-second-reality.wasm next to it

const player = await mountSecondReality({
  terminal,            // your web terminal instance; the host owns it
  part: 0,             // starting part
  scale: "auto",       // "auto" | "full" | "half"
  onState: (state, detail) => {},   // "playing" | "part" (index) | "ended" | "error"
  onGrid: (cols, rows) => {},       // optional: fit the grid into your own layout
});

player.stats();          // live statistics
player.setScale("half"); // applies on the next tick, no restart
await player.destroy();  // stops the main loop, closes audio, releases everything
```

Call `mountSecondReality` from a user gesture, for audio. Await `destroy()` before mounting again; restarting is destroy-then-mount. It was tested with 20 consecutive restarts, with no leaked AudioContexts, listeners or memory growth.

### Stress probe

```sh
make probe
./build/probe/stress --scale full            # random, worst case
./build/probe/stress --scale full --palette  # palette rotation, best case
```

In the browser, use `?probe=random` or `?probe=palette`, optionally with `&scale=half`.

---

## Licence

*Second Reality*'s original source and data (Future Crew), and SecondReality++ (XorJS), are released under the [Unlicense](https://unlicense.org/). See the respective repositories. Puzzle Terminal is MIT-licensed.
