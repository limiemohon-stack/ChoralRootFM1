// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot FM-1 layout tool (docs/LAYOUT-TOOL.md, step 2: a viewer): the model. Pure code, no DOM: the page
// (web/layout.html) and node (web/test_layout.mjs, tools/layout/dump_catalogs.mjs) use the same functions.
//   wrap(ex)            the preview module's exports (build/layout-web/crlayout.wasm: lay_*) as plain calls
//   tiles(catalog)      every screen x lane of a catalog, in the editor's order
//   formatLayout(cat)   a catalog as the text of its layout file (design/layouts/<engine>.json)
//   layoutFileName, ruleNotes, lintCounts, sheetGeometry, GROUP_KNOB_COLOURS ..
import { SKETCH_NAMES, parseSketch, isSketchStyle, drawSketches } from "./fm1sketch.js";

export const SCREEN = 240;
export const FORMAT = "choralroot-layout";
export const FORMAT_VERSION = 1;
// the unit's knob caps, KNOB 1..4 (docs/EDITOR.md: blue, orange, white, green)
export const KNOB_COLOURS = ["blue", "orange", "white", "green"];
export const SHOW_ERRORS = { "-1": "no such engine", "-2": "no such sound", "-3": "the engine has no such group",
                             "-4": "no such screen", "-5": "no such lane" };

// ---------------------------------------------------------------- the module ---
// ex: the raw exports (CRLayoutModule.load()). Strings are C strings in the module's memory, JSON answers are parsed.
export function wrap(ex) {
  const mem = () => new Uint8Array(ex.memory.buffer);
  const cstr = (p) => {
    const m = mem();
    let e = p;
    while (m[e]) e++;
    let s = "";
    for (let i = p; i < e; i++) s += String.fromCharCode(m[i]);   // (the module's JSON is ASCII: · escapes)
    return s;
  };
  const json = (p) => JSON.parse(cstr(p));
  const catalogs = new Map();
  let version = null;
  return {
    ex,
    version() { return version || (version = json(ex.lay_version())); },
    // today's layout of an engine as the unit shows it (kept: the tables do not change while the module lives): its
    // parameter pages the engine's own, its screens the editor's, a compiled layout's when the engine has one (P5's
    // rows: "layout" names the design). The module walks it from its reset state, so the editor is closed afterwards:
    // show() again
    catalog(engine) {
      if (!catalogs.has(engine)) catalogs.set(engine, json(ex.lay_catalog(engine)));
      return catalogs.get(engine);
    },
    // the same, but the screens the editor derives from the engine's own pages (no compiled layout: what layouts(false)
    // shows; the designer's in-place experiments)
    catalogBuiltin(engine) {
      const k = `builtin.${engine}`;
      if (!catalogs.has(k)) catalogs.set(k, json(ex.lay_catalog_builtin(engine)));
      return catalogs.get(k);
    },
    presets(engine) { return json(ex.lay_presets(engine)).presets || []; },
    // the real editor on that sound at that place, from the reset instance; -> the screen's lanes, or throws
    show(engine, preset, group, screen, lane) {
      const n = ex.lay_show(engine, preset, group, screen, lane);
      if (n < 0) throw new Error(`lay_show(${engine}, ${preset}, ${group}, ${screen}, ${lane}): ${SHOW_ERRORS[n] || n}`);
      return n;
    },
    knob(k, detents) { ex.lay_knob(k, detents); },
    select(detents) { ex.lay_select(detents); },
    step(ms) { ex.lay_step(ms); },
    reset() { ex.lay_reset(); },
    // the picture: 240 x 240 RGBA, a copy
    render() { const p = ex.lay_render(); return new Uint8ClampedArray(ex.memory.buffer, p, SCREEN * SCREEN * 4).slice(); },
    // the LCD as the firmware wrote it: RGB565 big-endian, a copy (the emulator's web_fb, for comparisons)
    fb() { return new Uint16Array(ex.memory.buffer, ex.lay_fb(), SCREEN * SCREEN).slice(); },
    lint() { return json(ex.lay_lint()); },
    state() { return json(ex.lay_state()); },
    // a packed design (packDesign) for its engine's classic pages: in effect from the next show(); unload(): none
    load(bytes) {
      const b = bytes || new Uint8Array(0), n = Math.min(b.length, ex.lay_buf_size());
      mem().set(b.subarray(0, n), ex.lay_buf());
      return json(ex.lay_load(ex.lay_buf(), n));
    },
    unload() { return this.load(new Uint8Array([76, 2, 255])); },
    // the compiled layouts (build/gen/cr_layouts.h: P5's rows, the unit's editor) on (the default: as catalog() lists
    // them) or off (the classic derivation of the built-in pages, as catalogBuiltin() lists them); from the next
    // show(). A loaded design shows its own screens either way. -> the previous setting
    layouts(on) { return !!this.ex.lay_layouts(on ? 1 : 0); },
    // a parameter of the editor's sound by its built-in address (page, col; page 255: a part parameter, col its id)
    pget(page, col) { return ex.lay_pget(page, col); },
    ptext(page, col) { return cstr(ex.lay_ptext(page, col)); },
    // the compiled layout of an engine (build/gen/cr_layouts.h) in lay_load's packed form (a classic layout: its
    // classic half, then the free half when it has free screens; free screens alone: the free half): its bytes (none: empty)
    compiled(engine) { const n = ex.lay_compiled(engine); return new Uint8Array(ex.memory.buffer, ex.lay_buf(), n).slice(); },
    // texts drawn by the firmware's faces on an empty page under its title line (the tools' captions): ops [{op:
    // "text", bold, px, x, y, s, col, align, maxw}] -> 240 x 240 RGBA, a copy
    draw(title, page, ops) {
      const buf = ex.lay_buf(), STR = 60000;
      let at = STR;
      const put = (t) => {   // a C string (Latin-1) in the module's buffer (each call copies it)
        const b = toLatin1(t || "");
        if (at + b.length + 1 > 63900) at = STR;
        const p = buf + at;
        mem().set(b, p);
        mem()[p + b.length] = 0;
        at += b.length + 1;
        return p;
      };
      ex.lay_draw_begin(put(title), put(page));
      for (const o of ops) {
        if (o.op === "text") ex.lay_draw_text(o.bold ? 1 : 0, o.px, o.x, o.y, put(o.s), o.col, o.align || 0, o.maxw || 0);
      }
      const p = ex.lay_draw_end();
      return new Uint8ClampedArray(ex.memory.buffer, p, SCREEN * SCREEN * 4).slice();
    },
    textW(s, px, bold) {
      const p = ex.lay_buf() + 60000, b = toLatin1(s);
      mem().set(b, p);
      mem()[p + b.length] = 0;
      return ex.lay_text_w(p, px, bold ? 1 : 0);
    },
    snapshotBytes() { return ex.lay_snapshot_bytes(); },
  };
}

// ------------------------------------------------------------- the catalog ---
// every screen x lane, in the editor's order: [{group, groupName, screen, lane, title, kind, band, cells, key}]
export function tiles(catalog) {
  const out = [];
  for (const g of catalog.groups || [])
    for (const s of g.screens)
      for (const l of s.lanes)
        out.push({ group: g.group, groupName: g.name, button: g.button, screen: s.screen, lane: l.lane, lanes: s.lanes.length,
                   title: l.title, kind: s.kind, type: s.type, band: s.band, tall: s.tall, plan: s.plan ?? null,
                   headings: s.headings || null, pages: s.pages, cells: l.cells,
                   key: `${g.group}.${s.screen}.${l.lane}` });
  return out;
}
export const tileLabel = (t) => `${t.groupName} ${t.screen + 1}${t.lanes > 1 ? "." + (t.lane + 1) : ""} · ${t.title}`;

// the cells bound to a parameter, and the deep-page cells among them
export function counts(catalog) {
  const ts = tiles(catalog);
  const cells = ts.flatMap((t) => t.cells);
  return { groups: (catalog.groups || []).length, screens: (catalog.groups || []).reduce((n, g) => n + g.screens.length, 0),
           lanes: ts.length, cells: cells.filter((c) => c.bind === "deep" || c.bind === "track").length,
           pages: (catalog.pages || []).length, pageCells: catalog.page_cells || 0 };
}

export const cellRange = (c) => (c.min === undefined ? "" : `${c.min} .. ${c.max} (default ${c.def})`);
export function cellBinding(c) {
  if (c.bind === "deep") return `page ${c.page} "${c.page_title}", column ${c.col}`;
  if (c.bind === "track") return `part parameter ${c.param}`;
  if (c.bind === "text") return `fixed text "${c.text}"`;
  return c.bind === "dash" ? "a dash (no parameter in this lane)" : "empty";
}

// The owner's rules for editor pages (docs/LAYOUT-TOOL.md "The owner's rules"), checked against a catalog as far as
// a catalog can say. Rule 1 holds: the shared header IS the editor's title line (the sound's name left, the page's
// right), drawn on every editor page today; the owner: "The current header ... is good, no need to change it." Rule 2
// holds by construction (no cell or element can be bound to a key). Nothing to report.
export function ruleNotes(catalog, tile) {   // eslint-disable-line no-unused-vars
  return [];
}

export function lintCounts(lint) {
  const c = { "off-screen": 0, overlap: 0, ellipsis: 0 };
  for (const f of (lint && lint.findings) || []) c[f.kind] = (c[f.kind] || 0) + 1;
  return c;
}
// the findings that are faults (an ellipsis is a note: the text was cut on purpose)
export const lintFaults = (lint) => ((lint && lint.findings) || []).filter((f) => f.kind !== "ellipsis");

// ---------------------------------------------------------- the layout file ---
export const layoutFileName = (name) => name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") + ".json";

// the catalog as text: two-space indents, a list of plain values or a cell on one line (a diff shows a cell a line)
export function formatLayout(catalog) {
  const plain = (v) => v === null || typeof v !== "object";
  const flat = (v) => Array.isArray(v) ? v.every(plain) : Object.values(v).every((x) => plain(x) || (Array.isArray(x) && x.every(plain)));
  const one = (v) => {
    if (plain(v)) return JSON.stringify(v);
    if (Array.isArray(v)) return "[" + v.map(one).join(", ") + "]";
    return "{" + Object.entries(v).map(([k, x]) => JSON.stringify(k) + ": " + one(x)).join(", ") + "}";
  };
  const fmt = (v, ind) => {
    if (plain(v) || flat(v)) return one(v);
    const pad = ind + "  ";
    if (Array.isArray(v)) return "[\n" + v.map((x) => pad + fmt(x, pad)).join(",\n") + "\n" + ind + "]";
    return "{\n" + Object.entries(v).map(([k, x]) => pad + JSON.stringify(k) + ": " + fmt(x, pad)).join(",\n") + "\n" + ind + "}";
  };
  return fmt(catalog, "") + "\n";
}

// is a layout file for this firmware's engine? -> null, or why not
export function checkLayout(file, version) {
  if (!file || file.format !== FORMAT) return `not a ${FORMAT} file`;
  if (file.version !== FORMAT_VERSION) return `format version ${file.version}, this tool reads ${FORMAT_VERSION}`;
  const e = (version.engines || []).find((x) => x.index === (file.engine || {}).index);
  if (!e) return `engine ${(file.engine || {}).name} is not in ${version.firmware}`;
  if (e.params_hash !== file.params_hash)
    return `made for other parameters of ${e.name} (params_hash ${file.params_hash}, the firmware's ${e.params_hash})`;
  return null;
}

// ------------------------------------------------------- the contact sheet ---
// n tiles of 240 x 240 at `scale`, `cols` a row, a caption line under each, a title line on top
export function sheetGeometry(n, cols, scale = 1) {
  const gap = 12, cap = 22, top = 34, w = SCREEN * scale;
  const rows = Math.ceil(n / cols);
  return { cols, rows, gap, cap, top, tile: w, width: gap + cols * (w + gap), height: top + rows * (w + cap + gap) + gap,
           at: (i) => ({ x: gap + (i % cols) * (w + gap), y: top + Math.floor(i / cols) * (w + cap + gap) }) };
}

// pixels of b that differ from a (two RGBA or RGB565 pictures of one size)
export function diffCount(a, b) {
  let n = 0;
  if (a.length === SCREEN * SCREEN * 4) {
    for (let i = 0; i < a.length; i += 4) n += a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2];
  } else {
    for (let i = 0; i < a.length; i++) n += a[i] !== b[i];
  }
  return n;
}
// pixels that are not the picture's most common colour (a blank screen: 0)
export function inkCount(rgba) {
  const seen = new Map();
  const px = new Uint32Array(rgba.buffer, rgba.byteOffset, rgba.length >> 2);
  for (const p of px) seen.set(p, (seen.get(p) || 0) + 1);
  let top = 0;
  for (const v of seen.values()) top = Math.max(top, v);
  return px.length - top;
}
export function toLatin1(s) {
  const out = [];
  for (const ch of String(s)) { const c = ch.codePointAt(0); out.push(c < 256 ? c : 63); }
  return Uint8Array.from(out);
}

// ================================================================ the designer (steps 3 and 4) ===
// A DESIGN (docs/LAYOUT-TOOL.md "The design format, version 2"): an engine's groups -> ordered screens, each either
// CLASSIC (one of today's kinds, lanes of four cells bound to parameters by id) or FREE (placed elements on a 4 px grid
// under the shared header, and lanes of up to four bound elements = KNOB 1..4, SELECT between lanes). Pure code: the
// page and the tests share it. Rendering goes through the module (wrap above): classic screens by lay_load + the real
// editor, free screens by the preview-only draw API.
export const DESIGN_VERSION = 2;
export const GRID = 4;
export const HEADER_H = 28;                  // the title line: rows 0 .. 27 are the header, the design area is below
export const MAX_LANES = 8, MAX_ELEMENTS = 24, LANE_SLOTS = 4;   // the firmware's cell[8][4] budget
export const HEADER_RIGHT_MAX = 112;         // the header's right text (the page's name, 11 px bold) at most this wide
// cr_screen.h CR_COL_*: the named colours and the palette's tokens (the only colours a design may use)
export const COLOURS = { white: 1, red: 2, blue: 3, yellow: 4, orange: 5, green: 6, grey: 7, black: 8, bg: 9, surf: 10,
                         text: 11, theme: 12, accent: 13, mid: 14, dim: 15, line: 16 };
export const KNOB_COL = [COLOURS.blue, COLOURS.orange, COLOURS.white, COLOURS.green];
// cr_screen.h CR_G_* in order
export const GLYPHS = ["none", "knob", "bar", "wave", "saw", "square", "steps", "dots", "morph", "noise", "room", "moon",
  "echoes", "lfo", "clip", "mix", "gate", "range", "arrow", "shift", "harm", "detune", "lfowave", "ratio", "tail", "pre",
  "cut", "width", "spread", "sync", "ping"];
export const BANDS = ["none", "env", "filter", "dx", "cz", "algo", "ade2", "wave"];     // CR_W_* (wave: GRAIN's buffers)
const PLAN_BAND = { none: 0, env: 1, filter: 2, algo: 4, ade2: 5, wave: 6 };            // ENG_B_*
export const ELEMENT_TYPES = ["frame", "label", "knob", "switch", "led", "bar", "glyph", "band"];
export const BINDABLE = new Set(["knob", "switch", "led", "bar", "glyph"]);
// an led's small waveform: the glyph and (lfowave) its wave
export const LED_GLYPHS = { saw: [4, 0], square: [5, 0], triangle: [22, 0], sine: [3, 0], noise: [9, 0] };
export const ELEMENT_SIZE = { frame: [112, 96], label: [48, 12], knob: [56, 64], switch: [56, 64], led: [52, 12],
                              bar: [56, 28], glyph: [40, 24], band: [240, 96] };
// a design's element style ("style": absent = "flat", the editor's look; "p5" = the Prophet-5's panel: framed sections
// titled in the border, knobs with a 0..10 scale, LED buttons, lever switches; docs/LAYOUT-TOOL.md "The p5 style")
export const STYLES = ["flat", "p5"];
const P5_SIZE = { knob: [56, 64], switch: [56, 64], led: [40, 32] };
export const P5_LED2_SIZE = [44, 32];   // a p5 led of two LEDs ("leds": 2)
export const elementSize = (type, style) => (style === "p5" && P5_SIZE[type]) || ELEMENT_SIZE[type];
export const KNOB_SIZES = { flat: [24, 28, 32, 36, 40], p5: [28, 32, 36, 40, 44] };
export const knobSize = (e, style) => e.size || (style === "p5" ? 36 : 32);
export const CLASSIC_KINDS = ["edit8", "stack"];
// a classic cell's look (core.h ENG_C_*, in its order; the catalog's cell "style"; tools/gen_layout.py CELL_STYLES): the
// editor's rule .. and the Prophet-5's controls drawn in the cell (cr_screen.h CR_G_P5*, docs/LAYOUT-TOOL.md "Cell styles")
export const CELL_STYLES = ["rule", "big", "ratio", "harm", "detune", "num", "val", "text", "knob", "bar", "waveph", "p5knob",
                            "p5led", "p5led-saw", "p5led-tri", "p5led-sqr", "p5led2", "p5switch"];
export const P5_CELL_STYLES = CELL_STYLES.filter((x) => x.startsWith("p5"));   // the designer's choice (beside the rule)
// a sketch glyph's cell style ("sketch:<name>", web/fm1sketch.js): packed as core.h ENG_C_SKETCH (the preview's blank
// glyph box, the page draws the sketch); never built
export const SKETCH_STYLE = CELL_STYLES.length;
export const sketchUnbuilt = (name) => `not buildable yet: a sketch glyph (${name}); port it into the firmware to build`;

// ------------------------------------------------------------- parameters ---
const cap = (s) => (s ? s[0] + s.slice(1).toLowerCase() : "");
// every parameter of the engine a design can bind: its stable id ("<page title>:<short>", "part:<P_ id>"), its
// built-in address (page, col; page 255 = a part parameter), the descriptor, the label today's editor shows, and the
// group / screen it is on today
export function paramList(cat) {
  const out = [], seen = new Map(), today = new Map();
  for (const t of tiles(cat))
    for (const c of t.cells) {
      const k = c.bind === "deep" ? `d${c.page}.${c.col}` : c.bind === "track" ? `t${c.param}` : null;
      if (k && !today.has(k)) today.set(k, { label: c.label, where: tileLabel(t), group: t.groupName, cell: c });
    }
  const uniq = (id) => { const n = (seen.get(id) || 0) + 1; seen.set(id, n); return n > 1 ? `${id}#${n}` : id; };
  for (const p of cat.pages || [])
    p.cols.forEach((c, i) => {
      if (!c) return;
      const t = today.get(`d${p.page}.${i}`);
      out.push({ id: uniq(`${p.title}:${c.short}`), page: p.page, col: i, pageTitle: p.title, short: c.short,
                 label: t ? t.label : cap(c.short), fmt: c.fmt, min: c.min, max: c.max, def: c.def, names: c.names,
                 unit: c.unit, where: t ? t.where : "", group: t ? t.group : "", part: false });
    });
  for (const [k, t] of today)
    if (k[0] === "t") {
      const c = t.cell;
      out.push({ id: uniq(`part:${c.param}`), page: 255, col: c.param, pageTitle: t.group, short: c.short,
                 label: c.label, fmt: c.fmt, min: c.min, max: c.max, def: c.def, names: c.names, unit: c.unit,
                 where: t.where, group: t.group, part: true });
    }
  return out;
}
export const paramIndex = (params) => new Map(params.map((p) => [p.id, p]));
// what a parameter is on the original instrument when its id does not say (the designer's palette: its search and
// its second line only)
export const PARAM_HINTS = { "LFO:NOISE": "wheel-mod source mix" };
const addrKey = (page, col) => `${page}.${col}`;

// ------------------------------------------------------------ new designs ---
const HEADER = { kind: "title-line", note: "the editor's title line: the sound's name left, the page's name right (fixed)" };
// today's layout as a design: every screen classic, as it is (the "start from today" of the designer)
export function designFromCatalog(cat, name = "today") {
  const params = paramList(cat), byAddr = new Map(params.map((p) => [p.part ? `t${p.col}` : addrKey(p.page, p.col), p.id]));
  const cell = (c) => c.bind === "deep" ? { param: byAddr.get(addrKey(c.page, c.col)) } :
                      c.bind === "track" ? { param: byAddr.get(`t${c.param}`) } :
                      c.bind === "text" ? { fixed: "text", text: c.text } : null;
  return {
    format: FORMAT, version: DESIGN_VERSION, kind: "design", name,
    engine: { index: cat.engine.index, name: cat.engine.name }, params_hash: cat.params_hash, header: { ...HEADER },
    groups: cat.groups.map((g) => ({
      group: g.group, name: g.name,
      screens: g.screens.map((s) => classicFromToday(s, cell)),
    })),
  };
}
function classicFromToday(s, cell) {
  return { type: "classic", kind: s.kind, band: s.band, title: s.lanes[0] ? s.lanes[0].title : "",
           lanes: s.lanes.map((l) => ({ title: l.title, cells: l.cells.map(cell) })) };
}
// a copy of today's screen k of group g (the "start from today's page")
export function classicScreenFromToday(cat, group, k) {
  const d = designFromCatalog(cat);
  const g = d.groups.find((x) => x.group === group);
  return JSON.parse(JSON.stringify(g.screens[k]));
}
export function newClassicScreen(kind = "edit8", title = "NEW") {
  const n = kind === "stack" ? 4 : 2;
  return { type: "classic", kind, band: "none", title,
           lanes: Array.from({ length: n }, () => ({ title, cells: [null, null, null, null] })) };
}
export function newFreeScreen(title = "NEW") { return { type: "free", title, elements: [], lanes: [] }; }
// a new element of a type at (x, y), snapped; id unique in the screen; its size the design's style's
export function newElement(screen, type, x, y, param = null, style = "flat") {
  const [w, h] = elementSize(type, style);
  let n = screen.elements.length + 1;
  while (screen.elements.some((e) => e.id === `e${n}`)) n++;
  const e = { id: `e${n}`, type, x: snap(type === "band" ? 0 : x), y: snap(type === "band" ? 24 : y), w, h };
  if (BINDABLE.has(type)) e.param = param;
  if (type === "frame" || type === "label") e.text = type === "frame" ? "SECTION" : "Label";
  if (type === "switch") e.positions = 2;
  if (type === "led") e.glyph = null;
  if (type === "knob") e.size = style === "p5" ? 36 : 32;
  if (type === "glyph") e.glyph = "wave";
  if (type === "band") e.band = "env";
  return e;
}
export const snap = (v) => Math.round(v / GRID) * GRID;
// copy and paste between screens: elements (copies) onto a screen at their own places, with fresh ids, in no lane; an
// element that would land exactly on an identical box moves 8 px right and down (again, until free). -> the new elements
export function pasteElements(screen, clip) {
  const made = [];
  let n = screen.elements.length + 1;
  for (const o of clip) {
    const e = JSON.parse(JSON.stringify(o));
    while (screen.elements.some((x) => x.id === `e${n}`)) n++;
    e.id = `e${n++}`;
    for (let k = 0; k < 30 && screen.elements.some((x) => x.x === e.x && x.y === e.y && x.w === e.w && x.h === e.h); k++) {
      e.x = Math.min(e.x + 8, SCREEN - e.w);
      e.y = Math.min(e.y + 8, SCREEN - e.h);
    }
    screen.elements.push(e);
    made.push(e);
  }
  return made;
}

// ---------------------------------------------------------- the design file ---
export const formatDesign = (d) => formatLayout(d);
export const designFileName = (d) => `${layoutFileName(d.engine.name).replace(/\.json$/, "")}-${layoutFileName(d.name || "design")}`;
// the groups a built design lays out as free screens (tools/gen_layout.py FREE_GROUPS); FX and MIX are the platform's
export const FREE_GROUPS = ["OSC", "FILT", "ENV", "LFO", "MOD"];
const latin1ok = (s) => typeof s === "string" && [...s].every((ch) => ch.codePointAt(0) < 256 && ch !== "\0");
// is a design well formed, and for this catalog? -> {errors: [..], warnings: [..]} (texts). opts.build: the firmware's
// rules too (tools/gen_layout.py's, word for word: the page's "not buildable" reasons; tests/gen_layout_test.py runs
// both): classic screens of OSC .. MOD as the screen plan has them (edit8 of one or two lanes, a plan band, no part
// parameter on an engine page, no sketch glyph, not beside free screens in a group); free screens: the 4 px grid and
// the area under the title line as errors, the budgets, a bound element in exactly one lane, a pair's halves touching,
// no band element; a parameter once, the p5 style only, FX and MIX as today, Latin-1 texts
export function validateDesign(d, cat, opts = {}) {
  const errors = [], warnings = [], build = !!opts.build;
  const err = (t) => errors.push(t), warn = (t) => warnings.push(t);
  if (!d || typeof d !== "object") return { errors: ["not a JSON object"], warnings };
  if (d.format !== FORMAT) err(`not a ${FORMAT} file`);
  if (d.version !== DESIGN_VERSION) err(`format version ${d.version}: a design is version ${DESIGN_VERSION}`);
  if (d.kind !== "design") err(`"kind" must be "design"`);
  if (!d.engine || d.engine.index !== cat.engine.index) err(`engine ${(d.engine || {}).name}: this catalog is ${cat.engine.name}`);
  if (d.params_hash !== cat.params_hash) err(`made for other parameters (params_hash ${d.params_hash}, the firmware's ${cat.params_hash})`);
  if (d.style !== undefined && d.style !== null && !STYLES.includes(d.style)) err(`style "${d.style}" (${STYLES.join(", ")}; none = flat)`);
  else if (build && d.style !== "p5") err(`style "${d.style || "flat"}": the firmware draws the p5 style only (a built design is "style": "p5")`);
  if (!d.header || d.header.kind !== "title-line") err(`"header" must be {"kind": "title-line"}: the shared header is the editor's title line`);
  if (!Array.isArray(d.groups)) { err(`"groups" must be a list`); return { errors, warnings }; }
  const pidx = paramIndex(paramList(cat)), gnames = new Set(cat.groups.map((g) => g.name));
  const today = build ? new Map(designFromCatalog(cat).groups.map((g) => [g.name, g.screens.map((s) => s.lanes.map((l) => l.cells.map((c) => (c && c.param) || null)))])) : null;
  const seenG = new Set(), used = new Map();
  const use = (p, where) => { if (!used.has(p)) used.set(p, []); used.get(p).push(where); };
  d.groups.forEach((g, gi) => {
    const where = `group ${gi + 1} (${g && g.name})`;
    if (!g || !gnames.has(g.name)) { err(`${where}: not a group of ${cat.engine.name} (${[...gnames].join(" ")})`); return; }
    if (seenG.has(g.name)) err(`${where}: the group twice`);
    seenG.add(g.name);
    if (!Array.isArray(g.screens)) { err(`${where}: "screens" must be a list`); return; }
    const freeGroup = FREE_GROUPS.includes(g.name);
    g.screens.forEach((s, si) => {
      const at = `${g.name} screen ${si + 1}`;
      if (!s || (s.type !== "classic" && s.type !== "free")) { err(`${at}: "type" must be "classic" or "free"`); return; }
      if (typeof s.title !== "string") err(`${at}: "title" must be a text`);
      else if (build && (!latin1ok(s.title) || [...s.title].length > 15)) err(`${at}: the title "${s.title}" is longer than 15 characters or not Latin-1`);
      if (JSON.stringify(s).includes('"key"')) err(`${at}: nothing may be bound to a key (rule 2: the keys always play)`);
      if (s.type === "classic") {
        if (!CLASSIC_KINDS.includes(s.kind)) err(`${at}: kind "${s.kind}" (${CLASSIC_KINDS.join(", ")})`);
        if (!BANDS.includes(s.band)) err(`${at}: band "${s.band}" (${BANDS.join(", ")})`);
        const plan = build && freeGroup;            // (OSC .. MOD: the screen plan's rules, tools/gen_layout.py's)
        if (plan) {
          if (CLASSIC_KINDS.includes(s.kind) && Array.isArray(s.lanes) && s.lanes.length && (s.kind !== "edit8" || s.lanes.length > 2))
            err(`${at}: a ${s.kind} of ${s.lanes.length} lanes: the screen plan has edit8 screens of one or two lanes`);
          if (BANDS.includes(s.band) && !(s.band in PLAN_BAND)) err(`${at}: band "${s.band}": not a band of the screen plan (${Object.keys(PLAN_BAND).join(", ")})`);
          if (g.screens.some((x) => x && x.type === "free"))
            err(`${at}: a classic screen beside free screens in ${g.name}: the unit's editor shows the group's free screens`);
        }
        if (!Array.isArray(s.lanes) || !s.lanes.length || s.lanes.length > MAX_LANES) { err(`${at}: 1 .. ${MAX_LANES} lanes`); return; }
        let good = true;
        s.lanes.forEach((l, li) => {
          if (!l || !Array.isArray(l.cells) || l.cells.length !== LANE_SLOTS) { err(`${at} lane ${li + 1}: four cells (KNOB 1..4; null = empty)`); good = false; return; }
          if (plan && l.title !== undefined && l.title !== null && !latin1ok(l.title)) err(`${at} lane ${li + 1}: its title is a Latin-1 text`);
          l.cells.forEach((c, ci) => {
            if (c === null) return;
            if (c.fixed) return;
            if (!pidx.has(c.param)) { err(`${at} lane ${li + 1} knob ${ci + 1}: no parameter "${c.param}"`); good = false; }
            else {
              use(c.param, at);
              if (plan && pidx.get(c.param).part) err(`${at} lane ${li + 1} knob ${ci + 1}: ${c.param}: a part parameter on an engine page`);
            }
            if (c.label !== undefined && c.label !== null && typeof c.label !== "string") err(`${at} lane ${li + 1} knob ${ci + 1}: a label is a text`);
            else if (plan && typeof c.label === "string" && !latin1ok(c.label)) err(`${at} lane ${li + 1} knob ${ci + 1}: its label is a Latin-1 text`);
            if (isSketchStyle(c.style)) {           // a sketch glyph: the page draws it; the firmware cannot build it
              const sk = parseSketch(c.style);
              if (!sk) err(`${at} lane ${li + 1} knob ${ci + 1}: style "${c.style}" (the sketch glyphs: ${SKETCH_NAMES.join(", ")}; a wave -saw -tri -sqr)`);
              else if (build) err(`${at} lane ${li + 1} knob ${ci + 1}: ${sketchUnbuilt(sk.name)}`);
            } else if (c.style !== undefined && c.style !== null && !CELL_STYLES.includes(c.style)) err(`${at} lane ${li + 1} knob ${ci + 1}: style "${c.style}" (${CELL_STYLES.join(", ")})`);
          });
        });
        if (build && good && !freeGroup) {
          const t = today.get(g.name) || [];
          const mine = s.lanes.map((l) => l.cells.map((c) => (c && c.label ? "label" : (c && c.param) || null)));
          if (si >= t.length || JSON.stringify(mine) !== JSON.stringify(t[si])) err(`${at}: today's platform screen: its cells cannot be moved by a design yet`);
        }
        return;
      }
      if (build && !freeGroup) err(`${at}: a free screen in ${g.name}: FX and MIX are the platform's`);
      if (!Array.isArray(s.elements)) { err(`${at}: "elements" must be a list`); return; }
      if (!Array.isArray(s.lanes)) { err(`${at}: "lanes" must be a list`); return; }
      const ids = new Map();
      s.elements.forEach((e, ei) => {
        const et = `${at} element ${ei + 1} (${e && e.id})`;
        if (!e || typeof e.id !== "string" || !e.id) { err(`${et}: an "id" text`); return; }
        if (ids.has(e.id)) err(`${et}: the id twice`);
        ids.set(e.id, e);
        if (!ELEMENT_TYPES.includes(e.type)) { err(`${et}: type "${e.type}" (${ELEMENT_TYPES.join(", ")})`); return; }
        for (const k of ["x", "y", "w", "h"]) if (!Number.isInteger(e[k]) || e[k] < 0) err(`${et}: ${k} a whole number >= 0`);
        const boxok = ["x", "y", "w", "h"].every((k) => Number.isInteger(e[k]));
        if (boxok && ["x", "y", "w", "h"].some((k) => e[k] % GRID)) (build ? err : warn)(`${et}: not on the ${GRID} px grid`);
        if (build && boxok && (e.y < HEADER_H || e.x + e.w > SCREEN || e.y + e.h > SCREEN || !e.w || !e.h))
          err(`${et}: outside the design area (under the header, rows ${HEADER_H} .. 239)`);
        if (BINDABLE.has(e.type) && e.param !== null && e.param !== undefined) {
          if (!pidx.has(e.param)) err(`${et}: no parameter "${e.param}"`);
          else use(e.param, `${at} ${e.id}`);
        } else if (build && BINDABLE.has(e.type) && e.type !== "glyph") err(`${et}: a ${e.type} bound to nothing`);
        if (e.type === "switch" && ![2, 3].includes(e.positions)) err(`${et}: a switch has 2 or 3 positions`);
        if (e.type === "led" && e.labelAt !== undefined && e.labelAt !== "above" && e.labelAt !== "below") err(`${et}: "labelAt" is "above" or "below"`);
        if (e.type === "frame" && e.r !== undefined && !(Number.isInteger(e.r) && e.r >= 0 && e.r <= 12)) err(`${et}: a frame's corner radius "r" is 0 .. 12`);
        if (e.type === "led" && e.leds !== undefined && ![1, 2].includes(e.leds)) err(`${et}: "leds" is 1 or 2`);
        if (e.type === "led" && e.leds === 2) {
          const lp = pidx.get(e.param);
          if (lp && lp.max - lp.min !== 1) err(`${et}: two LEDs show a parameter of two values (${e.param}: ${lp.min} .. ${lp.max})`);
          if (e.glyph) err(`${et}: two LEDs have their names above them, no glyph`);
        }
        if (e.type === "led" && e.pair !== undefined) {
          if (e.pair !== "left" && e.pair !== "right") err(`${et}: "pair" is "left" or "right"`);
          else if (!build || boxok) {
            const o = s.elements.find((x) => x && x.type === "led" && x.pair === (e.pair === "left" ? "right" : "left") &&
              x.y === e.y && x.h === e.h && (e.pair === "left" ? e.x + e.w === x.x : x.x + x.w === e.x));
            if (!o) (build ? err : warn)(`${et}: a ${e.pair} half with no ${e.pair === "left" ? "right" : "left"} half touching it (same y and h): its cap stands alone`);
          }
        }
        if (e.type === "led" && e.glyph && !LED_GLYPHS[e.glyph]) err(`${et}: led glyph "${e.glyph}" (${Object.keys(LED_GLYPHS).join(", ")})`);
        if (e.type === "glyph" && !GLYPHS.includes(e.glyph)) err(`${et}: glyph "${e.glyph}"`);
        if (e.type === "band") {
          if (!BANDS.includes(e.band)) err(`${et}: band "${e.band}"`);
          if (build) err(`${et}: a band element: not built (the bands are the classic screens')`);
        }
        if (e.colour !== undefined && e.colour !== "auto" && !(e.colour in COLOURS)) err(`${et}: colour "${e.colour}" is not a palette token`);
        if (build) {
          if (e.type === "knob" && "size" in e && !(Number.isInteger(e.size) && e.size >= 28 && e.size <= 44)) err(`${et}: a knob's dial is 28 .. 44`);
          if (e.type === "label" && "size" in e && !(Number.isInteger(e.size) && e.size >= 6 && e.size <= 32)) err(`${et}: a label's size is 6 .. 32 px`);
          if (e.type === "label" && !["left", "centre", "right"].includes("align" in e ? e.align : "left")) err(`${et}: "align" is "left", "centre" or "right"`);
          if ("names" in e && !(Array.isArray(e.names) && e.names.every((n) => typeof n === "string" && n))) err(`${et}: "names" is a list of texts`);
          for (const k of ["label", "text"])
            if (e[k] !== undefined && e[k] !== null && !latin1ok(e[k])) err(`${et}: its ${k} is a Latin-1 text`);
        }
      });
      if (build) {
        if (s.elements.length > MAX_ELEMENTS) err(`${at}: ${s.elements.length} elements: at most ${MAX_ELEMENTS}`);
        if (!s.lanes.length || s.lanes.length > MAX_LANES) err(`${at}: ${s.lanes.length} lanes: 1 .. ${MAX_LANES}`);
      }
      const seen = new Map();
      s.lanes.forEach((l, li) => {
        if (!Array.isArray(l) || (build && l.length !== LANE_SLOTS)) { err(`${at} lane ${li + 1}: a list of element ids` + (build ? " (four: KNOB 1..4; null = empty)" : "")); return; }
        for (const id of l) {
          if (id === null) continue;
          if (!ids.has(id)) err(`${at} lane ${li + 1}: no element "${id}"`);
          else if (build) {
            const e = ids.get(id);
            if (!(BINDABLE.has(e.type) && e.param !== null && e.param !== undefined)) err(`${at} lane ${li + 1}: ${id} binds no parameter`);
            if (seen.has(id)) err(`${at}: ${id} is in two lanes`);
            seen.set(id, li);
          }
        }
      });
      if (build)
        for (const [id, e] of ids)
          if (BINDABLE.has(e.type) && e.param !== null && e.param !== undefined && !seen.has(id)) err(`${at}: ${id} (${e.param}) is in no lane: no knob reaches it`);
    });
  });
  if (build) {
    for (const n of gnames)
      if (FREE_GROUPS.includes(n) && !d.groups.some((g) => g && typeof g === "object" && g.name === n && Array.isArray(g.screens) && g.screens.length))
        err(`${n}: no screens (a built design lays out every group of OSC .. MOD)`);
    for (const [p, where] of used) if (where.length > 1) err(`${p} is bound twice in the design (${where.join(", ")}): a parameter once`);
  }
  return { errors, warnings };
}

// ------------------------------------------------------ classic: lay_load ---
// the classic screens of a group, with their index among them (lay_show's screen)
const classicOf = (g) => g.screens.map((s, i) => ({ s, i })).filter((x) => x.s.type === "classic");
const sameCells = (a, b) => JSON.stringify(a.map((c) => (c && (c.param || c.text)) || null)) ===
                            JSON.stringify(b.map((c) => (c && (c.param || c.text)) || null)) && a.every((c) => !c || !c.label);
// Pack a design's classic screens for lay_load. Two ways the firmware's own hook can show them:
//   "in-place": every group's classic screens have today's structure (the same number, kinds and lanes): the engine's
//       page tables are kept, with the design's parameter in each column and its labels (moves, swaps, relabels);
//   "plan": the structure changed (screens added, removed, reordered, moved between groups), or the catalog's screens
//       are a compiled layout's (cat.layout: P5's rows; its pages are the engine's own, its screens the layout's, so
//       "today's structure" is not the pages'): OSC .. MOD become a screen plan (core.h eng_screen_t: edit8, one or two
//       lanes a screen, a band, a title), one page per lane: what tools/gen_layout.py compiles (the same bytes, through
//       lay_compiled).
// What neither can express is listed in `unbuildable` and shown as today's ("not buildable yet").
// -> {bytes, mode, show: Map("gi.si" -> {group, screen}), unbuildable: [{gi, si, why}], notes: [..]}
export function packDesign(d, cat) {
  const params = paramIndex(paramList(cat));
  const today = new Map(cat.groups.map((g) => [g.name, g]));
  const unbuildable = [], notes = [], show = new Map();
  const groups = d.groups.map((g, gi) => ({ g, gi, t: today.get(g.name) })).filter((x) => x.t);
  // in place?
  let inPlace = true;
  for (const { g, t } of groups) {
    const cl = classicOf(g);
    if (!cl.length) continue;
    if (cl.length !== t.screens.length) { inPlace = false; break; }
    cl.forEach(({ s }, k) => {
      const ts = t.screens[k];
      if (s.kind !== ts.kind || s.lanes.length !== ts.lanes.length) inPlace = false;
    });
  }
  if (cat.layout && groups.some(({ g, t }) => t.group <= 4 && classicOf(g).length)) inPlace = false;
  const colOf = (c) => {
    if (!c || c.fixed) return null;
    const p = params.get(c.param);
    return p && !p.part ? { src: [p.page, p.col], label: c.label || "",
                            style: parseSketch(c.style) ? SKETCH_STYLE : Math.max(0, CELL_STYLES.indexOf(c.style || "rule")) } : null;
  };
  let pages = [], screens = [], sec = [];
  const mode = inPlace ? "in-place" : "plan";
  if (inPlace) {
    pages = cat.pages.map((p) => ({ title: p.title, cols: p.cols.map((c, i) => (c ? { src: [p.page, i], label: "" } : null)) }));
    for (const { g, gi, t } of groups) {
      classicOf(g).forEach(({ s, i }, k) => {
        const ts = t.screens[k];
        show.set(`${gi}.${i}`, { group: t.group, screen: k });
        const deepLanes = ts.type === "edit" || ts.type === "page" || ts.type === "stack";
        if (!deepLanes) {
          if (!s.lanes.every((l, li) => sameCells(l.cells, designFromCatalog(cat).groups.find((x) => x.name === g.name).screens[k].lanes[li].cells)))
            unbuildable.push({ gi, si: i, why: `today's ${ts.type} screen: its cells cannot be moved by a design yet` });
          return;
        }
        s.lanes.forEach((l, li) => {
          const pg = ts.pages[li];
          l.cells.forEach((c, ci) => {
            if (c && c.param && params.get(c.param) && params.get(c.param).part)
              unbuildable.push({ gi, si: i, why: `${c.param}: a part parameter on an engine page` });
            pages[pg].cols[ci] = colOf(c);
          });
        });
        if (s.band !== ts.band) notes.push(`${g.name} ${i + 1}: the band follows the page titles in place ("${ts.band}"); "${s.band}" needs the plan`);
      });
    }
    for (const t of cat.groups.filter((x) => x.group <= 4)) {
      const ps = t.screens.flatMap((s) => s.pages);
      if (ps.length) sec[t.group] = Math.min(...ps);
    }
  } else {
    for (const gno of [0, 1, 2, 3, 4]) {
      const t = cat.groups.find((x) => x.group === gno);
      if (!t || !t.screens.some((s) => s.pages.length)) continue;
      const dg = groups.find((x) => x.t.group === gno);
      sec[gno] = pages.length;
      const cl = dg ? classicOf(dg.g) : [];
      if (!cl.length) pages.push({ title: t.name, cols: [null, null, null, null] });   // (a group of free screens only)
      cl.forEach(({ s, i }, k) => {
        show.set(`${dg.gi}.${i}`, { group: gno, screen: k });
        if (s.kind !== "edit8" || s.lanes.length > 2)
          unbuildable.push({ gi: dg.gi, si: i, why: `a ${s.kind} of ${s.lanes.length} lanes in a changed group: the screen plan has edit8 screens of one or two lanes` });
        const lanes = s.lanes.slice(0, 2), pg = [];
        for (const l of lanes) {
          pg.push(pages.length);
          pages.push({ title: (l.title || s.title || t.name).slice(0, 15), cols: l.cells.map(colOf) });
        }
        if (!(s.band in PLAN_BAND)) notes.push(`${dg.g.name} ${i + 1}: band "${s.band}" is not a plan band`);
        screens.push({ sec: gno, pg: [pg[0], pg.length > 1 ? pg[1] : 255], band: PLAN_BAND[s.band] || 0, title: (s.title || "").slice(0, 15) });
      });
    }
    for (const { g, gi, t } of groups)
      if (t.group > 4)
        classicOf(g).forEach(({ s, i }, k) => {
          show.set(`${gi}.${i}`, { group: t.group, screen: k });
          const ts = designFromCatalog(cat).groups.find((x) => x.name === g.name).screens[k];
          if (!ts || !s.lanes.every((l, li) => ts.lanes[li] && sameCells(l.cells, ts.lanes[li].cells)))
            unbuildable.push({ gi, si: i, why: `today's platform screen: its cells cannot be moved by a design yet` });
        });
  }
  // the free screens of OSC .. MOD (packFree): their parameters by the built-in (page, col); a loaded table that is not
  // the built-in one gets a copy of the built-in pages after its own (fenced off by a section) for them
  let free = packFree(d, cat, 0), fence;
  const mixed = new Set();
  if (free) {
    const identity = mode === "in-place" && pages.length === cat.pages.length && pages.every((p, i) => p.cols.every((x, k) =>
      cat.pages[i].cols[k] ? !!x && x.src[0] === cat.pages[i].page && x.src[1] === k && !x.label : !x));
    if (!identity) {
      fence = pages.length;
      for (const p of cat.pages) pages.push({ title: p.title, cols: p.cols.map((c, i) => (c ? { src: [p.page, i], label: "" } : null)) });
      free = packFree(d, cat, fence);
    }
    d.groups.forEach((g, gi) => {
      if (![...free.show.keys()].some((k) => k.startsWith(`${gi}.`))) return;
      mixed.add(gi);
      g.screens.forEach((s, si) => {
        if (s.type === "classic")
          unbuildable.push({ gi, si, why: `a classic screen beside free screens in ${g.name}: the unit's editor shows the group's free screens` });
      });
    });
  }
  d.groups.forEach((g, gi) => g.screens.forEach((s, si) => {
    if (s.type === "free" && !(free && free.show.has(`${gi}.${si}`)))
      unbuildable.push({ gi, si, why: `a free screen in ${g.name}: the unit's editor has free screens in OSC .. MOD only (FX and MIX are the platform's)` });
  }));
  // the bytes ('L' 3 engine npages pages.. section[8] nscreens screens.., the free half; a column: srcpage srccol style
  // label, its style the cell's ENG_C_*: the plan's cell styles and, in place, cr_edit.c's ce_style_hook)
  const b = [76, 3, cat.engine.index, pages.length];
  const str = (s) => { const u = toLatin1((s || "").slice(0, 15)); b.push(u.length, ...u); };
  for (const p of pages) {
    str(p.title);
    for (let c = 0; c < 4; c++) {
      const x = p.cols[c];
      if (x) { b.push(x.src[0], x.src[1], x.style || 0); str(x.label); } else { b.push(255, 0, 0); str(""); }
    }
  }
  const secs = [0, 1, 2, 3, 4].map((g) => sec[g]).filter((v) => v !== undefined);
  if (fence !== undefined && secs.length < 5) notes.push("a group without pages: the free screens' copy of the pages is not fenced off");
  for (let i = 0; i < 8; i++) b.push(i < secs.length ? secs[i] : i === secs.length && fence !== undefined && secs.length === 5 ? fence : 255);
  b.push(screens.length);
  for (const s of screens) { b.push(s.sec, s.pg[0], s.pg[1], s.band); str(s.title); }
  const bytesClassic = Uint8Array.from(b);
  return { bytes: free ? Uint8Array.from([...b, ...free.bytes]) : bytesClassic, bytesClassic, free: free ? free.bytes : null,
           freeShow: free ? free.show : new Map(), mixed, mode, show, unbuildable, notes, pages: pages.length, screens: screens.length,
           freeScreens: free ? free.screens : 0 };
}

// ------------------------------------------------------- free: lay_load ---
// A design's free screens as the records tools/gen_layout.py compiles (firmware/src/cr_layout.h: an element 14 B, a
// screen 38 B, one string pool), resolved the same way (a frame's title in capitals and r 3 by default, a knob's dial
// 36, a label's 11 px, an led's label below for two LEDs, its icon's label only when written, the parameter's label
// when the element has none, value names after the label in the pool, frames first, page 0xFE unbound, 0xFF a part
// parameter): for a built design byte for byte the compiled table (web/test_layout.mjs compares it with lay_compiled).
// The firmware's editor and renderer then show them (CE_S_FREE, CR_K_FREE). page + offset: a loaded table that is not
// the built-in one (packDesign) carries a copy of the built-in pages from `offset` on.
// -> {bytes (the free half: nfs, nel, npool, the screens, the elements, the pool), show: Map("gi.si" -> {group, screen}),
//     screens, elements, pool} or null (no free screen in OSC .. MOD)
const LAY_PART = 255, LAY_NONE = 254, LAY_NOSTR = 0xffff, LAY_EMPTY = 255;
const LF_LEDS2 = 0x8, LF_BELOW = 0x40, LF_BOLD = 0x800, LF_NAMES = 0x1000;
export function packFree(d, cat, offset = 0) {
  const params = paramIndex(paramList(cat)), gno = new Map(cat.groups.map((g) => [g.name, g.group]));
  const pool = [], runs = new Map(), screens = [], elems = [], show = new Map();
  const add = (...ss) => {
    const run = [];
    for (const s of ss) run.push(...toLatin1(String(s)), 0);
    const k = run.join(",");
    if (!runs.has(k)) { runs.set(k, pool.length); pool.push(...run); }
    return runs.get(k);
  };
  const b8 = (v) => Math.max(0, Math.min(255, Number.isInteger(v) ? v : 0));
  const record = (e, p) => {
    const ty = e.type;
    let page = LAY_NONE, col = 0, size = 0, r = 0, flags = 0, names = [];
    if (p) { page = p.part ? LAY_PART : p.page + offset; col = p.col; }
    let lab = e.label !== undefined && e.label !== null ? e.label : p ? p.label : "";
    if (ty === "frame") { lab = String(e.text || "").toUpperCase(); r = e.r ?? 3; }
    else if (ty === "label") {
      lab = e.text || "";
      size = e.size || 11;
      flags |= ({ centre: 1, right: 2 }[e.align] || 0) << 7;
      flags |= e.bold === false ? 0 : LF_BOLD;
    } else if (ty === "knob") { size = e.size || 36; names = p && p.names ? [...p.names] : []; }
    else if (ty === "switch") {
      const n = e.positions === 3 ? 3 : 2;
      flags |= n << 9;
      names = Array.from({ length: n }, (_, i) => switchName(e, p, n, i));
    } else if (ty === "led") {
      const two = e.leds === 2, icon = !two && e.glyph;
      if (icon) {
        const k = Object.keys(LED_GLYPHS).indexOf(icon);
        if (k >= 0) flags |= k + 1;
        lab = e.label || "";
      }
      if (two) { flags |= LF_LEDS2; names = [0, 1].map((i) => switchName(e, p, 2, i).toUpperCase()); }
      flags |= ({ left: 1, right: 2 }[e.pair] || 0) << 4;
      if ("labelAt" in e ? e.labelAt === "below" : two) flags |= LF_BELOW;
    } else if (ty === "glyph") { size = Math.max(0, GLYPHS.indexOf(e.glyph)); lab = ""; }
    let label;
    if (names.length) { flags |= LF_NAMES; label = add(lab, ...names.map((x) => String(x) || "?"), ""); }
    else label = lab ? add(lab) : LAY_NOSTR;
    return [ELEMENT_TYPES.indexOf(ty), b8(e.x), b8(e.y), b8(e.w), b8(e.h), page & 255, col & 255, size & 255, b8(r),
            COLOURS[e.colour] || 0, flags & 255, flags >> 8, label & 255, label >> 8];
  };
  d.groups.forEach((g, gi) => {
    if (!g || !FREE_GROUPS.includes(g.name) || !gno.has(g.name) || !Array.isArray(g.screens)) return;
    let k = 0;
    g.screens.forEach((s, si) => {
      if (!s || s.type !== "free" || !Array.isArray(s.elements)) return;
      const els = s.elements.filter((e) => e && ELEMENT_TYPES.includes(e.type))
        .map((e, i) => ({ e, i })).sort((a, b) => (a.e.type === "frame" ? 0 : 1) - (b.e.type === "frame" ? 0 : 1) || a.i - b.i).map((x) => x.e);
      if (screens.length >= 64 || elems.length + els.length > 255) return;
      const first = elems.length, ix = new Map();
      els.forEach((e, i) => ix.set(e.id, i));
      for (const e of els) elems.push(record(e, BINDABLE.has(e.type) && e.param ? params.get(e.param) : null));
      const title = add(String(s.title || ""));
      const lanes = (Array.isArray(s.lanes) ? s.lanes : []).slice(0, MAX_LANES)
        .map((l) => [0, 1, 2, 3].map((i) => (Array.isArray(l) && l[i] !== null && l[i] !== undefined && ix.has(l[i]) ? ix.get(l[i]) : LAY_EMPTY)));
      screens.push({ group: gno.get(g.name), first, count: els.length, nlanes: lanes.length, title, lanes });
      show.set(`${gi}.${si}`, { group: gno.get(g.name), screen: k++ });
    });
  });
  if (!screens.length) return null;
  const b = [screens.length, elems.length & 255, elems.length >> 8, pool.length & 255, pool.length >> 8];
  for (const s of screens) {
    b.push(s.group, s.first, s.count, s.nlanes, s.title & 255, s.title >> 8);
    for (let l = 0; l < MAX_LANES; l++) b.push(...(l < s.lanes.length ? s.lanes[l] : [LAY_EMPTY, LAY_EMPTY, LAY_EMPTY, LAY_EMPTY]));
  }
  for (const e of elems) b.push(...e);
  b.push(...pool);
  return { bytes: Uint8Array.from(b), show, screens: screens.length, elements: elems.length, pool: pool.length };
}

// --------------------------------------------------------- free: lanes ---
// where each element is on the knobs: id -> {lane, slot}; and the lane problems
export function laneMap(screen) {
  const at = new Map(), twice = [];
  (screen.lanes || []).forEach((l, li) => l.forEach((id, k) => {
    if (id === null) return;
    if (at.has(id)) twice.push(id); else at.set(id, { lane: li, slot: k });
  }));
  return { at, twice };
}
// a switch's position i of n: the element's names, else the parameter's value names, else Off / On (Lo Mid Hi)
function switchName(e, p, n, i) {
  const v = p ? p.min + Math.round(i * (p.max - p.min) / (n - 1)) : i;
  return String((e.names && e.names[i]) || (p && p.names && p.names[v - (p.fmt === "enum" ? p.min : 0)]) || (n === 2 ? ["Off", "On"][i] : ["Lo", "Mid", "Hi"][i]));
}

// ---------------------------------------------------------- design checks ---
const boxOf = (e) => [e.x, e.y, e.x + e.w, e.y + e.h];
const meet = (a, b) => a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
// the design's own checks of a screen (beside the firmware's text lint): [{kind, text, ids?}]; kind "order" is a
// warning (the page shows it yellow; tools/gen_layout.py prints it and builds)
// opts.measure(text, px, bold): the header's right text width (the module's lay_text_w)
export function checkScreen(screen, params, opts = {}) {
  const f = [], add = (kind, text, ids) => f.push({ kind, text, ids });
  if (opts.measure && opts.measure(screen.title || "", 11, true) > HEADER_RIGHT_MAX)
    add("title", `the title "${screen.title}" is wider than the header allows (${HEADER_RIGHT_MAX} px)`);
  if (screen.type === "classic") {
    if ((screen.title || "").length > 15) add("title", `the title "${screen.title}" is longer than 15 characters`);
    const seen = new Map();
    screen.lanes.forEach((l, li) => l.cells.forEach((c) => {
      if (c && c.param) { if (seen.has(c.param)) add("twice", `${c.param} is on this screen twice`); seen.set(c.param, li); }
    }));
    if (screen.lanes.length > MAX_LANES) add("budget", `${screen.lanes.length} lanes: at most ${MAX_LANES}`);
    if (screen.kind === "edit8" && screen.lanes.length > 2) add("budget", `an edit8 screen shows two lanes`);
    screen.lanes.forEach((l, li) => l.cells.forEach((c, ci) => {   // a sketch glyph: drawn by the page, not by a build
      const sk = c && parseSketch(c.style);
      if (sk) add("sketch", `lane ${li + 1} knob ${ci + 1}: ${sketchUnbuilt(sk.name)}${screen.kind === "edit8" ? "" : " (drawn on edit8 screens only)"}`);
    }));
    return f;
  }
  const els = screen.elements, { at, twice } = laneMap(screen);
  if (els.length > MAX_ELEMENTS) add("budget", `${els.length} elements: at most ${MAX_ELEMENTS}`);
  if (screen.lanes.length > MAX_LANES) add("budget", `${screen.lanes.length} lanes: at most ${MAX_LANES}`);
  screen.lanes.forEach((l, li) => { if (l.filter((x) => x !== null).length > LANE_SLOTS) add("lane", `lane ${li + 1} has ${l.length} elements: four knobs`); });
  for (const id of new Set(twice)) add("lane", `${id} is in two lanes`, [id]);
  // a warning: KNOB 1..4 follow the screen left to right (the bound slots' x never goes back)
  screen.lanes.forEach((l, li) => {
    const xs = l.map((id) => id !== null && els.find((e) => e.id === id)).filter((e) => e && Number.isFinite(e.x));
    if (xs.some((e, i) => i && e.x < xs[i - 1].x))
      add("order", `lane ${li + 1}: its slots are not left to right (KNOB 1..4 should follow the screen)`, xs.map((e) => e.id));
  });
  const used = new Map();
  for (const e of els) {
    if (BINDABLE.has(e.type) && e.param) {
      if (used.has(e.param)) add("twice", `${e.param} is bound twice (${used.get(e.param)}, ${e.id})`, [used.get(e.param), e.id]);
      used.set(e.param, e.id);
      if (!at.has(e.id)) add("lane", `${e.id} (${e.param}) is in no lane: no knob reaches it`, [e.id]);
      if (!params.has(e.param)) add("param", `${e.id}: no parameter ${e.param}`, [e.id]);
    }
    if (BINDABLE.has(e.type) && !e.param && e.type !== "glyph") add("param", `${e.id}: a ${e.type} bound to nothing`, [e.id]);
    if (e.y < HEADER_H || e.x < 0 || e.x + e.w > SCREEN || e.y + e.h > SCREEN) add("area", `${e.id} is outside the design area (under the header, rows ${HEADER_H} .. 239)`, [e.id]);
  }
  for (let i = 0; i < els.length; i++)
    for (let j = i + 1; j < els.length; j++) {
      const a = els[i], b = els[j];
      if (a.type === "frame" || b.type === "frame") continue;
      if (meet(boxOf(a), boxOf(b))) add("overlap", `${a.id} and ${b.id} overlap`, [a.id, b.id]);
    }
  return f;
}
// a parameter used on more than one screen of the design: [{param, where: [..]}]
export function usedTwice(d) {
  const at = new Map();
  d.groups.forEach((g) => g.screens.forEach((s, si) => {
    const ps = s.type === "classic" ? s.lanes.flatMap((l) => l.cells.filter((c) => c && c.param).map((c) => c.param))
                                    : s.elements.filter((e) => e.param).map((e) => e.param);
    for (const p of new Set(ps)) { if (!at.has(p)) at.set(p, []); at.get(p).push(`${g.name} ${si + 1}`); }
  }));
  return [...at].filter(([, w]) => w.length > 1).map(([param, where]) => ({ param, where }));
}
// the cell of a classic screen at (x, y), screen px (0..240), from the editor's state (lay_state: kind, band, lanes):
// {lane, k} or null. cr_draw.c's rows: edit8 with a band 124..180 and 184..240 (one lane: 124..224, full size),
// without 30..130 and 134..234; a stack's N rows share 41..239. Columns 60 px (KNOB 1 .. 4)
export function cellAt(state, x, y) {
  if (!state || x < 0 || x >= 240 || y < 0 || y >= 240) return null;
  const n = Math.max(1, state.lanes | 0), k = Math.floor(x / 60);
  let lane = -1;
  if (state.kind === "stack") lane = y >= 41 ? Math.floor((y - 41) / (198 / Math.min(n, 8))) : -1;
  else if (state.kind === "edit8") {
    const rows = state.band && state.band !== "none" ? (n === 1 ? [[124, 224]] : [[124, 180], [184, 240]]) : [[30, 130], [134, 234]];
    lane = rows.findIndex(([a, b]) => y >= a && y < b);
  }
  return lane >= 0 && lane < n ? { lane, k } : null;
}
// every screen x lane of a design, in order: [{gi, si, lane, lanes, type, group, title, key}]
export function designTiles(d) {
  const out = [];
  d.groups.forEach((g, gi) => g.screens.forEach((s, si) => {
    const n = Math.max(1, s.lanes.length);
    for (let l = 0; l < n; l++)
      out.push({ gi, si, lane: l, lanes: n, type: s.type, group: g.name, title: s.title, key: `${gi}.${si}.${l}` });
  }));
  return out;
}

// ------------------------------------------------------------ a session ---
// The designer's live preview of one design on the module: the design is packed (packDesign) and loaded (lay_load),
// and the firmware's own editor and renderer show it: classic screens through its pages, free screens through its
// CE_S_FREE / CR_K_FREE (the unit's look and knobs, pixel for pixel). The knobs and SELECT are the unit's.
//   const s = designSession(m, cat, design, preset); s.open(gi, si, lane); s.render() -> {rgba, lint, checks, ..}
export function designSession(m, cat, design, preset = 1) {
  const params = paramIndex(paramList(cat)), E = cat.engine.index;
  let pack = null, reasons = null, at = { gi: 0, si: 0, lane: 0 };
  const scr = () => design.groups[at.gi].screens[at.si];
  const load = (bytes) => {                  // (every open: another session may have loaded its own design since)
    const r = m.load(bytes);
    if (!r.ok) throw new Error(`lay_load: ${r.error}`);
  };
  // the firmware shows this screen as designed (a free screen of OSC .. MOD, or a classic one)
  const shown = (gi, si) => !!(pack.freeShow.get(`${gi}.${si}`) || (design.groups[gi].screens[si] || {}).type === "classic");
  const self = {
    get at() { return { ...at }; },
    get pack() { return pack; },
    // the design changed: pack and load it again (the place is kept)
    reload() {
      pack = packDesign(design, cat);
      reasons = null;
      load(pack.bytes);
      return self.open(at.gi, at.si, at.lane);
    },
    // why the unit cannot show this screen as designed (today's or another is shown, or the generator would refuse
    // it: validateDesign's build rules, tools/gen_layout.py's words), else null
    unbuildable(gi = at.gi, si = at.si) {
      const sc = (design.groups[gi] || { screens: [] }).screens[si], sk = sc && sc.type === "classic" &&
        sc.lanes.flatMap((l) => l.cells).map((c) => c && parseSketch(c.style)).find((x) => x);
      if (sk) return `${design.groups[gi].name} screen ${si + 1}: ${sketchUnbuilt(sk.name)}`;   // (first: the badge says so)
      const u = pack && pack.unbuildable.find((x) => x.gi === gi && x.si === si);
      if (u) return u.why;
      if (!reasons) reasons = validateDesign(design, cat, { build: true }).errors;
      const g = design.groups[gi], pre = g ? `${g.name} screen ${si + 1}` : "";
      const mine = reasons.find((t) => t.startsWith('style "') || [...t.matchAll(new RegExp(pre.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "g"))]
        .some((x) => /^([ :,)]|$)/.test(t.slice(x.index + pre.length))));
      return mine || null;
    },
    open(gi, si, lane = 0) {
      if (!pack) pack = packDesign(design, cat);
      at = { gi, si, lane };
      const s = scr();
      if (!s) return self;
      at.lane = Math.max(0, Math.min(lane, Math.max(1, s.lanes.length) - 1));
      const key = `${gi}.${si}`, fp = pack.freeShow.get(key);
      if (fp) {
        load(pack.bytes);
        m.show(E, preset, fp.group, fp.screen, at.lane);
      } else if (s.type === "classic") {
        const p = pack.show.get(key);
        load(pack.mixed.has(gi) ? pack.bytesClassic : pack.bytes);
        m.show(E, preset, p ? p.group : design.groups[gi].group, p ? p.screen : 0, Math.min(at.lane, s.lanes.length - 1));
      } else {                                   // (a free screen the editor has no place for: the group's first screen)
        load(pack.bytes);
        m.show(E, preset, design.groups[gi].group, 0, 0);
      }
      return self;
    },
    // KNOB k (1..4) n detents (the unit's editor: its step, the hot mark)
    knob(k, n) { if (shown(at.gi, at.si)) m.knob(k, n); },
    // the 800 ms of a hot cell's mark have passed
    cool() { m.step(800); },
    // SELECT n detents: the lane, on across the group's screens (in the design's order)
    select(n) {
      if (!n) return;
      let { gi, si, lane } = at;
      const g = design.groups[gi];
      for (let i = 0; i < Math.abs(n); i++) {
        const d = Math.sign(n), cur = g.screens[si], nl = Math.max(1, cur.lanes.length);
        if (lane + d >= 0 && lane + d < nl) lane += d;
        else if (si + d >= 0 && si + d < g.screens.length) { si += d; lane = d > 0 ? 0 : Math.max(1, g.screens[si].lanes.length) - 1; }
      }
      if (si === at.si && shown(gi, si)) { m.select(lane - at.lane); at.lane = lane; return; }
      self.open(gi, si, lane);
    },
    // the screen now: {rgba, lint (the firmware's text lint), checks (the design's), free, unbuildable, state}
    render() {
      const s = scr(), rgba = m.render(), st = m.state();
      if (shown(at.gi, at.si) && st.lane !== at.lane && st.lane < Math.max(1, s.lanes.length)) at.lane = st.lane;
      if (s.type === "classic" && shown(at.gi, at.si) && st.kind === "edit8") {   // the sketch glyphs in their boxes
        const cells = [];
        s.lanes.slice(0, 2).forEach((l, row) => l.cells.forEach((c, k) => {
          if (!c || !parseSketch(c.style)) return;
          const p = params.get(c.param);
          cells.push({ row, k, style: c.style, names: p && p.names && p.names.length >= 2 ? p.names.slice(0, 2) : null });
        }));
        if (cells.length) drawSketches(rgba, st, cells);
      }
      return { rgba, lint: m.lint(), checks: checkScreen(s, params, { measure: (t, px, b) => m.textW(t, px, b) }), free: s.type === "free",
               unbuildable: self.unbuildable(), state: st };
    },
    // the values of the four knobs of the active lane: [{label, value} | null]
    lane() {
      if (!shown(at.gi, at.si)) return [null, null, null, null];
      return m.state().cells.map((c) => (c.on ? { label: c.label, value: c.value } : null));
    },
  };
  return self;
}
