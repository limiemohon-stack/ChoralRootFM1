// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Every .dnsnd of a folder as FM TONE gets it (the device manager's import, web/fm1dnsnd.js dnsndToQuad): a line a
// sound, "name|blob hex|drive|pan", for the host tools that play them (docs/QUAD.md "Level", "Pops"):
//   node tools/dnsnd_list.mjs DIR > LIST
//   build/host/va_levels dnsnd LIST        their loudness
//   build/host/quad_pops list LIST         their pops
// A file that does not parse is named on stderr and left out.
import { readFileSync, readdirSync } from "node:fs";
import { parseDnsnd, dnsndToQuad } from "../web/fm1dnsnd.js";

const dir = process.argv[2];
if (!dir) { console.error("usage: node tools/dnsnd_list.mjs DIR"); process.exit(2); }
for (const f of readdirSync(dir).filter(f => f.endsWith(".dnsnd")).sort()) {
  try {
    const q = dnsndToQuad(await parseDnsnd(new Uint8Array(readFileSync(dir + "/" + f))));
    console.log([f.replace(/\.dnsnd$/, "").replace(/\|/g, "/"), Buffer.from(q.blob).toString("hex"), q.drive, q.pan].join("|"));
  } catch (e) { console.error(`${f}: ${e.message}`); }
}
