// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Two Digitone .dnsnd files decoded and compared (docs/SOUNDS.md "Refining the table"): every byte of the 302-byte
// sound record that differs, with both values and the field of web/fm1dnsnd.js's table it belongs to (or "?").
// Export a sound, change ONE parameter on the Digitone, export again, then:
//   node tools/dnsnd_diff.mjs BEFORE.dnsnd AFTER.dnsnd
import { readFileSync } from "node:fs";
import { parseDnsnd, DNSND_FIELDS, DN_LFO_DEST_NAMES } from "../web/fm1dnsnd.js";

const [a, b] = process.argv.slice(2);
if (!a || !b) { console.error("usage: node tools/dnsnd_diff.mjs BEFORE.dnsnd AFTER.dnsnd"); process.exit(2); }
const A = await parseDnsnd(new Uint8Array(readFileSync(a))), B = await parseDnsnd(new Uint8Array(readFileSync(b)));
const fieldAt = (o) => {
  for (const [name, off, hiRes, conf] of DNSND_FIELDS) {
    if (o === off) return `${name} (${conf})`;
    if (hiRes && o === off + 1) return `${name} fine (${conf})`;
  }
  if (o >= 112 && o < 128) return "? (the slots of the LFO-only targets PITCH ALL .. AB DELAY, always 64)";
  if (o >= 270 && o < 298) return "? (changes from save to save on version 5: not a parameter)";
  return o < 4 ? "magic" : o < 8 ? "version" : o < 12 ? "tag bits" : o < 28 ? "name" : "?";
};
const destName = (o, v) => DNSND_FIELDS.some(([k, off]) => off === o && /\.dest$/.test(k)) ? ` = ${DN_LFO_DEST_NAMES[v] ?? `#${v}`}` : "";
let n = 0;
console.log(`${a}: ${JSON.stringify(A.name)} (version ${A.version})  ${b}: ${JSON.stringify(B.name)} (version ${B.version})`);
for (let o = 0; o < A.record.length; o++) {
  if (A.record[o] === B.record[o]) continue;
  n++;
  console.log(`offset ${String(o).padStart(3)}  ${String(A.record[o]).padStart(3)} -> ${String(B.record[o]).padStart(3)}  ${fieldAt(o)}${destName(o, B.record[o])}`);
}
console.log(n ? `${n} byte(s) differ` : "the two sounds are the same");
