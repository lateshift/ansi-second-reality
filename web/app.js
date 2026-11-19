const host = document.querySelector("#terminal");
const gate = document.querySelector("#gate");
const start = document.querySelector("#start");
const status = document.querySelector("#status");
const toggle = document.querySelector("#stats-toggle");
const stats = document.querySelector("#stats");

const params = new URLSearchParams(location.search);
const half = params.get("scale") === "half";
const probe = params.get("probe");
let terminal, mount, formatStats, player;
let cols = half ? 160 : 320;
let rows = half ? 50 : 100;

function resize(newCols = cols, newRows = rows) {
  cols = newCols;
  rows = newRows;
  const dpr = window.devicePixelRatio;
  const width = Math.round(window.innerWidth * dpr);
  const height = Math.round(window.innerHeight * dpr);
  const minFont = 4;
  // Allow for cap-height snapping before rounding cells to device pixels.
  const advance = 0.65;
  const lineHeight = 1.36;
  const minWidth = cols * Math.ceil(minFont * dpr * advance);
  const minHeight = rows * Math.ceil(minFont * dpr * lineHeight);
  host.style.minWidth = `${minWidth / dpr}px`;
  host.style.minHeight = `${minHeight / dpr}px`;
  host.style.setProperty("--canvas-scale", Math.min(1, width / minWidth, height / minHeight));
  const cellWidth = Math.floor(Math.max(width, minWidth) / cols);
  const cellHeight = Math.floor(Math.max(height, minHeight) / rows);
  const size = Math.min(cellWidth / advance, cellHeight / lineHeight) / dpr;
  terminal.settings.set({ "font.size": Math.max(minFont, Math.min(200, Math.floor(size * 10) / 10)) });
  terminal.resize(cols, rows);
}

function showState(state, error) {
  if (state === "playing" || state === "part") return;
  stats.value = formatStats(player?.stats());
  status.textContent = error ? String(error) : "Playback ended";
  gate.hidden = false;
  start.disabled = false;
}

try {
  const puzzleUrl = new URL(params.get("puzzleUrl") ?? "./puzzle/puzzle_web.js", import.meta.url);
  const { default: initPuzzle, PuzzleTerminal } = await import(puzzleUrl.href);
  await initPuzzle();
  terminal = await PuzzleTerminal.create(host, {
    cols, rows,
    assetsUrl: new URL("./assets/", puzzleUrl).href,
    settings: { "font.size": 4, "render.padding": 0, "theme.mode": "dark" },
  });
  resize();
  const runtime = await import(probe ? "./stress.js" : "./ansi-second-reality.js");
  mount = probe ? runtime.mountSrProbe : runtime.mountSecondReality;
  formatStats = runtime.formatSrStats;
  start.disabled = false;
  status.textContent = "";
} catch (error) {
  status.textContent = String(error);
}

start.addEventListener("click", async () => {
  start.disabled = true;
  gate.hidden = true;
  status.textContent = "";
  terminal.focus();
  try {
    player = await mount({
      terminal, part: params.get("part") ?? 0, scale: half ? "half" : "auto",
      onGrid: resize, onState: showState, palette: probe === "palette", mute: params.has("no-audio"),
    });
  } catch (error) {
    showState("error", error);
  }
});
window.addEventListener("resize", () => {
  if (terminal) resize();
});
toggle.addEventListener("click", () => {
  stats.hidden = !stats.hidden;
  toggle.setAttribute("aria-expanded", String(!stats.hidden));
});
setInterval(() => {
  if (player && !stats.hidden) stats.value = formatStats(player.stats());
}, 1000);
