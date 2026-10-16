# The layout tool

A web page for designing the pages of the sound editor: which parameters are on a screen, in which lane, on which
knob, and (later) how the screen looks. The owner's request: "a webapp tool in which I can mockup page layouts for
the synth engine sound editor interfaces".

Two things make it more than a drawing program:

- **The preview is the firmware.** The picture in the tool is drawn by the unit's own renderer (`cr_draw.c` on
  `gfx.c`) from the unit's own editor code (`cr_edit.c`) on the real engines and their factory sounds: the whole
  firmware compiled to WebAssembly, as the browser emulator is. What the tool shows is what the unit shows, to the
  pixel (tested: below).
- **Its output is a file the firmware build reads.** A layout is JSON in `design/layouts/`; the build turns it into
  the engine's tables. Layouts are compiled in: a unit never loads one.

It was built in steps ("The five steps", below). **Steps 1 to 4 are done**: the viewer; the designer, in which
today's pages are rearranged and new free screens laid out, previewed live and exported as a design file; and the
generator that compiles a design into the build (`tools/gen_layout.py`), for free screens and, since 3y97, for classic
ones. The owner's P5 rows (`p5-rows.json`: classic edit8 screens of the p5 cell styles) are P5's editor on the unit,
compiled from the design file as a screen plan (`cr_pages.c` `cp_deep`, `cr_edit.c`); the designer draws every
design through that same editor and renderer, so its preview is the unit's picture ("Step 4, the firmware: what was
built", below). Not tested on a unit yet.

## The owner's rules

Verbatim (2026-10-13):

> Main rule for pages is they should all have the same consistent header across pages. None of the pages should use
> the main keybed for shortcuts. While sound designing I want to always be able to play notes. I design it, then we
> figure out how to build it. New UI's are in the build not on the fly changeable.

What follows from them:

1. **One shared header on every editor page: the editor's title line.** The owner (2026-10-13): "The current header
   that shows the preset name and the page name is good, no need to change it." So the shared header IS the title
   line the editor draws on every page today (`cr_draw.c` `cr_ed_title`: the sound's name left, the page's name
   right, rows 0 .. 27). Every design screen carries it automatically: it is not an element, it cannot be moved or
   edited, and a free screen's design area is the 240 x 212 under it. (The step-2 viewer's "no shared header" note
   was wrong and is gone; the catalog's `"header": null` means only that the header is not part of a layout.)
2. **The 27 keys always play.** No page may use a key as a switch or a shortcut. A switch on a page is operated by
   a knob or SELECT, nothing else.
3. **Design first, build second.** The tool is a design tool before it is a generator: the owner lays pages out
   freely, and how the firmware builds a design is worked out afterwards. So the later steps must let him place
   things the firmware cannot draw yet. Such an element is flagged "not buildable yet"; it is never refused.
4. **Layouts are in the build.** A layout is compiled into the firmware; nothing is loaded or changed at run time
   on a unit. (The preview module's `lay_load` is the tool's own door, in the browser only.)

## Build it and open it

```
sh tools/layout/build_layout.sh     # build/layout-web/crlayout.wasm and crlayout.js (Emscripten, ~/emsdk; 6 s)
python3 tools/layout/make_page.py   # build/layout-web/index.html (web/layout.html with web/fm1layout.js inlined)
open build/layout-web/index.html    # or: python3 -m http.server -d build/layout-web
node web/test_layout.mjs            # the tests (the emulator comparison needs sh tools/emu/web/build_web.sh)
node tools/layout/dump_catalogs.mjs # design/layouts/*.json again, after a change to an engine's pages
```

The page works from `file://`: `crlayout.js` carries the module as base64, so nothing is fetched. `crlayout.wasm`
beside it is the same bytes, for node and other tools. `web/make_site.py` puts the page into the site as
`webapp/layout/` (index.html, crlayout.js, crlayout.wasm) and links it from the landing page; without
`build/layout-web` it warns, leaves the link out and builds the rest. The Pages job (`.github/workflows/pages.yml`)
does not build the module yet, so the published site has no layout viewer until a step is added there.

A link opens a place: `?engine=P5&group=FILT&screen=1&lane=2&preset=1` (numbers from 1; the group by name or
index), `?view=sheet` for the contact sheet.

## The designer (steps 3 and 4)

"Viewer looks good, let's build the designer." (the owner, 2026-10-13). **Design** in the top bar; the engine buttons
work in both modes. The design is kept in the browser (localStorage, per engine; the page works without it) and is
exported as a file.

- **Top**: the design's name; its *Style* (flat or p5: "The p5 style", below); *open a shipped design* (the files of
  `design/layouts/designs/`, e.g. `p5-osc-panel`, `p5-panel`); *New from today* (today's layout, every screen classic); *Import* / *Export JSON* (a file pick and a
  download: no server); *PNG* of the screen; *Contact sheet*; the sound; *Undo* / *Redo* (Ctrl+Z, Ctrl+Shift+Z).
- **Left**: the groups (the editor's buttons: OSC, FILT, ENV, LFO, MOD, FX, MIX) and their screens. The current
  screen has earlier / later, duplicate, delete and *move to* another group. *New screen*: a group and a kind (free,
  classic edit8, classic stack), or *Copy today's* page as a classic screen.
- **Centre**: the live 240 x 240 at 2x with SELECT and the four knobs, as in the viewer, drawn and turned by the
  firmware's own editor (free screens too: "How a design is drawn"), and a badge under it: "buildable today" (the
  unit shows it as designed and `tools/gen_layout.py` would compile it) or **"design: not buildable yet"** (the
  badge's title says why: the generator's own words, `validateDesign(d, cat, {build: true})`). Over a free screen an editing layer on the 4 px grid: drag an element from the strip above the
  screen (frame, label, knob, switch, led, bar, glyph, band) onto it; click to select, Shift+click to add, drag to
  move, the yellow corner to resize; arrows nudge 4 px (Shift 16); Delete; Ctrl+D duplicates, Ctrl+A selects all;
  align left / top / right, spread. **Copy and paste between screens**: Ctrl+C / Cmd+C (or *copy*) keeps the
  selected elements in the page's clipboard (and puts them on the system's as JSON text when the browser lets it);
  Ctrl+V / Cmd+V (or *paste*) puts them on the current free screen at their places (8 px off where an identical box
  is already), with new ids, everything else kept (the parameter too: the lint then says it is twice, for you to
  rebind), selected and in no lane, one undo step (`pasteElements`). The shortcuts do nothing while typing. Each element in a lane wears its "lane·knob" tag in the knob's colour.
- **Lanes**: four coloured slots a lane (KNOB 1..4); SELECT moves between lanes. On a free screen select an element,
  then click a slot (it leaves any other slot); on a classic screen drag a parameter onto a slot, or click the slot
  and pick it in the inspector. **Click a cell on a classic screen's picture** (3y95) to select its slot, the same as
  clicking the slot under the screen (its lane shown): columns of 60 px, the rows by the screen's geometry as the
  editor draws it (`cellAt` in fm1layout.js, from `lay_state`'s kind, band and lanes: with a band two lanes at
  124–180 / 184–240, one lane 124–224; without a band 30–130 / 134–234; a stack's rows share 41–239); a click on the
  title line or the band selects nothing. Lanes can be added (up to 8), moved up, deleted; a classic lane has its title.
- **Right**: the inspector (a free element's type, parameter, label or text, x y w h, switch positions, led glyph,
  glyph, band, dial size, label size and alignment, colour: `auto` = the editor's lane colours, or a palette token;
  a classic screen's title, kind, band, and the selected slot's parameter and label) and the **parameter palette**:
  the engine's parameters grouped by today's pages, searchable, "placed" on those the design uses; drag one onto a
  slot or an element, or click it to bind the selection.
- **Lint** under the screen: the firmware's text lint (a text cut, off the screen, two texts overlapping) and the
  design's checks: a parameter twice on a screen or in the design, a bound element in no lane or in two, a lane of
  more than four, two elements overlapping (frames excepted), outside the area under the header, more than 24
  elements or 8 lanes, a title wider than the header's right side (112 px), and the "not buildable yet" reasons.
  One is a warning (yellow), not an error: **order**, "lane n: its slots are not left to right (KNOB 1..4 should follow
  the screen)", when a free lane's bound slots' x goes back (0.22-beta1's LFO lane had FREQ, the right-hand knob, on
  KNOB 1; 3y92). `tools/gen_layout.py` prints it as a warning and builds; it changes no error.
- **Contact sheet**: per group, today's screens x lanes beside the design's; a click on a design tile opens it; one
  PNG of the whole sheet.

Turning a knob changes the preview's sound only; any edit of the design re-opens the screen from the reset
instance, so earlier turns are undone then (the viewer's "Reload sound" behaviour). No element can be bound to a
key, and a design file that tries (any `"key"`) is refused: the 27 keys always play.

### How a design is drawn

**Classic screens: through the real editor.** `packDesign` (web/fm1layout.js) packs the design's classic screens into
the firmware's own tables and `lay_load` hands them to the editor through `cp_deep_hook`: an `eng_deep_t` whose pages
hold, per column, a copy of the built-in descriptor the design binds there, and whose `get` / `set` / `desc` /
`mod_dst` go through to the built-in (page, column), so the engine sees its own patch addresses. Labels come through a
second preview-only hook, `cr_edit.c` `ce_label_hook`. `ce_scr`, `ce_view`, `ce_screen` and `cr_draw` then draw the
design exactly as the unit would, and the knobs and SELECT are the unit's. Two ways:

- **in place**, when every group's classic screens have today's structure (as many, the same kinds and lane counts):
  today's page titles are kept (so the editor derives the same screens, bands and titles), with the design's
  parameter and label in every column. Moves, swaps and relabels within that structure; tested: a relabel changes
  nothing but the label's pixels, a swap makes the knobs turn the swapped parameters.
- **plan**, when the structure changed (screens added, removed, reordered or moved between groups), or when the
  catalog's screens are a compiled layout's (the catalog's `"layout"`: P5's rows; "today's structure" is then not the
  pages'): OSC .. MOD become a screen plan (`core.h` `eng_screen_t`, as FM TONE's): one page per lane, an edit8 screen
  of one or two lanes, its band (none, env, filter, algo, ade2), its title, each cell's style and label. This is the
  form `tools/gen_layout.py` compiles, byte for byte (web/test_layout.mjs 7): a design in it is what the unit shows.

What neither can express is flagged **"not buildable yet"** and today's screen is shown in its place: a stack or a
screen of more than two lanes in a changed group, a part parameter moved onto an engine page, a changed platform
screen (FX, MIX, a platform MOD), a lane title or band the in-place way cannot change (a note). A group with free
screens only keeps today's tables, unseen. The editor appends EXPR after a plan's MOD screens, as on every engine (3y97).
The MOD group's last screen, EXPR (0.22, type `expr`: the sound's velocity
and aftertouch routings, no deep page), is such a stack: a plan always flags it.

**Free screens: the firmware's own kind.** `packDesign` packs a design's free screens of OSC .. MOD into exactly the
records the generator compiles (`packFree`: an element 14 B, a screen 38 B, one string pool; the same texts: a frame's
title in capitals, the defaults, the parameter's label where the element has none, value names in the pool, frames
first; for `p5-panel.json` byte for byte the generator's records, tests/gen_layout_test.py). `lay_load` installs them
through `cr_layout.c`'s preview-only `cr_layout_hook`, so the unit's own editor and renderer show them: `cr_edit.c`
CE_S_FREE (the lanes, KNOB 1..4, SELECT, the hot mark, the knob's step and acceleration) and `cr_draw.c` CR_K_FREE (the
p5 look, "The p5 style"). The preview of a free screen IS the unit's picture (the loaded panel and the compiled one
gave the same 75 frames while the panel was built, 3y87; since 3y97 no unit carries a free design, and the free-page
code is compiled into the preview and into a unit only when a built design has free screens: `CR_LAYOUTS_FREE`).
MOD's EXPR screen (0.22) follows a design's MOD screens, on the unit and in the preview;
it is the platform's and never in a design. A design whose classic tables are not the built-in ones (a plan, a moved column) carries a
copy of the built-in pages after its own, fenced off by a section, for its free screens' parameters. What the unit
cannot show as designed is marked "not buildable yet" with its reason: a classic screen beside free screens in one
group (the editor shows the group's free screens), a free screen in FX or MIX, and everything the generator refuses
(a flat style: the firmware draws the p5 look only, so a flat design is previewed in it; a band element: not drawn;
an element off the grid or over the title line; the budgets; a bound element in no lane; a parameter twice ..).

### The module's design API (preview only)

| function | does |
| --- | --- |
| `lay_load(ptr, len)` | a packed design (below) for its engine: its classic pages and its free screens: `{"ok", "engine", "pages", "screens", "free"}` or `{"ok": false, "error"}`; engine 255 unloads. Kept across `lay_show` (the tables live outside the snapshot) |
| `lay_layouts(on)` | the compiled layouts (`build/gen/cr_layouts.h`: P5's rows, the unit's editor) in effect from the next `lay_show` (1, the default: what `lay_catalog` lists), or not (0: the classic derivation of the built-in pages, `lay_catalog_builtin`); a loaded design of the engine shows its own screens either way. Returns the previous setting |
| `lay_compiled(engine)` | the engine's compiled layout in the packed form, into `lay_buf`: its length (0: none). A classic layout: the classic half ('L' 3: its pages with each column's built-in (page, col), style and label, the sections, the screens), then the free half if the engine has free screens too; free screens alone: the free half |
| `lay_pget(page, col)`, `lay_ptext(page, col)` | a parameter of the editor's sound by its built-in address (page 255: a part parameter, col its `P_` id): the value, the editor's cell text (`ce_text`: the unit's) |
| `lay_draw_begin(title, page)`, `lay_draw_text(bold, px, x, y, s, col, align, maxw)`, `lay_draw_end()` | texts in the firmware's faces on an empty page under the title line (the contact sheets' captions): the 240 x 240 RGBA |
| `lay_text_w(s, px, bold)` | a text's advance (the design's checks: a title that fits the header) |

The packed form: `'L' 3 engine npages`, per page `tlen title` and four times `srcpage srccol style llen label` (srcpage
255: an empty column; style the cell's `ENG_C_*`, 'L' 2 has none), `section[8]`, `nscreens`, per screen `sec pg0 pg1 band tlen title`; then, with free screens, the
free half: `nfs`, `nel` (2 B, little-endian), `npool` (2 B), the screens (`cr_lay_screen_t`, 38 B), the elements
(`cr_lay_elem_t`, 14 B), the pool.

## The design format, version 2

A design is a "choralroot-layout" file of version 2 with `"kind": "design"`, in
`design/layouts/designs/<engine>-<name>.json` (the generated `design/layouts/<engine>.json` stay version 1: today's
layouts). It binds parameters by **stable id**: `"<page title>:<short>"` for an engine page's column (`"FILTER:CUT"`,
`"OSC B+:LOFREQ"`), `"part:<P_ id>"` for a part parameter (`"part:12"`); the designer's palette and
`paramList(catalog)` list them. `params_hash` ties the file to the parameters as in version 1.

```
{
  "format": "choralroot-layout", "version": 2, "kind": "design",
  "name": "osc-panel",
  "style": "flat",                        optional: how free screens look, "flat" (the default) or "p5" (below)
  "engine": {"index": 16, "name": "P5"}, "params_hash": "f5941e824048848d",
  "header": {"kind": "title-line"},       the shared header: the editor's title line, fixed (rule 1)
  "groups": [                             the editor's groups by name (OSC FILT ENV LFO MOD FX MIX), screens in order
    {"group": 1, "name": "FILT", "screens": [
      {"type": "classic",                 (a) one of today's kinds
       "kind": "edit8",                   edit8 (two rows under a band) or stack
       "band": "filter",                  none env filter dx cz algo ade2
       "title": "FILTER",
       "lanes": [                         1 .. 8 lanes of four cells, KNOB 1..4; null = an empty cell
         {"title": "FILTER", "cells": [
           {"param": "FILTER:RES"},
           {"param": "FILTER:CUT", "label": "Bright"},       a label of the design's own (else the editor's)
           {"param": "FILTER:FENV", "style": "p5knob"},      a look of its own ("Cell styles", below; else the editor's;
                                                             "sketch:<name>": a sketch glyph, "Sketch glyphs", below)
           {"param": "FILTER:FENV"}, {"param": "FILTER:KTRK"}]},
         ..]}]},
    {"group": 0, "name": "OSC", "screens": [
      {"type": "free",                    (b) placed elements under the header (x 0 .. 239, y 28 .. 239, 4 px grid)
       "title": "OSC A",                  the header's right text
       "elements": [
         {"id": "fa", "type": "frame", "x": 4, "y": 36, "w": 232, "h": 80, "text": "OSC A"},
         {"id": "a_freq", "type": "knob", "x": 12, "y": 44, "w": 56, "h": 64, "param": "OSC A:FREQ", "label": "Freq", "size": 32},
         {"id": "a_saw", "type": "led", "x": 80, "y": 56, "w": 40, "h": 12, "param": "OSC A:SAW", "glyph": "saw"},
         {"id": "a_sync", "type": "switch", "x": 184, "y": 48, "w": 48, "h": 44, "param": "OSC A+:SYNC", "label": "Sync", "positions": 2},
         {"id": "m_a", "type": "bar", "x": 12, "y": 144, "w": 68, "h": 28, "param": "MIXER:MIX A", "label": "Osc A"},
         ..],
       "lanes": [["a_freq", "a_saw", "a_pulse", "a_pw"], ["a_sync", null, null, null], ..]}   ids, KNOB 1..4
    ]}
  ]
}
```

Elements: `frame` (a titled section box: `text`), `label` (static `text`, `size`, `align`, `bold`), `knob` (dial,
label, value: `size` the dial's height), `switch` (`positions` 2 or 3, the current one filled; `names` optional),
`led` (an on/off square with its label, or a small waveform `glyph`: saw, square, triangle, sine, noise), `bar`
(level), `glyph` (any `CR_G_*` pictogram by name, its fill from the bound parameter if any), `band` (`band`: env,
filter, dx, cz, algo, ade2, at its own rows). `param`, `label` and `colour` (a palette token: white red blue yellow
orange green grey black bg surf text theme accent mid dim line; default `auto`) where they apply. Every bound element
must be in exactly one lane; up to 4 a lane, 8 lanes and 24 elements a screen (the firmware's `cell[8][4]`).
`validateDesign` lists what is wrong; the page refuses a file with errors and opens one with warnings. The design's
`style` (absent, `"flat"` or `"p5"`; anything else is an error) changes how frame, knob, led and switch look and their
new sizes (p5: knob 56 x 64 with a dial of 36, led 40 x 32, switch 56 x 64), not what they bind.

### The p5 style

The owner (2026-10-14): "For the prophet 5, I would like the sound editor UI to better resemble the real prophet 5
interface." and "I'd like update glyph designs to match with the Prophet 5 knobs and layout, lets do mockups and
Designer pages first." `"style": "p5"` composes a free screen's elements as the Prophet-5's panel. It is the
firmware's look (`cr_draw.c` CR_K_FREE, ported in 3y86 from the tool's first composition, `freeOps` / `p5Op`, which
went away in 3y87): the only one the firmware draws, so a flat design is previewed in it too and is "not buildable":

- **frame**: a rounded outline (`r`, default 3; 1 px, `line` or its colour), its text in capitals, 10 px bold `mid`,
  centred in the top border on a `bg` block, the border showing either side: the panel's section titles. A frame's
  `r` (0 .. 12; the inspector's *Corners*, 0 .. 12 in steps of 2) rounds it in either style (flat: square without);
  the owner's panel uses 6.
- **knob**: the dial box (`size`, default 36; 28 .. 44) at the cell's top: a printed scale of 11 ticks (0..10) over the
  270 degrees at the box's edge (`line`, the end ticks 2 px), a face of radius size / 2 - 6 in `surf` with a `mid` rim,
  a 2 px pointer in the element's colour (the lane's knob colour when its lane is active, else `dim`). The value (the
  editor's text) sits inside the face at 9 px when it fits (2r - 4; for a parameter of value names, when the widest
  name fits, so it never jumps rows), and the pointer is then short (70 .. 95 % of the radius) so it clears the value;
  else the value goes on the row under the label (10 px, the hot block as flat's). Hot (just turned): the face fills
  with the element's colour, the value and pointer in `bg`. The label under the dial at size + 12, as written.
- **led**: the panel's SHAPE button. On top (10 px) its icon or its label; the icon is ONE cycle of the wave as a thin
  line, no baseline, as the panel prints it (`cr_draw.c cr_wave1`, 16 x 10: the saw a ramp to 70 % and straight down, the
  triangle one peak, the pulse one ⊓, the sine one hump, the noise a jag), in the lane's colour when its lane is active,
  else `mid` when on and `dim` when off. Under a 2 px gap the cap, 18 x 18 rounded (r 2, `surf`, a 1 px `mid` rim): a
  `black` window band (its top 8 px inside the rim) over a 1 px `line` row, the pad under it, and the LED centred in the
  window: a 5 px `red` disc when on, a 3 px `dim` dot when off. The owner (2026-10-14), with a photo of the buttons:
  "I'd like the buttons to look more like these and also have simple waveshape designs that match." The icon's drawn
  extent (its points' box) is centred over the LED, not its 16 x 10 box (the saw's drop would pull it aside).
  **Two LEDs** (`"leds": 2`, 44 x 32; only for a parameter of two values, onoff or an enum of two, and no `glyph`):
  one wide cap (30 x 18) with two LEDs at 1/4 and 3/4 of its window, the left `red` (the minimum), the right `yellow`
  (the maximum), the current one lit, the other a `dim` dot; the two names (the enum's, else `names`, else OFF / ON),
  9 px, over their LEDs, the current one in the element's colour; a cell 44 high or more prints the label under the
  cap. The panel's FILTER REV. **A pair** (`"pair": "left"` / `"right"` on two single leds that touch: the left's x + w
  is the right's x, the same y and h; `validateDesign` warns otherwise): the left's cap sits at its cell's right edge,
  the right's over the shared rim, their inner corners square, so they read as one wide cap with two windows, each
  icon over its own LED: the LFO's saw | triangle button. The inspector has LEDs (1, 2) and Pair selects. The owner:
  "Good except some buttons don't have the waveform centered, example photo. The prophet also has some buttons with 2
  LEDS: Filter REV; LFO Shape; Sample And Hold LFO." (P5 has no sample-and-hold LFO parameter: no such button.)
  **The label above or below** (`"labelAt"`, the inspector's *Label at*): above (the default; a two-LED button's
  default is below, its names being on top) or below: the cap moves up (to the cell's top + 2, or under the icon or
  the names) and the label is printed under it, 10 px bold, centred. With a glyph the icon stays on top and only an
  explicit label goes under. OSC A's SYNC is so, as the owner asked.
- **switch**: the label on top, a slot (8 px wide, 12 px a position, rounded `line` outline) with a 6 x 8 lever in the
  element's colour at the current position, the positions' names beside it (the current one in the element's colour).
- label, bar, glyph and band as flat, but a label's baseline is its box's bottom edge.

**One baseline rule** (`P5_UNDER`): a text under a graphic (a knob's dial, a led's cap) sits 12 px under its bottom
edge, and a label element's baseline is its box's bottom edge, so elements placed on the 4 px grid put their labels'
baselines on the grid and in line: a knob of size 36 at y 44, a led at y 60 with its label below (cap y + 2 .. y + 20,
label at y + 32: a 32-high cell's bottom) and a label box at y 80, 12 high, all have their baseline at 92. The owner:
"One thing I don't love is that the labels across different parameters and labels don't vertically align, picture for
example "-Shape-" is a couple pixels up off grid, same with SYNC while FREQ and PULSE W are right on the grid." (A
label above a cap, a switch's texts and a two-LED button's names are as before.)

`design/layouts/designs/p5-panel.json` is P5's whole engine in that style: eight free screens over OSC .. MOD (OSC A
with the mixer, OSC B, FILTER with the filter envelope, AMPLIFIER, LFO with its SHAPE, WHEEL-MOD, POLY-MOD, VOICE with
GLIDE), all 56
parameters placed once, KNOB 1..4 a lane, FX and MIX classic as today; every screen x lane lint clean. Since
2026-10-15 it is the owner's own (edited in the page: ENV 2 in FILT, his frames' titles, SHAPE labels, VOICE
rearranged, the filter envelope on the FILTER screen in a third lane), with frames of r 8. The wheel-mod's SOURCE MIX is `LFO:NOISE`: P5's LFO noise amount IS the Prophet's
wheel-mod source mix (LFO to noise; docs/PROPHET.md, the LFO page "Noise (its mix)"), so the WHEEL screen's "SRC MIX"
knob is bound to it and the LFO screen has no NOISE knob; the palette shows it with the hint "(wheel-mod source mix)"
(`PARAM_HINTS`). Its contact
sheet, `design/choralroot-fm1-p5-panel-screens.png`, is made by

```
node tools/layout/render_sheet.mjs design/layouts/designs/p5-panel.json design/choralroot-fm1-p5-panel-screens.png
```

(any design: every screen x lane at 2x with its caption, through the firmware's editor and renderer; it prints each
tile's lint). It was compiled into the firmware from 3y86 (0.22-beta1) until the owner's rows replaced it (3y97); the
unit's then, walked on the browser emulator: `design/choralroot-fm1-p5-panel-emulator.png`. `node
tools/layout/emu_sheet.mjs <out.png>` walks the unit's P5 editor of the day (now the rows:
`design/choralroot-fm1-p5-rows-emulator.png`).

### Cell styles (classic screens, 3y94)

The owner (2026-10-15), on the P5 panel: "the new P5 UI just feels out of place compared with the rest of the engine
UI's. With the frames around things the controls get tiny and hard to read, and also having 5 or six parameters on a
row while it matches the P5, just feels weird, so I think we can redesign this, all parameters for pages follow a up to
4 parameter per row philosophy and fill the row's width so they are big and clear, then we can use the same row
highlighting behaviour we use in the other engine's UI." and "I want to see the layout with the row selection bars like
we have elsewhere, the labels at the top like we have elsewhere, and then just have update knob and button glyphs
inside instead of the bars." Then: "Why are you building it in Firmware? I'd like mockup first, and I'll want to change
the page layouts." So the tool, not the unit, has it first:

A classic cell may carry `"style"`: how the editor draws its parameter (core.h `ENG_C_*`, the names the catalog's cell
`style` field uses): `rule` (the editor's own choice, the default), `big`, `ratio`, `harm`, `detune`, `num`, `val`,
`text`, `knob`, `bar`, `waveph` (FM TONE's), and the Prophet-5's controls drawn in the cell's middle, sized to its
glyph box (34 px high without a band or with one lane under a band, 22 with two lanes under a band; docs/EDITOR.md §9
"A cell"), the label on top, the value on the row under it ("64", "On", "Half", "1/2"), the row bars and the hot block
the editor's as on every engine. In the 34 px box they are the owner's choice from five rounds of mock-ups (3y95:
`design/p5-glyph-mockups-5.html`), in the 22 px box the first pass's small ones:

| style | the glyph (cr_screen.h, cr_draw.c `cr_p5glyph`) |
| --- | --- |
| `p5knob` | `CR_G_P5KNOB` (K6d): eleven grey ticks (the one nearest the value white and 1 px longer: a patch matches a real P5's scale), the rim in the row colour, the face its 40 %, a white pointer; no value inside |
| `p5led` | `CR_G_P5LED` (B1a): a 22 x 20 cap, the rim in the row colour, the LED window, the red LED with its halo when on, a dark dot off |
| `p5led-saw`, `p5led-tri`, `p5led-sqr` | the same with its wave's one-cycle icon (2 px, 14 x 8) 4 px over the cap: white when on, grey when off |
| `p5led2` | `CR_G_P5LED2` (R3): a 30 x 20 cap, two red LEDs, the current value's lit, the two value names (8 px) over them, the current one white |
| `p5switch` | `CR_G_P5SWITCH` (S5): a 30 x 20 cap, two LEDs counting the position (Off none, Half the left, Full both) |

The designer's inspector has *Style* per slot, beside its parameter and label (the editor's rule or one of the p5
styles); the slot shows it. `validateDesign` and `tools/gen_layout.py` accept the field and refuse a name not in the
list (the same words); since 3y97 the generator compiles classic screens of OSC .. MOD (the plan), styles and all. `packDesign` carries a column's style in the packed form ('L' 3: `srcpage srccol style llen label`;
'L' 2, without it, is still read), and `lay_load` gives it to the real editor two ways: in a plan as the screen's
`cell[2][4]` (`ce_cell_plan`), in place through a preview-only hook beside `ce_label_hook`, `cr_edit.c`
`ce_style_hook` (an edit8 cell of a deep parameter; the p5 styles). On the unit P5's rows use them (3y97).

The owner's starting point: **`design/layouts/designs/p5-rows.json`**, P5's editor as the editor's own edit8 screens:
eleven screens (OSC A, OSC B; FILTER under the filter's curve, ENV 2 · filter under the envelope, ENV 2 +; ENV 1 · amp
under the envelope, ENV 1 +; LFO, WHEEL; POLYMOD, VOICE; a screen under a curve holds one lane, drawn at full size,
3y95), up to four cells a row, each cell's style by its parameter's kind
(the 0..127 knobs, VOICES, DETUNE and PRIORITY `p5knob`; the switches `p5led`, the waves' with their icon; REV
`p5led2`; KEY TRK `p5switch`), the labels the editor's way ("Cutoff", "Env amt", "Src mix" on WHEEL: the LFO's noise
amount, the wheel-mod source mix), a wave's buttons on one row (the owner: "OSC B SAW, TRI, PULSE must all be on same
row, same with on the LFO page": OSC B Freq · Saw · Tri · Pulse, LFO Freq · Saw · Tri · Square, their other knobs on
row B), FX and MIX today's. It is a plan (the OSC group has two screens, not today's three),
so the preview packs it as one. The owner signed it off ("This layout now checks out, I updated the label text in
editor. We are good to build and get into the firmware.") and it is **P5's editor on the unit** since 3y97
(`tools/gen_layout.py` `BUILD`; "Step 4"). Its contact sheet, every screen x lane through the firmware's editor and
renderer: `design/choralroot-fm1-p5-rows-screens.png` (`node tools/layout/render_sheet.mjs
design/layouts/designs/p5-rows.json design/choralroot-fm1-p5-rows-screens.png`); the unit's, walked on the browser
emulator: `design/choralroot-fm1-p5-rows-emulator.png` (`node tools/layout/emu_sheet.mjs ..`).

### Sketch glyphs (classic screens, 3y96)

The owner: "It doesn't have to stay that way. The designer can get a sketch layer: glyph variants drawn by the page
itself in the cell's glyph box" — "Yes, lets support this". A **sketch glyph** is a cell glyph drawn by the page, not
the firmware, so a glyph design can be tried on real pages, with real values, before anyone writes C. The firmware
still draws the cell (the label, the value, the row bars, the band, the hot block); the page draws only the glyph.

- **In a design**: a classic cell's `"style": "sketch:<name>"`, or for an LED button `"sketch:<name>-saw"`, `-tri`,
  `-sqr` (its wave). The names are web/fm1sketch.js's registry: the knobs `k1` .. `k6`, `k6a` .. `k6e`; the LED
  buttons `b1` .. `b5`, `b1a` .. `b1d`; the switches `s1` .. `s6`; two LEDs `r1` .. `r3` — every variation of the
  owner's glyph mock-ups (`design/p5-glyph-mockups.html`, `-2`, `-3`, `-5`), each with its note; `k6d`, `b1a`, `s5` and
  `r3` as chosen (the firmware's `p5knob`, `p5led`, `p5switch`, `p5led2`). `validateDesign` and `tools/gen_layout.py`
  accept a known name and refuse another with the list (`style "sketch:k9" (the sketch glyphs: k1, k2, ..; a wave -saw
  -tri -sqr)`); a build refuses every sketch: "lane n knob k: not buildable yet: a sketch glyph (<name>); port it into
  the firmware to build" (the same words in both, `tests/gen_layout_test.py`).
- **In the designer**: the inspector's *Style* per slot has a "sketch" group (each LED sketch also with each wave),
  and under it the sketch's note. The screen's badge says "design: not buildable yet" with the sketch's reason; the
  lint lists a "sketch" finding per slot.
- **How it is drawn**: `packDesign` packs a sketch cell's style as `ENG_C_SKETCH` (core.h, the style after
  `ENG_C_P5SWITCH`); the preview module (`lay_load`, cr_edit.c's `CR_LAYOUT_PREVIEW` hook) gives the cell the glyph
  `CR_G_N`, which `cr_cglyph` does not draw: the cell has its label, value, bars and hot block and an empty glyph box.
  No device build has the style (the image is byte for byte the same). After `lay_render`, `designSession.render`
  (web/fm1layout.js) draws each sketch into the module's 240 x 240 RGBA at 1x with whole pixels
  (`drawSketches`), so everything that shows a design screen shows it: the live screen (painted at 2x, no smoothing),
  the contact sheet and its PNG, the screen's PNG, `tools/layout/render_sheet.mjs`. The sketch's state comes from
  `lay_state` (its new `rows`: every row's cells' fills; `ink`: the palette's tokens and the knob colours as RGB):
  `pct` the fill (0..1), `on` (pct >= 1/2), `pos` (thirds), the parameter's two value names, the row colour on the
  active lane (by slot: blue, orange, white, green), the editor's greys on a dim one. The hot cell is the firmware's
  value block; the sketch does not change.
- **Where**: the cell's glyph box as cr_draw.c's `cr_p_edit8` has it (`sketchBox`): a full-size row (no band; or one
  lane under a band at 124–224) its 34 px box at row + 26 (rows 30 / 134, or 124), the sketch centred at box + 12
  (22 px under the label's baseline, as the mock-ups; a knob at box + 18, level with the buttons' LEDs, as the
  firmware's K6d), drawn only in the cell's column between its label and its value
  (row + 19 .. row + 68); two lanes under a band (rows 124–180 / 184–240) the 22 px box at row + 15, centred at
  box + 10, drawn in row + 14 .. row + 40, the chosen four with their small forms. On edit8 screens only (a stack has
  no glyph box: the lint says so).
- **Add one**: write its function in web/fm1sketch.js on the pixel helpers (`p.fill`, `p.dot`, `p.ring`, `p.disc`,
  `p.line`, `p.text`: the mock-ups' own; a 1x 8 px face for names) and a registry line (`skEntry(name, kind, round,
  note, f)`); add the name to `SKETCH_NAMES` in tools/gen_layout.py (the test compares the lists).
- **Make it real**: when the owner picks a sketch, port its function into cr_draw.c (as `cr_p5glyph` is K6d, B1a, S5
  and R3), give it a cell style (core.h `ENG_C_*`, `CELL_STYLES` in both tools), and the designs that use the sketch
  switch to that style.
- **Tests**: web/test_layout.mjs 6 (a design with `sketch:k6d` validates, the build refuses it in the generator's
  words, it draws the same twice, its pixels differ from `p5knob`'s only in the cell's glyph area and its label, value
  and bars are the firmware's, the lint and the badge say so; all 29 sketches draw in a knob's and an LED's box, lint
  clean; an unknown name or wave refused), the Design self-test's 10th square (a slot set to `sketch:k1` changes the
  canvas, the badge, the lint and the note); tests/gen_layout_test.py (the refusal and the build's words, the names
  equal); tests/cr_draw_test.c (a cell of glyph `CR_G_N`: label and value, its glyph area empty).

**Not buildable yet** means: the design is valid and previewed, but the unit would not show it as designed, or the
generator would refuse it (its reason, word for word: `validateDesign(d, cat, {build: true})` runs
`tools/gen_layout.py`'s rules, and `tests/gen_layout_test.py` checks both say the same on 48 designs). A screen that is
"buildable today" in a design added to the generator's `BUILD` is what the unit shows.

## The viewer (step 2)

- **Left**: the engines the unit shows (FM6, FM TONE, VA, CZ-1, P5, WHEEL), then the groups (with the button that
  opens each) and every screen and lane.
- **Centre**: the live 240 x 240 at twice the size. Under it SELECT and the four knobs in their cap colours (blue,
  orange, white, green): the mouse wheel, a drag, the arrow keys or the - / + buttons turn one detent, as on the
  unit, and the screen answers as the unit's does (the value, the band, the hot cell for 800 ms). The sound picker
  lists the engine's pool; "Reload sound" undoes every turn. The lint's findings are drawn over the picture (red: a
  text off the screen or over another; yellow dashes: a text cut with an ellipsis) and listed under it; "text boxes"
  outlines every text.
- **Right**: the inspector of the selected lane: the screen's kind and band and where they come from (the engine's
  screen plan, the page titles, or the platform's pages), and per knob the label, the parameter (page and column, or
  a part parameter), the range and default, the format, the value names, the value now.
- **Contact sheet**: every screen x lane of the engine on the chosen sound, each tile with its name, its lint fault
  count and a PNG download; one PNG of the whole sheet. A click on a tile opens it.
- **Download catalog JSON**: today's built-in layout of the engine in the layout file format (generated, read-only).

## The preview module

`tools/layout/layout_web.c` includes the firmware exactly as the browser emulator does (`tools/emu/emu_fw.c`: every
source file, the emulator's HAL) with `-DCR_LAYOUT_PREVIEW`, and adds the functions below. Standalone WebAssembly, no
Emscripten runtime, about 940 KB; instantiating it and powering it on takes a few tens of milliseconds, a screen
about 2 ms.

**The instance.** Power-on with no flash file (a fresh 0xFF flash in RAM: default settings, no user sounds), then
1000 ms of the device clock, then a snapshot of the module's whole static data (about 3.1 MB: every firmware
variable, the RAM flash, the LCD). `lay_reset` copies the snapshot back, and `lay_show` and `lay_catalog` begin with
it. So nothing one call changes (a knob turn, a send, a settings write) reaches a later `lay_show`: the same calls
give the same pixels. Time is simulated and moves only inside `lay_show`, `lay_knob`, `lay_select` and `lay_step`,
by fixed amounts. The wall clock is never read (the loader answers the clock call with 0).

**How the editor is driven.** `lay_show` calls the editor's own functions, the ones the unit's gestures end in:
`cu_pool_load` and `cu_sends_to_fx` (the sound, as PRESETS and the engine picker), `cu_edit_open` (EDIT tapped),
`ce_group` (a group button tapped: once for the group, once more per screen), `ce_select` (SELECT to the lane). Not
injected inputs: reaching any engine and pool position by gestures needs OPT + PRESETS, its commit on release and a
popup to wait out, and would tie the API to today's gestures. `lay_knob` and `lay_select` do inject: detents into
the HAL's encoders, as the emulator's `web_enc`, so the step sizes, the hot cell and the redraw are the unit's own
knob code. The test checks the first choice against the second: the emulator, driven by its panel alone, shows the
same pixels.

**The API** (C functions; the names and meanings are the contract for the later steps; strings are C strings in the
module's memory, valid until the next call):

| function | answer |
| --- | --- |
| `lay_version()` | JSON: `firmware` (the version string), `api` (1), `format`, `format_version`, `engines`: per engine `index`, `name`, `params_hash`, `deep_pages`, in the unit's order |
| `lay_catalog(engine)` | JSON: the engine's layout as the unit shows it, as a layout file (below): its `pages` the engine's own (what the parameters, the blob and `params_hash` address), its `groups` the editor's screens, a compiled layout's when it has one (P5: the rows, each cell bound to its built-in (page, col), and `"layout": {"design", "pages", "screens"}`). Starts from the reset instance |
| `lay_presets(engine)` | JSON: `presets`, the names of the engine's pool (INIT, then the factory sounds); `lay_show`'s preset is an index into it |
| `lay_show(engine, preset, group, screen, lane)` | opens the editor there (all from 0; group 0 OSC 1 FILT 2 ENV 3 LFO 4 MOD 5 FX 6 MIX). Returns the screen's number of lanes, or -1 no such engine, -2 sound, -3 group, -4 screen, -5 lane. Starts from the reset instance |
| `lay_knob(k, detents)` | KNOB k (1..4) turned, + clockwise; 30 ms pass |
| `lay_select(detents)` | SELECT turned (the lane, running on across the group's screens); 30 ms pass |
| `lay_step(ms)` | the clock on with no input (a hot cell's mark ends after 800 ms) |
| `lay_render()` | pointer to 240 x 240 RGBA: the screen drawn again from scratch. The clock does not move |
| `lay_fb()` | pointer to the LCD as the firmware wrote it: 240 x 240 RGB565, big-endian (the emulator's `web_fb`) |
| `lay_lint()` | JSON for the last `lay_render`: `findings` (`kind` "off-screen", "overlap" or "ellipsis"; `text`; `box` [x0, y0, x1, y1]; `other` for an overlap) and `texts` (every text's box): `tests/cr_draw_test.c`'s rules, from the same hook (`gfx.c` `GFX_HOOK_TEXT`) |
| `lay_state()` | JSON: where the editor is and what the active lane shows now (title line, kind (`free` on a compiled layout's screen, `free` its index + 1), band, `hot`, the four cells' label, value, pct) |
| `lay_layouts(on)` | the compiled layouts (`build/gen/cr_layouts.h`: P5's rows, the unit's editor) in effect from the next `lay_show` (1, the default), or not (0: the classic derivation of the built-in pages); a loaded design of the engine (`lay_load`) shows its own pages instead. Returns the previous setting |
| `lay_catalog_builtin(engine)` | as `lay_catalog`, with the screens the editor derives from the engine's own pages (no compiled layout): the designer's in-place experiments |
| `lay_reset()` | the instance as after power-on |
| `lay_buf()`, `lay_buf_size()`, `lay_load(ptr, len)` | a packed design's classic pages for its engine (the designer, below) |
| `lay_snapshot_bytes()` | the size of the snapshot (a number for tests) |

`web/fm1layout.js` wraps these (`wrap(exports)`: `catalog`, `show`, `knob`, `render`, `lint` ..) and holds the
pure code the page and the tests share (`tiles`, `formatLayout`, `checkLayout`, `ruleNotes`, `sheetGeometry`).

**The hooks.** `firmware/src/cr_pages.c` `cp_deep()`, the one place the editor asks for an engine's pages, calls
`cp_deep_hook` when `CR_LAYOUT_PREVIEW` is defined; `cr_edit.c` `ce_label()` asks `ce_label_hook` for a design's own
label. The module's hooks return the loaded design's table and labels, else the built-in. The macro is the preview
module's alone: the device image is byte-identical with and without them (sha256 of `build/choralroot.bin` before and
after: equal), and the emulators do not define it. `lay_version`'s `api` is 2 (the design API) and `format_version` 2.

## The file format, version 1

`design/layouts/<engine>.json` (`fm6`, `fm-tone`, `va`, `cz-1`, `p5`, `wheel`), written by
`tools/layout/dump_catalogs.mjs` from `lay_catalog`. In step 2 the files are **generated and read-only**: they are
today's layouts in the future format, the seed the later steps edit. `web/test_layout.mjs` regenerates them and
compares, so they cannot rot: after a change to an engine's pages, run the dump again and commit.

```
{
  "format": "choralroot-layout",
  "version": 1,
  "generated": "...",                 a note: who wrote the file (a hand-edited file will not have it)
  "engine": {"index": 16, "name": "P5"},
  "params_hash": "f5941e824048848d",  the parameters this layout is for (below)
  "pages": [                          the engine's parameter pages, as the firmware has them (core.h eng_page_t)
    {"page": 5, "title": "FILTER", "cols": [
      {"short": "CUT", "fmt": "int", "min": 0, "max": 127, "def": 0, "names": null, "unit": null},
      ..four; null = an empty column ]}
  ],
  "page_cells": 56,
  "header": null,                     (the shared header is the title line: not part of a layout)
  "groups": [                         the editor's groups, in button order
    {"group": 1, "name": "FILT", "button": "SEL", "screens": [
      {"screen": 0,
       "kind": "edit8",               the renderer's screen kind: "edit8" (two rows under a band) or "stack"
       "type": "edit",                how the editor made it: platform, stack, columns, mixer, edit, page, expr
       "band": "filter",              the wide band: none, env, filter, dx, cz, algo, ade2
       "tall": false, "header": false,
       "plan": 3, "plan_band": "..",  only when the engine has a screen plan (FM TONE): its index, its band
       "headings": ["Wave", ..],      only on a stack: the column headings
       "pages": [5, 6],               the parameter pages behind the lanes
       "lanes": [
         {"lane": 0, "title": "FILTER", "cells": [     the title line's right text; four cells, KNOB 1..4
           {"knob": 1, "bind": "deep", "page": 5, "col": 0, "page_title": "FILTER",
            "label": "Cutoff", "short": "CUT", "fmt": "int", "min": 0, "max": 127, "def": 0,
            "names": null, "unit": null},
           ..]}
       ]}
    ]}
  ]
}
```

A cell's `bind` is `"deep"` (a column of an engine page: `page`, `col`), `"track"` (a part parameter: `param`, the
`P_*` id), `"text"` (a fixed text: the platform's MOD routes), `"dash"` or `"none"`. `label` is what the screen
shows (the long label, or the screen plan's), `short` the parameter's own. `style` is the screen plan's cell style
where there is one; `by_mode: true` says the engine may show another label or names by the sound's mode (the VA's
oscillator modes). Strings are Unicode (the firmware's Latin-1 middle dot is U+00B7).

The catalog is read at run time from the firmware's own tables through the editor's own derivation (`ce_scr`,
`ce_lanes`, `ce_view` on the engine's INIT sound); nothing is copied by hand. One rule is the catalog's own: a
column the view hides for the sound at hand (an unused MOD slot shows its source only) is listed as the page's
column.

**`params_hash`** is FNV-1a 64 over what a layout binds to: the engine's index and name, its eight EDIT descriptors
and page titles, every page (title, the four descriptors with their value names and units), the sections and the
screen plan. A layout made for one hash is for exactly those parameters; `checkLayout` refuses another. The
firmware's version is not in the file: the hash is the binding.

Not in version 1 yet, and expected to be added without breaking it: the element vocabulary of step 4, the header's
design, and the per-engine table that says which patch parameter a (page, column) is (P5's `P5_DKEY`, the VA's
`VA_MAP`, FM TONE's `QUAD_MAP`): the generator of step 1 needs it, the viewer does not.

## Tests

`node web/test_layout.mjs` (suite `layout_test` of `tests/run_all.sh`; `layout_build` builds the module, cached by
`tests/hostcc.sh`):

- the module: the version, every engine's catalog (every bound cell has a label and a range; every page cell is on
  some screen; P5 has 19 pages and 56 cells), the refusals, `lay_load`'s reserved answer;
- every screen x lane of P5, VA and FM TONE on the first factory sound: not blank, all different, the same bytes
  drawn twice and shown again later; a knob turn on every lane changes the value, the picture and the hot cell, and
  the mark ends; SELECT moves the lane and back;
- **the frames equal the emulator's.** The browser emulator's module (`build/emu-web/choralroot.wasm`: the same
  firmware without the layout API or the hook) is driven by its panel alone (GLO + PRESETS to the engine, EDIT, the
  group buttons, SELECT) through every screen x lane of the three engines, and its LCD (RGB565) is compared with
  `lay_fb` pixel for pixel: 94 frames, identical; and one more per engine after a knob turn, the hot cell included.
  P5's are its compiled rows' since 3y97 (the module's default and the catalog's: 17 screen x lane of the plan, MOD's
  EXPR after VOICE, FX and MIX: 22 frames; CUTOFF after KNOB 1 +1);
  This is the cheapest exact reference: `tests/run_cr_draw.sh`'s PPMs are drawn from mock-up states, not from the
  editor on a sound, so they have no frame for the same state;
- the lint on every screen x lane of every engine: no text off the screen or overlapping (cut texts are printed as
  notes);
- `design/layouts/*.json` are what the firmware says;
- the page, from `file://` in headless Chromium with `?selftest=1`: the page checks itself (the canvas is drawn, a
  click on a knob's + changes it, the contact sheet has every tile, the lists are filled) and paints a square per
  check, which the test reads from the screenshot;
- **the designer**: a design's JSON reads back to the same model; today's layout of every engine is a valid design;
  `validateDesign` catches each of 22 broken files (format, version, kind, params_hash, engine, header, group, screen
  type, a lane of three cells, a parameter, a band, an element type, a position, an id twice, a switch of four, an
  led glyph, a colour not a token, a lane naming nothing, a key binding); today's design loaded by `lay_load` gives
  all 22 P5 frames identical to the built-in tables'; a relabel changes only the label's pixels; a swap of Cutoff and
  Reso makes KNOB 1 turn the resonance and KNOB 2 the cutoff; an added screen packs as a plan and shows; the P5
  example renders every screen x lane lint clean and the same bytes twice, its knobs turn its parameters, SELECT runs
  across its lanes and screens, its header is the title line; `validateDesign` refuses a style that is not flat or
  p5; the p5 panel (`p5-panel.json`) valid with no warning, every screen x lane lint clean and deterministic, no
  parameter twice, all 56 placed, KNOB 1 on each of its free lanes turns its parameter and changes the picture, a
  knob's value text inside its dial, a lit led's dot the firmware's red, a saw led's one-cycle icon drawn over its cap
  and the cap's window darker than its pad, every wave icon's ink centred over its LED within 1 px, REV's two LEDs
  (yellow at 3, red at 1/2), a pair's caps touching; `validateDesign` refuses two LEDs on a 0..127 parameter and warns
  on a pair that does not touch; SYNC's label (labelAt below) under its cap; a frame at r 12 leaves its corner pixel
  the background (r 13 refused); the source mix bound once, on WHEEL, in a lane, with its hint; `pasteElements` (new
  ids, no lane, 8 px off an identical box, the parameter then twice); the "order" warning: none on the panel (its LFO
  lane 1 INIT AMT, FREQ), 0.22-beta1's LFO lane warns and still builds; the flat example's one (OSC B lane 2, over two
  rows);
- **cell styles** (3y94): today's P5 design with p5 styles on FILT 1 (Cutoff .. Env amt `p5knob`, Key trk `p5switch`,
  Rev `p5led2`, Aftertouch `p5led`) packs in place, loads, and changes FILT 1's frames only (lint clean); the
  Aftertouch cell's LED is the firmware's red when on and not when off, its value "On" / "Off"; a cell style not in the
  list is refused (`validateDesign` and the generator, the same words: the 22nd broken design); these in-place checks
  run on P5's built-in derivation (`catalogBuiltin`, `lay_layouts(0)`); the owner's `p5-rows.json`: valid, eleven
  edit8 screens in OSC .. MOD (one lane under each curve), all 20 screens x lanes drawn by the editor, lint clean,
  deterministic, every one of its 17 engine rows different from the same rows unstyled (the glyphs), OSC A's Saw on
  with its LED red, FILTER's full-size row (CUTOFF);
- the page in Design mode (`?selftest=design`): the shipped example opens, a free screen is drawn and marked (the
  flat example: not buildable), a knob turns a dial, **a scripted pointer drag from the element strip adds a knob on
  the grid** (drawn by the firmware), the export equals the model and validates, the contact sheet has today's and the
  design's tiles, the shipped p5 panel opens on the firmware's free screen and is "buildable today", a copied element
  pasted on another screen is in the export with a new id, selected, in no lane, one undo step.
- **P5's rows, designed and compiled, are one** (7, 3y97): `packDesign` of `p5-rows.json` is the compiled table byte
  for byte (826 B, the plan, against `lay_compiled`); loaded by `lay_load` (17 pages, 11 screens), every screen x lane
  and every cell turned (hot): 88 frames, each identical (`lay_render` and the LCD) to the compiled rows' (and those
  equal the browser emulator's, 3); `lay_layouts(0)` gives FILT 1 the built-in derivation back. The owner's panel
  through the preview: its 8 free screens load, 19 screen x lane CR_K_FREE, deterministic, buildable; a flat design, a
  band, an element off the grid: "not buildable" with the generator's reason; and the rows changed: a stack, a dx band,
  a part parameter on an engine page, in the generator's words. On the host: `tests/run_cr_draw.sh` (P5's rows from
  mock-up states with the generated tables: 17 screen x lane and FILTER's hot cell, lint clean, hashed in
  `tests/cr_p5rows_golden.txt`, every cell turned and cooled with a partial compose equal to a whole redraw; and in a
  second build on the panel compiled apart, `build/host/freegen`, the 22 free pictures of `tests/cr_free_golden.txt`,
  unchanged); `tests/cr_p5rows_test.c` and `tests/cr_layout_test.c` (`run_cr_tests.sh`): "Step 4" below.

`?demo=osc|classic|sheet` opens the places the screenshots show. Not tested: the wheel handler, moving and resizing
by drag, the keyboard shortcuts, import, the downloads (Export / PNG write a file the test does not read), the
autosave, any browser but Chromium.

## What the lint says about today's pages

Every screen x lane of every engine on every sound of its pool (6374 frames, 2026-10-13):

- **no text off the screen and no two texts overlapping**, on any engine;
- **CZ-1, FILT screen 1 ("DCW")**: four labels are cut with an ellipsis on every sound: "DCW key 1", "DCW key 2",
  "DCW vel 1" in the top row and "DCA key 1" in the bottom row do not fit a cell;
- **VA, OSC screen 2 (the "+" stack)**: the column heading "Morph/Shape" (two sounds) or "Shape/Density" (one
  sound) is cut when the four oscillators are in different modes;
- FM6, FM TONE, P5, WHEEL: nothing.

And against the rules: every page has the shared header, the title line (rule 1). Rule 2: by docs/EDITOR.md today's
editor uses buttons, SELECT and the knobs only, and the keys play; a design cannot bind a key.

## The five steps

1. **The generator** (done, 3y85 for free screens, 3y97 for classic ones): `tools/gen_layout.py` compiles a design into
   the build: free screens as `cr_layout.h`'s records, classic screens of OSC .. MOD as a screen plan over pages of the
   layout's own. The engines' page tables stay theirs (they are what `get` / `set`, the blob, SysEx and the layout
   file's `params_hash` address); a design binds them by (page, column), and a classic layout's pages are views of
   them (each column a copy of the built-in descriptor and its built-in address), so reproducing an engine's page
   tables from a layout file was not needed: the layout's own tables are generated instead.
2. **This viewer.** The preview module, the read-only page, the seed files, the tests.
3. **Editing classic pages, and export** (done): the designer; `lay_load` shows the edited layout at once through
   the real editor; export the design JSON.
4. **Free pages** (done, 3y84 .. 3y87), **and classic designs in the firmware** (done, 3y97): the element vocabulary
   (frame, label, knob, switch, led, bar, glyph, band), lanes of up to four bound elements (the four knobs + SELECT
   grammar; no key ever: rule 2), the title line as the shared header on every screen (rule 1), "not buildable yet"
   flags instead of refusals (rule 3), the p5 style; the firmware's free screens (CE_S_FREE, CR_K_FREE); the cell
   styles and the plan form compiled; the designer on the firmware's own renderer. P5's editor on the unit is the
   owner's rows (`p5-rows.json`), from 0.22-beta1 until then his panel (`p5-panel.json`).
5. **The other engines** (what remains): FM6, FM TONE, VA, CZ-1 and WHEEL keep their classic pages. A design for one
   of them is drawn by the firmware in the designer today; to put it on the unit: its layout file's `params_hash`, a
   line in the generator's `BUILD` (one design an engine; the generator checks it against
   `design/layouts/<engine>.json`), and screens the generator builds: classic screens of OSC .. MOD as the plan has
   them (edit8 of one or two lanes, a band of none, env, filter, algo, ade2; no stack, no dx or cz band: VA's
   oscillator stacks, FM6's operators and CZ-1's envelopes would need the plan to grow those), or free screens in the
   p5 style (their code then comes back into the unit, about 6.5 KB); FX and MIX as today. FM TONE has a plan of its
   own already (`QUAD_SCREENS`); a design of it would replace that, through the same `cp_deep`.

In every step the layout is compiled into the build (rule 4).

## Step 4, the firmware: what was built

### P5's rows: a classic design compiled (3y97, lay-7)

The owner, 2026-10-15, on `design/layouts/designs/p5-rows.json`: "This layout now checks out, I updated the label
text in editor. We are good to build and get into the firmware." Eleven edit8 screens over OSC .. MOD (17 lanes, all
56 parameters once, the p5 cell styles, the owner's labels), one lane under a band, FX and MIX the platform's.

- **The generator** (`tools/gen_layout.py`): `BUILD = {"P5": "design/layouts/designs/p5-rows.json"}`. A design's
  classic screens of OSC .. MOD compile to exactly what `packDesign`'s plan packs (`pack_classic`; with free screens
  too, the built-in pages copied after the plan's, fenced off by a section, as packDesign does): `build/gen/
  cr_layouts.h` gets `CR_LAYC` (an engine's layout: its pages, screens, sections and design file, `cr_layout.h
  cr_layc_t`), `CR_LAYC_PAGES` (core.h `eng_page_t`: a lane a page, the lane's title, per column a copy of the
  built-in descriptor, generated from the layout file's descriptor), `CR_LAYC_SRC` (each column's built-in (page,
  col)) and `CR_LAYC_SCREENS` (core.h `eng_screen_t`: the group, rows A and B, the band, the title, the cells' styles
  and labels). The rules, in `validateDesign`'s words with `{build: true}`: an edit8 screen of one or two lanes, a
  plan band, no part parameter on an engine page, Latin-1 labels and lane titles, no sketch glyph, a group one kind
  (a classic screen beside free ones is refused), FX and MIX today's (their cells, no labels of their own), and the
  rest as before. A free screen stays buildable: a built design with free screens sets `CR_LAYOUTS_FREE 1`.
  `--design ENGINE=path` compiles other designs (the host tests). The header's cost line: 17 x 92 + 11 x 52 + 20 +
  strings, about 2.5 KB.
- **The firmware**: `cr_pages.c` `cp_deep`, the one place the editor (and the blob's readers) ask for an engine's
  pages, serves an engine with a compiled classic layout the layout's `eng_deep_t`: the engine's own (its blob, its
  presets) with the layout's pages, sections and screen plan, and `get` / `set` / `desc` / `mod_dst` going through to
  each column's built-in (page, col) (`desc` answers the built-in descriptor itself, so the knob's step, the ranges
  and the names are this build's). **How it is built**: the tables are const (generated); the `eng_deep_t` is one
  struct in RAM (52 B), filled on first use from the engine's own, because its function pointers and the blob's size
  are the engine's, not constants the header could name. The copied descriptors are generated from the layout file
  (which web/test_layout.mjs 4 proves to be the firmware's); `tests/cr_p5rows_test.c` checks each, field for field,
  against the engine's own (a build with another `P5_CORE` would change VOICES's range and `params_hash`: the editor
  steps by the built-in descriptor all the same). `cr_edit.c` then derives P5's screens from the plan as FM TONE's,
  with **EXPR appended after a plan's MOD screens** (as every engine has it); the cells' styles and labels come from
  the plan (`ce_cell_plan`), the bands from the plan's pages' columns (CUT RES, ATK DEC SUS REL), the knobs, SHIFT, OPT
  snap, acceleration, the hot block and the modulation marks are the edit8 screens'. The editor's trace names the
  built-in page and column. The blob, SysEx, `cr_backup`, `design/layouts/p5.json`'s pages and its `params_hash` are
  unchanged.
- **The free screens leave the unit**: `cr_layout.c` defines `CR_LAY_FREE` (a built design has free screens, or the
  preview); without it the free tables, `cr_edit.c` CE_S_FREE and `cr_draw.c` CR_K_FREE (and the 128 B of its strip
  hashes) are not compiled. **Size**: XIP 548 860 -> 546 156 B (95.7 -> 95.2 %, -2 704 B: the panel's tables and the
  free code out, the rows' tables in), RAM 79.8 -> 79.9 % (78 524 B), POOL 69.5 -> 69.4 %.
- **The tool**: `lay_catalog` lists P5 as the unit shows it: its pages the engine's 19, its screens the rows (each cell
  bound to its built-in (page, col), the screen's `pages` the built-in pages behind its cells), and `"layout"` names
  the design; `design/layouts/p5.json` is that (its pages and `params_hash` unchanged, its groups the rows and EXPR,
  FX, MIX). `lay_layouts` is on by default (the module's P5 is the unit's: the Viewer shows the rows); off, and
  `lay_catalog_builtin`, give the classic derivation of the built-in pages (the in-place experiments). `packDesign`
  packs a design as a plan when the catalog has a `"layout"`; `lay_compiled` answers a classic layout in the packed
  form, so web/test_layout.mjs compares the generator's tables with `packDesign`'s bytes. A classic p5 design in the
  plan form is "buildable today" (the badge and `validateDesign`'s build reasons follow the generator).
- **Tests**: tests/gen_layout_test.py (the rows compiled, their bytes = packDesign's, the panel's free records =
  packFree's, a mixed design's fence = packDesign's, the plan's refusals, every rule in both words); tests/cr_layout_test.c
  (the header after core.h: 17 pages, 11 screens, every column's address, the 56 once, the screens, the styles, the
  labels); tests/cr_p5rows_test.c (the editor: the copies against the engine's, the groups, EXPR after VOICE, every
  cell turned through KNOB 1..4, empty slots inert, SELECT, the blob); tests/cr_prophet_test.c (the editor's pages are
  the rows' over the engine's); tests/run_cr_draw.sh (`tests/cr_p5rows_golden.txt`, the panel's free goldens in a
  second build); web/test_layout.mjs 1, 3, 7 ("Tests" above); tools/emu/test_cr.sh and tools/emu/scripts/cr_prophet.txt
  (FILTER's KNOB 1 the cutoff, the LFO's KNOB 1 FREQ and KNOB 2 the saw's button, OSC A's row B KNOB 2 Mix A, the
  rows' screens and groups), test_persist.sh's P5 runs (the cutoff on KNOB 1). The owner's sheets:
  `design/choralroot-fm1-p5-rows-screens.png` (the design) and `design/choralroot-fm1-p5-rows-emulator.png` (the
  unit's editor on the browser emulator).

### The owner's panel: free screens compiled (3y85 .. 3y87, lay-5)

The owner, 2026-10-15, signing off his P5 panel: "I made my final changes and reviewed all the knob lanes and updated
them. I'm happy with this layout, so good to build it." Built in three stages on lay-5 (3y85, 3y86, 3y87) from a plan
written that day; the design is `design/layouts/designs/p5-panel.json`: eight free screens over OSC .. MOD (73
elements: 29 knobs, 26 leds, 10 frames, 7 labels, 1 switch; 19 lanes; all 56 P5 parameters), FX and MIX the
platform's, style p5. It was P5's editor in 0.22-beta1; the rows replaced it (3y97), and what follows describes the
free form, which the preview keeps.

### The compiled form (`tools/gen_layout.py`, `firmware/src/cr_layout.h`, `cr_layout.c`)

- **Which designs**: a list in the generator, `BUILD` (then `{"P5": "design/layouts/designs/p5-panel.json"}`), not a field of
  the design (a file the page exports must not decide what goes on a unit; rule 4). One design an engine. A step of
  `tools/build.py`'s generate list; the emulator, the web builds and the test scripts run it too (written only when
  its text changes).
- **Checked at build time** against `design/layouts/<engine>.json` (today's tables, which `web/test_layout.mjs` 4 proves
  to be the firmware's): the same `params_hash`; every stable id resolved to its built-in (page, col) (`part:<id>` to
  (0xFF, id)); validateDesign's rules and the firmware's (the grid, the area, 24 elements, 8 lanes of four, a bound
  element in exactly one lane, a parameter once, a pair touching, the p5 style, no band, no classic screen in OSC ..
  MOD, FX and MIX as today, Latin-1), in the same words as `validateDesign(d, cat, {build: true})`.
- **The records**: an element 14 B (type, x y w h, page col, size: the dial / a label's px / a glyph's `CR_G_*`, r,
  colour, flags: the led's icon, two LEDs, pair, label below, align, positions, bold, names; the label's offset), a
  screen 38 B (group, its elements, lanes `[8][4]`, the title's offset), an engine's header 8 B, one string pool; the
  texts resolved as the look draws them (capitals, defaults, the parameter's label), value names after their label.
  **P5's panel: 73 x 14 + 8 x 38 + a pool of 453 + 8 = 1787 B.** One copy, in `cr_layout.c`, with its accessors.

### The firmware

- **The editor** (`cr_edit.c` CE_S_FREE): for an engine with a compiled layout, OSC .. MOD are its screens; a lane's
  slots become the editor's refs (`CE_R_DEEP` page, col; `CE_R_TRK` a part parameter), so the knobs (step,
  acceleration, OPT snap), SHIFT, the hot mark, `cu_edited` and the blob are the edit8 screens' unchanged; an empty
  slot does nothing; MOD's last screen is still EXPR (0.22, `CE_S_EXPR`: after the layout's MOD screens, as on every
  engine; a design never holds it); FX and MIX stay the platform's. `ce_cell_free` adds the label, an led's on and a switch's position
  (`pct2`); `cr_screen_t` has `fl_eng`, `fl_scr`.
- **The renderer** (`cr_draw.c` CR_K_FREE): the title line, then the elements in the table's order (frames first): the
  rounded frame with its title in the border, the p5 knob (11 ticks, the face, the pointer, the value inside or on the
  row, the hot face), the LED button (one LED or two, pairs, the one-cycle wave icons), the lever switch, label, bar,
  glyph; the active lane in the knob colours by slot, the others dim, unbound text. Per strip only the elements whose
  rows touch it; a screen or lane change composes all six strips, a knob turn only the strips of the elements whose
  cells changed and of the hot element before and now (a hash a cell).
- **Cost**: XIP 530 500 -> 536 948 B (92.5 -> 93.6 %, +6448 with the tables), RAM the same, POOL +128. On the host
  (`tests/cr_draw_test.c`): a whole compose of FILTER 364 us against edit8's FILTER 260 us; a knob turn 2 strips,
  111 us against 89 us. Other engines' screens unchanged (their renders, the emulator's frames, test_cr).

### The tool

`packDesign` packs a design's free screens into the same records (`packFree`), `lay_load` installs them through the
preview-only `cr_layout_hook`, and the firmware's editor and renderer show them ("How a design is drawn"); `freeOps`,
`p5Op` and the draw ops went away (the captions' text stays). The design checks stay in JS. `lay_layouts` shows the
compiled layouts in the module (off by default: the catalog and the viewer list today's classic pages). Stage 3
changed no device byte (`./build.sh` before and after: sha256 of `build/choralroot.bin` ee9b2362.. both times).

### Tests (then, lay-5)

`tests/gen_layout_test.py` (the generator on the panel and 46 broken designs), `tests/cr_layout_test.c` (the header
as C), `tests/cr_p5panel_test.c` (the editor on the panel; replaced by `tests/cr_p5rows_test.c` in 3y97),
`tests/run_cr_draw.sh` (22 free pictures hashed in `tests/cr_free_golden.txt`, kept since 3y97 in the second build on
the panel compiled apart), `web/test_layout.mjs` 3 and 7 (the panel compiled against the emulator and against the
design loaded: 75 frames), `tools/emu/test_cr.sh` (the panel's screens). Today's: "P5's rows" above.

### Not done

On a unit: nothing yet (the owner tries the rows). The other engines (step 5). An in-place design (today's structure
of an engine without a compiled layout) is built as a plan, which keeps its screens but not a title-derived band or
letter: none is built today.
