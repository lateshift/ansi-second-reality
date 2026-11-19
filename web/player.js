const enterScreen = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[0m\x1b[2J\x1b[H";
const leaveScreen = "\x1b[?2026l\x1b]104\x1b\\\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l";

function measurePlayback() {
  let started = 0, ended = 0, lastSample = 0, raf = 0;
  let frames = 0, bytes = 0, busySkips = 0, deadlineSkips = 0, latency = 0;
  let callbacks = 0, firstRaf = 0, lastRaf = 0;
  let previousFrames = 0, previousBytes = 0, previousSkips = 0, previousLatency = 0;
  const writeTimes = [], encodeTimes = [];
  let lastReading;

  function sampleRaf(now) {
    if (!callbacks) firstRaf = now;
    lastRaf = now;
    callbacks++;
    raf = requestAnimationFrame(sampleRaf);
  }

  function read(now) {
    const final = Boolean(ended);
    const interval = Math.max(0.001, (now - (final ? started : lastSample)) / 1000);
    const completed = frames - (final ? 0 : previousFrames);
    const transferred = bytes - (final ? 0 : previousBytes);
    const skips = busySkips + deadlineSkips;
    const skipped = skips - (final ? 0 : previousSkips);
    const writeMs = latency - (final ? 0 : previousLatency);
    lastSample = now;
    previousFrames = frames;
    previousBytes = bytes;
    previousSkips = skips;
    previousLatency = latency;
    return {
      seconds: (now - started) / 1000,
      fps: completed / interval,
      bytesPerFrame: completed ? transferred / completed : 0,
      bytesPerSecond: transferred / interval,
      skipsPerSecond: skipped / interval,
      skips,
      meanWriteMs: completed ? writeMs / completed : 0,
      summary: final ? {
        rafHz: callbacks > 1 ? 1000 * (callbacks - 1) / (lastRaf - firstRaf) : 0,
        callbacks, writeMs: percentiles(writeTimes), encodeMs: percentiles(encodeTimes),
        busySkips, deadlineSkips,
      } : undefined,
    };
  }

  return {
    start() {
      started = lastSample = performance.now();
      raf = requestAnimationFrame(sampleRaf);
    },
    written(size, elapsed, encodeMs) {
      frames++;
      bytes += size;
      latency += elapsed;
      writeTimes.push(elapsed);
      encodeTimes.push(encodeMs);
    },
    skip(busy) {
      if (busy) busySkips++;
      else deadlineSkips++;
    },
    stop() {
      if (!started) return;
      cancelAnimationFrame(raf);
      ended = performance.now();
      lastReading = read(ended);
      writeTimes.length = encodeTimes.length = 0;
    },
    read() {
      const now = performance.now();
      if (!ended && started && (!lastReading || now - lastSample >= 900))
        lastReading = read(now);
      return lastReading;
    },
  };
}

function percentiles(times) {
  const sorted = [...times].sort((a, b) => a - b);
  return [0.5, 0.95].map(p => (sorted[Math.ceil(sorted.length * p) - 1] ?? 0).toFixed(2));
}

export function formatSrStats(reading) {
  if (!reading) return "";
  const { seconds, fps, bytesPerFrame, bytesPerSecond, skipsPerSecond, skips, meanWriteMs, summary } = reading;
  const lines = [
    `${seconds.toFixed(1)} s | ${fps.toFixed(1)} fps`,
    `${bytesPerFrame.toFixed(0)} B/frame | ${bytesPerSecond.toFixed(0)} B/s`,
    `${skipsPerSecond.toFixed(1)} skips/s | ${skips} total skips`,
    `${meanWriteMs.toFixed(2)} ms mean write latency`,
  ];
  if (summary) lines.push(
    `${summary.rafHz.toFixed(1)} Hz rAF (${summary.callbacks} callbacks)`,
    `Write p50 / p95 ${summary.writeMs.join(" / ")} ms`,
    `Encode p50 / p95 ${summary.encodeMs.join(" / ")} ms`,
    `${summary.busySkips} pending-write skips | ${summary.deadlineSkips} expired-tick skips`,
  );
  return lines.join("\n");
}

async function mountPlayback({ terminal, onState, onGrid, args, sound, scale }) {
  let quit = false, entered = false, pending, input, audioContext, sdlAudio;
  let failure;
  const stats = measurePlayback();

  function notify(state, detail) {
    try {
      onState?.(state, detail);
    } catch (callbackError) {
      console.error(callbackError);
    }
  }

  async function release() {
    input?.dispose();
    input = undefined;
    // SDL destroys and disconnects its device on return from main. Its web
    // driver keeps an empty recording slot, so it can leave the context open.
    // Remove any remaining audio callbacks even if WASM exited with a trap.
    try {
      const playback = sdlAudio?.audio_playback;
      if (playback?.silenceTimer !== undefined) clearInterval(playback.silenceTimer);
      if (playback?.scriptProcessorNode) {
        playback.scriptProcessorNode.onaudioprocess = null;
        playback.scriptProcessorNode.disconnect();
      }
      if (sdlAudio) sdlAudio.audioContext = undefined;
      if (audioContext && audioContext.state !== "closed") await audioContext.close();
    } finally {
      sdlAudio = audioContext = undefined;
      try {
        await pending;
        stats.stop();
        if (entered) await terminal.write(leaveScreen);
      } finally {
        terminal = onGrid = undefined;
      }
    }
  }

  let module;
  try {
    if (sound) {
      const Audio = globalThis.AudioContext ?? globalThis.webkitAudioContext;
      if (!Audio) throw new Error("Web Audio is unavailable");
      // This runs before the first await, while the play gesture is active.
      audioContext = new Audio();
      if (audioContext.state === "suspended") audioContext.resume().catch(() => {});
    }
    module = await Module({
      noInitialRun: true,
      terminalQuitting: () => quit,
      terminalReady: () => !quit && !pending,
      terminalPart: index => notify("part", index),
      terminalHalfScale: () => scale === "half",
      terminalResize: (cols, rows) => {
        if (onGrid) onGrid(cols, rows);
        else terminal.resize(cols, rows);
      },
      terminalWrite: (frame, encodeMs) => {
        const began = performance.now(), size = frame.byteLength;
        pending = terminal.write(frame).then(() => {
          stats.written(size, performance.now() - began, encodeMs);
        }).catch(error => {
          failure = error;
          quit = true;
        }).finally(() => {
          pending = undefined;
        });
        return 1;
      },
      terminalSkipped: stats.skip,
      configureAudio(audio) {
        sdlAudio = audio;
        if (audioContext) audio.audioContext = audioContext;
      },
    });
    await terminal.write(enterScreen);
    entered = true;
    // Puzzle dispatches terminal-encoded input from its own focused editor.
    input = terminal.onData(data => {
      if (terminal.hasFocus && data.length === 1 && (data[0] === 27 || data[0] === 113 || data[0] === 81))
        quit = true;
    });
  } catch (error) {
    try {
      await release();
    } finally {
      notify("error", error);
      onState = undefined;
    }
    throw error;
  }

  stats.start();
  notify("playing");
  const finished = (async () => {
    try {
      const status = await module.runPlayback(args);
      if (status) throw new Error(`ansi-second-reality exited with status ${status}`);
    } catch (error) {
      failure ??= error;
    } finally {
      module = undefined;
      try {
        await release();
      } catch (error) {
        failure ??= error;
      }
      if (failure) notify("error", failure);
      else notify("ended");
      onState = undefined;
    }
  })();

  return {
    stats: stats.read,
    setScale(nextScale) {
      if (!["auto", "full", "half"].includes(nextScale))
        throw new RangeError("Expected auto, full or half scale");
      scale = nextScale;
    },
    destroy() {
      quit = true;
      return finished;
    },
  };
}

export function mountSecondReality({ terminal, part = 0, scale = "auto", onState, onGrid, mute = false }) {
  const args = ["--part", String(part)];
  if (scale !== "auto") args.push("--scale", scale);
  if (mute) args.push("--no-audio");
  return mountPlayback({ terminal, onState, onGrid, args, sound: !mute, scale });
}

export function mountSrProbe({ terminal, scale, palette, onState, onGrid }) {
  const args = ["--scale", scale === "half" ? "half" : "full"];
  if (palette) args.push("--palette");
  return mountPlayback({ terminal, onState, onGrid, args, sound: false });
}
