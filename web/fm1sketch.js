// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot FM-1 layout tool: sketch glyphs (docs/LAYOUT-TOOL.md "Sketch glyphs"). A sketch is a cell glyph drawn
// by the page itself, at the FM-1's own pixels, so a glyph design can be tried on real pages before anyone writes C.
// A classic cell's style "sketch:<name>" (or "sketch:<name>-saw" / "-tri" / "-sqr": an LED's wave): the firmware
// draws the cell (its label, value, row bars, the hot block; cr_edit.c ENG_C_SKETCH, preview only) with nothing in
// its glyph box, and designSession (web/fm1layout.js) draws the sketch there on the module's RGBA (drawSketches).
// Each registry entry: {name, kind: "knob" | "led" | "switch" | "led2", round, note, draw(p, box, state)}:
//   p      the pixel helpers (fill, dot, ring, disc, line, text: whole pixels, the mock-ups' own), clipped to the cell;
//   box    the cell's glyph box in screen pixels {x, y, w, h}: x the cell's left (60 wide), y and h the firmware's
//          (cr_draw.c cr_p_edit8: 34 high at row + 26, or 22 at row + 15 with two lanes under a band);
//   state  {pct (0..1), on, pos (0..2), names ([a, b] or null), icon ("saw" | "tri" | "sqr" | null), col (the row
//          colour [r, g, b]), active, small (the 22 px box), C (the editor's colours: bg surf text mid dim line white
//          red yellow black; lay_state's "ink")}.
// The glyphs are design/p5-glyph-mockups.html (round 1: K1 .. K6, B1 .. B5, S1 .. S3), -2 (K6a .. K6e, B1a .. B1d,
// S4 .. S6), -3 (R1 .. R3) and -5 (K6d, B1a and S5 as chosen), drawn as there: the glyph centred 22 px under the
// label's baseline (box.y + 12; the 22 px box: box.y + 10), so a sketch may reach a few pixels over the box (the
// mock-ups' dials do; a knob in the 34 px box at box.y + 18, level with the buttons' LEDs, as the firmware's K6d since
// the owner asked); the page composites only the cell's column between its label and its value.
// To add one: a function here and a SK_REGISTRY line (its name lower case, a letter and a number, its note); a chosen
// sketch becomes a firmware glyph by porting its function into cr_draw.c (cr_p5glyph is K6d / B1a / S5 / R3's).

// a 1x bitmap face for the sketches' small texts (Arial Bold 9 pt thresholded at 110 of 255, ASCII 32 .. 126, made once
// with PIL): [advance, x0, top (from the baseline), width, the rows in hex, (width + 3) / 4 digits a row, bit x = column x0 + x]
const SK_FONT8 = [[3],[3,1,-7,1,"1111101"],[4,0,-7,4,"fa"],[5,0,-7,5,"1e0a1f0a1f0a05"],[5,0,-8,5,"040e07060e1c1f0e04"],[8,0,-7,8,"2617177ee8ec64"],[7,0,-7,6,"0c1e0e0e3b3b3e"],[2,0,-7,2,"33"],[3,0,-7,3,"422333224"],[3,0,-7,3,"122666221"],[4,0,-7,3,"2764"],[5,0,-6,5,"04041f0404"],[3,1,-1,1,"11"],[3,0,-3,3,"7"],[3,1,-1,1,"1"],[3,0,-7,2,"2222311"],[5,0,-7,5,"0e0a1b1b1b0a0e"],[5,1,-7,3,"6776666"],[5,0,-7,5,"0e1b180c06061f"],[5,0,-7,5,"0e0a080c181b0e"],[5,0,-7,5,"080c0e0a0b1f08"],[5,1,-7,4,"f17ccd7"],[5,0,-7,5,"0e0a030f1b1a0e"],[5,0,-7,5,"1f080c04040606"],[5,0,-7,5,"0e0a0a0e1b1b0e"],[5,0,-7,5,"0e0b1b1e180a0e"],[3,1,-5,1,"10001"],[3,1,-5,1,"100011"],[5,0,-6,5,"100e030e10"],[5,0,-5,5,"1f001f"],[5,0,-6,5,"030e180e03"],[6,1,-7,4,"7984202"],[9,0,-7,9,"0780c60fa16d0a50a507e08607c"],[7,0,-7,6,"0c1c14163e2323"],[7,1,-7,5,"0f19190f19190f"],[7,0,-7,6,"1c32030303321c"],[7,1,-7,5,"0f19111111190f"],[6,1,-7,5,"1f01010f01011f"],[6,1,-7,4,"f11f111"],[7,0,-7,7,"1c26027362663c"],[7,1,-7,5,"1919191f191919"],[3,1,-7,1,"1111111"],[5,0,-7,4,"88888be"],[7,1,-7,5,"190d070f0d1919"],[6,1,-7,4,"111111f"],[8,1,-7,6,"333b3b3f2f2d2d"],[7,1,-7,5,"111313151d1919"],[7,0,-7,7,"1c26636363261c"],[6,1,-7,5,"0f19190f010101"],[7,0,-7,7,"1c2663636b363c40"],[7,1,-7,5,"0f19190f0d1919"],[6,0,-7,6,"1e12021e30331e"],[6,0,-7,5,"1f040404040404"],[7,1,-7,5,"1919191919190f"],[6,0,-7,6,"3333121e1e0c0c"],[9,0,-7,8,"9b9bbaeaee6666"],[6,0,-7,6,"331e0c0c1e1e33"],[6,0,-7,6,"33121e0c0c0c0c"],[6,0,-7,5,"1f180c0406031f"],[3,1,-7,2,"311111113"],[3,0,-7,2,"1132222"],[3,0,-7,2,"322222223"],[5,1,-7,4,"27d"],[5,0,1,5,"1f"],[3,0,-7,2,"32"],[5,0,-5,5,"0e081e0b1e"],[6,1,-7,4,"1179997"],[5,0,-5,5,"0e1b031b0e"],[6,0,-7,5,"10101e1b131b1e"],[5,0,-5,5,"0e0b1f030e"],[3,0,-7,3,"6272222"],[6,0,-5,5,"1e1b131b1e1a0e"],[6,1,-7,4,"117d999"],[3,1,-7,1,"1011111"],[3,0,-7,2,"202222233"],[5,1,-7,4,"115775d"],[3,1,-7,1,"1111111"],[8,0,-5,7,"7f5b5b5b5b"],[6,1,-5,4,"fd999"],[6,0,-5,5,"0e1b13130e"],[6,1,-5,4,"7999711"],[6,0,-5,5,"1e1a131a1e1010"],[4,1,-5,2,"31111"],[5,0,-5,5,"0e030e180e"],[3,0,-7,3,"2272226"],[6,1,-5,4,"999df"],[5,0,-5,5,"1b0a0e0e04"],[7,0,-5,7,"497f3e3636"],[5,0,-5,5,"1b0e060e1b"],[5,0,-5,5,"1b0a0a0e040403"],[5,0,-5,4,"fc63f"],[4,0,-7,3,"462232264"],[3,1,-7,1,"11111111"],[4,0,-7,3,"322646223"],[5,1,-4,4,"3e"]];
const SK_W = 240;
// the mock-ups' colours: the defaults when the editor's (lay_state "ink") are not given
const SK_MOCK = { bg: [0, 0, 0], text: [235, 235, 235], white: [240, 240, 240], red: [230, 56, 41], yellow: [246, 182, 0], dim: [90, 90, 90],
                  mid: [150, 150, 150], surf: [26, 30, 34], line: [62, 62, 62], black: [14, 14, 14] };
const SK_HALO = [120, 20, 10], SK_LEDOFF = [110, 24, 16];           // (the mock-ups' LED shades, as cr_draw.c's)

// the pixel helpers on a 240 x 240 RGBA buffer, every write inside clip {x0, y0, x1, y1} (the mock-ups' px(): integer
// rectangles, rings pixel by pixel, lines as discs along them; text in SK_FONT8, a 1x face)
export function sketchPixels(rgba, clip) {
  const put = (x, y, c) => {
    if (x < clip.x0 || x >= clip.x1 || y < clip.y0 || y >= clip.y1) return;
    const i = (y * SK_W + x) * 4;
    rgba[i] = c[0]; rgba[i + 1] = c[1]; rgba[i + 2] = c[2]; rgba[i + 3] = 255;
  };
  const fill = (x, y, w, h, c) => {
    const x0 = Math.round(x), y0 = Math.round(y), x1 = x0 + Math.round(w), y1 = y0 + Math.round(h);
    for (let j = y0; j < y1; j++) for (let i = x0; i < x1; i++) put(i, j, c);
  };
  const dot = (x, y, c) => fill(x, y, 1, 1, c);
  // a ring between radii r0..r1 over the angles a0..a1 (degrees, 0 = right, clockwise), pixel by pixel
  const ring = (cx, cy, r0, r1, a0, a1, c) => {
    for (let y = Math.floor(cy - r1 - 1); y <= cy + r1 + 1; y++) for (let x = Math.floor(cx - r1 - 1); x <= cx + r1 + 1; x++) {
      const dx = x + 0.5 - cx, dy = y + 0.5 - cy, d = Math.hypot(dx, dy);
      if (d < r0 || d >= r1) continue;
      let a = Math.atan2(dy, dx) * 180 / Math.PI; if (a < 0) a += 360;
      let e = a1; if (e < a0) e += 360; let aa = a; if (aa < a0) aa += 360;
      if (aa >= a0 && aa <= e) dot(x, y, c);
    }
  };
  const disc = (cx, cy, r, c) => ring(cx, cy, 0, r, 0, 360, c);
  const line = (x0, y0, x1, y1, w, c) => {
    const n = Math.ceil(Math.hypot(x1 - x0, y1 - y0)) * 2 + 1;
    for (let i = 0; i <= n; i++) { const t = i / n; disc(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w / 2, c); }
  };
  // a text in SK_FONT8 (8 px bold) on baseline y, centred at x (align "left": from x); size is the mock-ups' (ignored)
  const text = (x, y, s, size, c, align = "center") => {
    const gl = [...String(s)].map((ch) => SK_FONT8[(ch.charCodeAt(0) - 32) >= 0 && ch.charCodeAt(0) < 127 ? ch.charCodeAt(0) - 32 : 31]);
    let pen = Math.round(align === "center" ? x - gl.reduce((n, g) => n + g[0], 0) / 2 : align === "right" ? x - gl.reduce((n, g) => n + g[0], 0) : x);
    for (const g of gl) {
      if (g.length > 1) {
        const [adv, gx, top, w, hex] = g, nd = (w + 3) >> 2;
        for (let r = 0; r * nd < hex.length; r++) {
          const bits = parseInt(hex.slice(r * nd, r * nd + nd), 16);
          for (let b = 0; b < w; b++) if (bits & (1 << b)) dot(pen + gx + b, y + top + r, c);
        }
        pen += adv;
      } else pen += g[0];
    }
  };
  return { fill, dot, ring, disc, line, text };
}

// the 270-degree sweep: 135 degrees (7:30) to 405 (4:30), clockwise
const SK_A0 = 135, SK_SWEEP = 270;
const skAng = (pct) => SK_A0 + SK_SWEEP * pct;
const skRad = (a) => a * Math.PI / 180;
const skTint = (col, f) => col.map((v) => Math.round(v * f));

// the one-cycle wave, 14 x 8 at (x, y), w px
function skIcon(p, kind, x, y, c, w = 2) {
  const W = 14, H = 8, L = (a, b, cc, d) => p.line(x + a, y + b, x + cc, y + d, w, c);
  if (kind === "saw") { L(1, H - 1, W - 3, 1); L(W - 3, 1, W - 3, H - 1); }
  else if (kind === "tri") { L(1, H - 1, W / 2, 1); L(W / 2, 1, W - 1, H - 1); }
  else if (kind === "sqr") { L(2, H - 1, 2, 1); L(2, 1, W - 2, 1); L(W - 2, 1, W - 2, H - 1); }
}
const skLed = (p, x, y, on, C) => { if (on) { p.disc(x, y, 4, SK_HALO); p.disc(x, y, 3, C.red); } else p.disc(x, y, 2, SK_LEDOFF); };
function skTicks(p, cx, cy, r0, r1, w, colFn) {
  for (let i = 0; i <= 10; i++) { const a = skRad(skAng(i / 10)); p.line(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0, cx + Math.cos(a) * r1, cy + Math.sin(a) * r1, w, colFn(i)); }
}
function skK6body(p, cx, cy, pct, tint, rim, ptr) {
  const r = 13; p.disc(cx, cy, r - 2, rim); p.disc(cx, cy, r - 4, tint);
  const a = skRad(skAng(pct)); p.line(cx + Math.cos(a) * 2, cy + Math.sin(a) * 2, cx + Math.cos(a) * (r - 5), cy + Math.sin(a) * (r - 5), 3, ptr);
}
// the two-LED cap of round 2 (S4 .. S6): rim and pad in the column colour
function skCap2(p, cx, cy, col, act, l, r, C) {
  const w = 30, h = 20, x = cx - w / 2, y = cy - 3;
  p.fill(x, y, w, h, act ? col : C.line); p.fill(x + 2, y + 2, w - 4, 8, C.black); p.fill(x + 2, y + 10, w - 4, h - 12, act ? skTint(col, .75) : C.surf);
  skLed(p, cx - 7, y + 6, l, C); skLed(p, cx + 7, y + 6, r, C);
}

// ---------------------------------------------------------------- the knobs ---
// (p, cx, cy, s): s.pct 0..1, s.col, s.active, s.small, s.C
const SK_KNOBS = {
  k1: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, r = 12, cur = Math.round(pct * 10);
    for (let i = 0; i <= 10; i++) { const a = skRad(skAng(i / 10)); const c = i === cur ? col : (act ? C.mid : C.dim);
      p.line(cx + Math.cos(a) * (r + 1), cy + Math.sin(a) * (r + 1), cx + Math.cos(a) * (r + 4), cy + Math.sin(a) * (r + 4), 2, c); }
    p.disc(cx, cy, r - 3, act ? C.mid : C.line); p.disc(cx, cy, r - 5, C.surf);
    const a = skRad(skAng(pct)); p.line(cx + Math.cos(a) * 2, cy + Math.sin(a) * 2, cx + Math.cos(a) * (r - 6), cy + Math.sin(a) * (r - 6), 3, act ? col : C.mid); },
  k2: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, r = 16; p.ring(cx, cy, r - 3, r, SK_A0, SK_A0 + SK_SWEEP, act ? C.line : C.black);
    if (pct > 0) p.ring(cx, cy, r - 3, r, SK_A0, skAng(pct), act ? col : C.dim);
    p.disc(cx, cy, r - 5, C.surf);
    const a = skRad(skAng(pct)); p.line(cx + Math.cos(a) * 3, cy + Math.sin(a) * 3, cx + Math.cos(a) * (r - 6), cy + Math.sin(a) * (r - 6), 3, act ? col : C.mid); },
  k3: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, r = 12; p.ring(cx, cy, r - 2, r, SK_A0, SK_A0 + SK_SWEEP, act ? C.line : C.black);
    if (pct > 0) p.ring(cx, cy, r - 2, r, SK_A0, skAng(pct), act ? col : C.dim);
    for (let i = 0; i <= 4; i++) { const a = skRad(skAng(i / 4)); p.line(cx + Math.cos(a) * (r + 1), cy + Math.sin(a) * (r + 1), cx + Math.cos(a) * (r + 4), cy + Math.sin(a) * (r + 4), 2, act ? C.mid : C.dim); }
    p.disc(cx, cy, r - 4, C.surf);
    const a = skRad(skAng(pct)); p.line(cx + Math.cos(a) * 2, cy + Math.sin(a) * 2, cx + Math.cos(a) * (r - 5), cy + Math.sin(a) * (r - 5), 3, act ? col : C.mid); },
  k4: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, r = 13; p.disc(cx, cy, r, act ? C.mid : C.line); p.disc(cx, cy, r - 2, C.surf);
    for (const i of [0, 1]) { const a = skRad(skAng(i)); p.line(cx + Math.cos(a) * (r + 1), cy + Math.sin(a) * (r + 1), cx + Math.cos(a) * (r + 3), cy + Math.sin(a) * (r + 3), 2, act ? C.mid : C.dim); }
    const a = skRad(skAng(pct)), c = act ? col : C.mid;
    for (let t = 0; t <= 1; t += 0.05) { const w = 7 - 5 * t; p.disc(cx + Math.cos(a) * (t * (r + 2)), cy + Math.sin(a) * (t * (r + 2)), w / 2, c); } },
  k5: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, r = 16, cur = Math.round(pct * 10);
    for (let i = 0; i <= 10; i++) { const a = skRad(skAng(i / 10)); const c = i <= cur ? (act ? col : C.dim) : (act ? C.line : C.black);
      p.line(cx + Math.cos(a) * (r - 4), cy + Math.sin(a) * (r - 4), cx + Math.cos(a) * r, cy + Math.sin(a) * r, 2, c); }
    const a = skRad(skAng(pct)); p.line(cx, cy, cx + Math.cos(a) * (r - 6), cy + Math.sin(a) * (r - 6), 3, act ? col : C.mid); p.disc(cx, cy, 2, act ? col : C.mid); },
  k6: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, r = 13, tint = act ? skTint(col, 0.28) : C.surf;
    for (let i = 0; i <= 10; i++) { const a = skRad(skAng(i / 10)); p.line(cx + Math.cos(a) * (r + 1), cy + Math.sin(a) * (r + 1), cx + Math.cos(a) * (r + 3), cy + Math.sin(a) * (r + 3), 2, act ? C.mid : C.dim); }
    p.disc(cx, cy, r - 2, act ? col : C.line); p.disc(cx, cy, r - 4, tint);
    const a = skRad(skAng(pct)); p.line(cx + Math.cos(a) * 2, cy + Math.sin(a) * 2, cx + Math.cos(a) * (r - 5), cy + Math.sin(a) * (r - 5), 3, act ? C.white : C.mid); },
  k6a: (p, cx, cy, s) => { const { pct, col, active: act, C } = s;
    skTicks(p, cx, cy, 14, 17, 2, () => act ? C.mid : C.dim); skK6body(p, cx, cy, pct, act ? skTint(col, .28) : C.surf, act ? col : C.line, act ? C.white : C.mid); },
  k6b: (p, cx, cy, s) => { const { pct, col, active: act, C } = s;
    skTicks(p, cx, cy, 14, 17, 2, () => act ? skTint(col, .55) : C.dim); skK6body(p, cx, cy, pct, act ? skTint(col, .28) : C.surf, act ? col : C.line, act ? C.white : C.mid); },
  k6c: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, cur = Math.round(pct * 10);
    skTicks(p, cx, cy, 14, 17, 2, (i) => i <= cur ? (act ? col : C.mid) : (act ? C.line : C.black)); skK6body(p, cx, cy, pct, act ? skTint(col, .28) : C.surf, act ? col : C.line, act ? C.white : C.mid); },
  // K6d as chosen (rounds 3 .. 5; the 22 px box: the small dial)
  k6d: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, small = !!s.small, r = small ? 9 : 13, t0 = small ? 10 : 14, t1 = small ? 12 : 17;
    const cur = Math.round(pct * 10);
    for (let i = 0; i <= 10; i++) { const a = skRad(skAng(i / 10)), near = i === cur;
      p.line(cx + Math.cos(a) * t0, cy + Math.sin(a) * t0, cx + Math.cos(a) * (near ? t1 + 1 : t1), cy + Math.sin(a) * (near ? t1 + 1 : t1), 2, near ? (act ? C.white : C.mid) : (act ? C.mid : C.dim)); }
    p.disc(cx, cy, r - 2, act ? col : C.line); p.disc(cx, cy, r - 4, act ? skTint(col, .40) : C.surf);
    const a = skRad(skAng(pct)); p.line(cx + Math.cos(a) * 2, cy + Math.sin(a) * 2, cx + Math.cos(a) * (r - 5), cy + Math.sin(a) * (r - 5), small ? 2 : 3, act ? C.white : C.mid); },
  k6e: (p, cx, cy, s) => { const { pct, col, active: act, C } = s, r = 13;
    skTicks(p, cx, cy, 14, 17, 3, () => act ? C.mid : C.dim); p.disc(cx, cy, r - 2, act ? skTint(col, .35) : C.surf);
    const a = skRad(skAng(pct)); p.line(cx + Math.cos(a) * 2, cy + Math.sin(a) * 2, cx + Math.cos(a) * (r - 5), cy + Math.sin(a) * (r - 5), 3, act ? C.white : C.mid); },
};

// ---------------------------------------------------------------- the buttons ---
// (p, cx, cy, s): s.on, s.icon, s.col, s.active, s.small, s.C
const SK_BUTTONS = {
  b1: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, w = 22, h = 20, x = cx - w / 2, y = cy - 3; skIcon(p, icon, cx - 7, y - 12, act ? col : C.dim);
    p.fill(x, y, w, h, act ? C.mid : C.line); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf); p.fill(x + 2, y + 2, w - 4, 8, C.black); skLed(p, cx, y + 6, on, C); },
  b2: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, w = 24, h = 22, x = cx - w / 2, y = cy - 11;
    if (on) { p.fill(x, y, w, h, act ? col : C.mid); skIcon(p, icon, cx - 7, y + 6, C.bg); }
    else { p.fill(x, y, w, h, act ? C.line : C.black); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf); skIcon(p, icon, cx - 7, y + 6, act ? C.dim : C.line); } },
  b3: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, w = 24, h = 20, x = cx - w / 2, y = cy - 3; skIcon(p, icon, cx - 7, y - 12, act ? col : C.dim);
    p.fill(x, y, w, h, act ? C.mid : C.line); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf); p.fill(x + 3, y + 3, w - 6, 3, on ? C.red : [100, 22, 16]); },
  b4: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, c = on ? (act ? col : C.mid) : (act ? C.dim : C.line), x = cx - 10, y = cy - 9, w = 20, h = 12;
    const L = (a, b, cc, d) => p.line(x + a, y + b, x + cc, y + d, 3, c);
    if (icon === "saw") { L(1, h - 1, w - 4, 1); L(w - 4, 1, w - 4, h - 1); } else if (icon === "tri") { L(1, h - 1, w / 2, 1); L(w / 2, 1, w - 1, h - 1); } else { L(3, h - 1, 3, 1); L(3, 1, w - 3, 1); L(w - 3, 1, w - 3, h - 1); }
    if (on) p.fill(x, y + h + 4, w, 2, c);
    p.disc(cx + 16, y + h + 5, 2, on ? C.red : [100, 22, 16]); },
  b5: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, w = 24, h = 20, x = cx - w / 2, y = cy - 3; skIcon(p, icon, cx - 7, y - 12, act ? col : C.dim);
    p.fill(x, y, w, h, act ? C.line : C.black); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf);
    if (on) p.fill(x + 2, y + 2, w - 4, (h - 4) / 2, act ? col : C.mid); else p.fill(x + 2, y + h / 2, w - 4, (h - 4) / 2, act ? C.dim : C.line); },
  // B1a as chosen (round 5: the wave white when on, grey when off; the 22 px box: the cap alone)
  b1a: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s;
    if (s.small) { const w = 22, h = 16, x = cx - w / 2, y = cy - 8;
      p.fill(x, y, w, h, act ? col : C.line); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf); p.fill(x + 2, y + 2, w - 4, 7, C.black); skLed(p, cx, y + 5, on, C); return; }
    const w = 22, h = 20, x = cx - w / 2, y = cy - 3;
    if (icon) skIcon(p, icon, cx - 7, y - 12, on ? (act ? C.white : C.mid) : (act ? C.dim : C.line));
    p.fill(x, y, w, h, act ? col : C.line); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf); p.fill(x + 2, y + 2, w - 4, 8, C.black); skLed(p, cx, y + 6, on, C); },
  b1b: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, w = 22, h = 20, x = cx - w / 2, y = cy - 3; skIcon(p, icon, cx - 7, y - 12, act ? col : C.dim);
    p.fill(x, y, w, h, act ? col : C.line); p.fill(x + 2, y + 2, w - 4, 8, C.black); p.fill(x + 2, y + 10, w - 4, h - 12, act ? skTint(col, .75) : C.surf); skLed(p, cx, y + 6, on, C); },
  b1c: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, w = 22, h = 20, x = cx - w / 2, y = cy - 3; skIcon(p, icon, cx - 7, y - 12, act ? col : C.dim);
    p.fill(x, y, w, h, act ? col : C.line); p.fill(x + 2, y + 2, w - 4, h - 4, act ? skTint(col, .30) : C.surf); p.fill(x + 2, y + 2, w - 4, 8, act ? skTint(col, .15) : C.black); skLed(p, cx, y + 6, on, C); },
  b1d: (p, cx, cy, s) => { const { on, icon, col, active: act, C } = s, w = 22, h = 20, x = cx - w / 2, y = cy - 3; skIcon(p, icon, cx - 7, y - 12, act ? col : C.dim);
    const c = on ? (act ? col : C.mid) : (act ? C.line : C.black);
    p.fill(x, y, w, h, c); p.fill(x + 2, y + 2, w - 4, 8, C.black); p.fill(x + 2, y + 10, w - 4, h - 12, on ? (act ? skTint(col, .75) : C.mid) : C.surf); skLed(p, cx, y + 6, on, C); },
};

// ---------------------------------------------------------------- the switches ---
// (p, cx, cy, s): s.pos 0 .. 2, s.col, s.active, s.small, s.C
const SK_SWITCHES = {
  s1: (p, cx, cy, s) => { const { pos, col, active: act, C } = s, x = cx - 3, y = cy - 15, h = 30; p.fill(x - 2, y - 2, 10, h + 4, act ? C.mid : C.line); p.fill(x, y, 6, h, C.black);
    p.fill(x - 2, y + 2 + (2 - pos) * 9, 10, 6, act ? col : C.mid); },
  s2: (p, cx, cy, s) => { const { pos, col, active: act, C } = s;
    for (let i = 0; i < 3; i++) { const y = cy + 11 - i * 11, on = i === pos; p.fill(cx - 10, y - 7, 20, 8, on ? (act ? col : C.mid) : (act ? C.line : C.black)); if (!on) p.fill(cx - 8, y - 5, 16, 4, C.surf); } },
  s3: (p, cx, cy, s) => { const { pos, col, active: act, C } = s, x = cx - 5, y = cy - 15, h = 30; p.fill(x - 2, y - 2, 14, h + 4, act ? C.mid : C.line); p.fill(x, y, 10, h, C.black);
    const f = pos === 0 ? 3 : pos === 1 ? h / 2 : h; p.fill(x, y + h - f, 10, f, act ? col : C.mid); },
  s4: (p, cx, cy, s) => skCap2(p, cx, cy, s.col, s.active, s.pos === 2, s.pos === 1, s.C),
  // S5 as chosen (rounds 3 .. 5: the rim in the column colour, the face dark; the 22 px box: 16 high)
  s5: (p, cx, cy, s) => { const { pos, col, active: act, C } = s, w = 30, h = s.small ? 16 : 20, x = cx - w / 2, y = s.small ? cy - 8 : cy - 3;
    p.fill(x, y, w, h, act ? col : C.line); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf); p.fill(x + 2, y + 2, w - 4, s.small ? 7 : 8, C.black);
    skLed(p, cx - 7, y + (s.small ? 5 : 6), pos >= 1, C); skLed(p, cx + 7, y + (s.small ? 5 : 6), pos === 2, C); },
  s6: (p, cx, cy, s) => { skCap2(p, cx, cy, s.col, s.active, s.pos === 2, s.pos === 1, s.C); const y = cy - 11, c = s.active ? s.col : s.C.dim;
    p.fill(cx + 6, y, 2, 2, c); p.fill(cx - 10, y, 2, 2, c); p.fill(cx - 6, y, 2, 2, c); },
};

// ---------------------------------------------------------------- two LEDs (REV) ---
// (p, cx, cy, s): s.on (the second value: the right LED), s.names, s.col, s.active, s.small, s.C
function skRev(p, cx, cy, s, o) {
  const { col, active: act, C } = s, pos = s.on ? 1 : 0, w = 30, h = s.small ? 16 : 20, x = cx - w / 2, y = s.small ? cy - 8 : cy - 3;
  const nm = s.names || ["1/2", "3"];
  if (o.names && !s.small) { p.text(cx - 7, y - 5, nm[0], 8, act ? (pos === 0 ? C.white : C.dim) : C.dim); p.text(cx + 7, y - 5, nm[1], 8, act ? (pos === 1 ? C.white : C.dim) : C.dim); }
  p.fill(x, y, w, h, act ? col : C.line); p.fill(x + 2, y + 2, w - 4, h - 4, C.surf); p.fill(x + 2, y + 2, w - 4, s.small ? 7 : 8, C.black);
  const ly = y + (s.small ? 5 : 6);
  const lit = (xx, on, yel) => { if (on) { p.disc(xx, ly, 4, yel ? [120, 90, 0] : SK_HALO); p.disc(xx, ly, 3, yel ? C.yellow : C.red); } else p.disc(xx, ly, 2, yel ? [110, 84, 10] : SK_LEDOFF); };
  lit(cx - 7, pos === 0, false); lit(cx + 7, pos === 1, !!o.yellow);
}

// ---------------------------------------------------------------- the registry ---
const skEntry = (name, kind, round, note, f) => [name, { name, kind, round, note, f }];
const SK_REGISTRY = new Map([
  skEntry("k1", "knob", 1, "Prophet, heavier: 2 px ticks in mid grey, the one nearest the value in the row colour; a 2 px rim; a 3 px pointer in the row colour.", SK_KNOBS.k1),
  skEntry("k2", "knob", 1, "ChoralRoot arc: a 3 px dark track, the value arc 3 px in the row colour, a filled face, a 3 px pointer; no ticks.", SK_KNOBS.k2),
  skEntry("k3", "knob", 1, "Hybrid: a 2 px track with the value arc in colour, five bold ticks outside it, a face with a wide pointer.", SK_KNOBS.k3),
  skEntry("k4", "knob", 1, "Chicken head: a dark skirt with a 2 px rim, a solid wedge pointer in the row colour, ticks at 0 and 10 only.", SK_KNOBS.k4),
  skEntry("k5", "knob", 1, "Open scale: no face, eleven 2 px ticks filled up to the value in the row colour (a fan), a 3 px pointer.", SK_KNOBS.k5),
  skEntry("k6", "knob", 1, "Lit face: the face tinted with the row colour (28 %), a white 3 px pointer, 2 px ticks in mid grey.", SK_KNOBS.k6),
  skEntry("k6a", "knob", 2, "K6 with eleven 3 px ticks in mid grey (dim on a dim row), the face at 28 %, a 2 px rim in the row colour.", SK_KNOBS.k6a),
  skEntry("k6b", "knob", 2, "K6a with the ticks in the row colour at half strength; white pointer.", SK_KNOBS.k6b),
  skEntry("k6c", "knob", 2, "K6a with the ticks up to the value in the row colour, the rest grey: the scale doubles as a fill.", SK_KNOBS.k6c),
  skEntry("k6d", "knob", 5, "As chosen (the firmware's CR_G_P5KNOB): grey ticks, the nearest white and 1 px longer, the face at 40 %, a white pointer.", SK_KNOBS.k6d),
  skEntry("k6e", "knob", 2, "3 px ticks in mid grey, the face tinted 35 % with no separate rim, a 3 px white pointer.", SK_KNOBS.k6e),
  skEntry("b1", "led", 1, "Prophet cap, bigger LED: a 22 x 20 cap with a grey rim, a 6 px LED with a halo (a 4 px dark dot off), the wave 4 px above in the row colour.", SK_BUTTONS.b1),
  skEntry("b2", "led", 1, "ChoralRoot block: the whole cap filled in the row colour when on, the wave in black inside it; off a dark cap, the wave grey.", SK_BUTTONS.b2),
  skEntry("b3", "led", 1, "LED bar: a 3 px bar across the top of the cap, red on, dark red off; the wave above in the row colour.", SK_BUTTONS.b3),
  skEntry("b4", "led", 1, "The wave is the switch: drawn large (3 px), in the row colour and underlined when on, dim when off; an LED dot beside.", SK_BUTTONS.b4),
  skEntry("b5", "led", 1, "Toggle: a well split in two, the top half lit in the row colour when on, the bottom half when off.", SK_BUTTONS.b5),
  skEntry("b1a", "led", 5, "As chosen (the firmware's CR_G_P5LED): the rim in the row colour, the face dark, the wave white when on and grey when off.", SK_BUTTONS.b1a),
  skEntry("b1b", "led", 2, "The pad (the lower part) in the row colour, the LED window dark above it, the rim in the colour.", SK_BUTTONS.b1b),
  skEntry("b1c", "led", 2, "The whole cap tinted in the row colour (30 %), the rim at full colour, the LED window a shade darker.", SK_BUTTONS.b1c),
  skEntry("b1d", "led", 2, "The rim and pad in the row colour only while on; off, the cap is grey: the colour itself says on.", SK_BUTTONS.b1d),
  skEntry("s1", "switch", 1, "Lever, thicker: a 6 px slot with a 2 px rim, a 10 x 6 lever block in the row colour at the position.", SK_SWITCHES.s1),
  skEntry("s2", "switch", 1, "Steps: three stacked blocks, the current one filled in the row colour, the others dark.", SK_SWITCHES.s2),
  skEntry("s3", "switch", 1, "Fill bar: a 10 x 30 well filled to the position (Off empty, Half half, Full full) in the row colour.", SK_SWITCHES.s3),
  skEntry("s4", "switch", 2, "Two LEDs in one wide cap (pad in the colour), binary: Off none, Half the right LED, Full the left.", SK_SWITCHES.s4),
  skEntry("s5", "switch", 5, "As chosen (the firmware's CR_G_P5SWITCH): the wide cap, the rim in the row colour, the LEDs count the position.", SK_SWITCHES.s5),
  skEntry("s6", "switch", 2, "Binary as S4 with the bit values marked above the LEDs in 2 px dots.", SK_SWITCHES.s6),
  skEntry("r1", "led2", 3, "REV, both LEDs red: left lit = the first value (1/2), right lit = the second (3); the value row says which.", (p, cx, cy, s) => skRev(p, cx, cy, s, {})),
  skEntry("r2", "led2", 3, "REV, red and yellow as the panel: the right LED yellow for the second value.", (p, cx, cy, s) => skRev(p, cx, cy, s, { yellow: true })),
  skEntry("r3", "led2", 5, "As chosen (the firmware's CR_G_P5LED2): both red, the value names (8 px) above the LEDs, the current one white.", (p, cx, cy, s) => skRev(p, cx, cy, s, { names: true })),
]);
export const SKETCH_NAMES = [...SK_REGISTRY.keys()];
export const SKETCH_WAVES = ["saw", "tri", "sqr"];
// the registry's entries: {name, kind, round, note, draw(p, box, state)}
export const SKETCHES = Object.fromEntries([...SK_REGISTRY].map(([k, v]) => [k, {
  name: v.name, kind: v.kind, round: v.round, note: v.note,
  // (a knob in the 34 px box 6 px lower, level with the buttons' LEDs: the firmware's CR_G_P5KNOB, the owner's)
  draw: (p, box, st) => v.f(p, box.x + (box.w >> 1), box.y + (box.h >= 30 ? (v.kind === "knob" ? 18 : 12) : 10), { ...st, C: { ...SK_MOCK, ...(st.C || {}) }, small: box.h < 30 }),
}]));
// a cell style -> {name, icon} when it is a known sketch ("sketch:k6d", "sketch:b1a-saw"), else null
export function parseSketch(style) {
  const m = typeof style === "string" && /^sketch:([a-z0-9]+)(?:-(saw|tri|sqr))?$/.exec(style);
  return m && SK_REGISTRY.has(m[1]) ? { name: m[1], icon: m[2] || null } : null;
}
export const isSketchStyle = (style) => typeof style === "string" && style.startsWith("sketch:");

// the cells' glyph boxes of an edit8 screen (lay_state: kind, band, lanes), as cr_draw.c cr_p_edit8 draws them: row r,
// column k -> {box: {x, y, w, h}, clip: the cell's column between its label and its value} | null
export function sketchBox(state, r, k) {
  if (!state || state.kind !== "edit8" || k < 0 || k > 3) return null;
  const n = Math.max(1, state.lanes | 0), band = state.band && state.band !== "none", big = !band || n === 1;
  if (r >= Math.min(n, 2)) return null;
  const y0 = band ? (r ? 184 : 124) : (r ? 134 : 30), x = k * 60;
  return big ? { box: { x, y: y0 + 26, w: 60, h: 34 }, clip: { x0: x, y0: y0 + 19, x1: x + 60, y1: y0 + 68 } }
             : { box: { x, y: y0 + 15, w: 60, h: 22 }, clip: { x0: x, y0: y0 + 14, x1: x + 60, y1: y0 + 40 } };
}
// draw the sketches of a classic screen into rgba (the module's 240 x 240 RGBA): cells [{row, k, style, names}];
// state: lay_state after the render (its rows' fills, the active lane, the ink). -> the number drawn
export function drawSketches(rgba, state, cells) {
  let n = 0;
  const ink = state && state.ink, KC = ink ? [ink.blue, ink.orange, ink.white, ink.green] : [[43, 80, 200], [240, 122, 26], [246, 243, 234], [47, 179, 122]];
  for (const c of cells) {
    const sk = parseSketch(c.style), g = sketchBox(state, c.row, c.k), cell = state && state.rows && state.rows[c.row] && state.rows[c.row][c.k];
    if (!sk || !g || !cell || !cell.on) continue;
    const pct = Math.max(0, Math.min(1, (cell.pct | 0) / 255)), active = c.row === state.lane;
    const p = sketchPixels(rgba, g.clip);
    SKETCHES[sk.name].draw(p, g.box, { pct, on: cell.pct >= 128, pos: cell.pct < 64 ? 0 : cell.pct < 192 ? 1 : 2, names: c.names || null,
                                       icon: sk.icon, col: KC[c.k], active, C: ink || undefined });
    n++;
  }
  return n;
}
