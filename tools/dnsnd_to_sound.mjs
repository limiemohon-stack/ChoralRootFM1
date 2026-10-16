// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// A Digitone .dnsnd -> a ChoralRoot sound file (choralroot-sound, an FM TONE sound), the host side of the device
// manager's .dnsnd import (docs/SOUNDS.md ".dnsnd import"): the notes (what was lost / approximated) are printed, then
//   python3 tools/fm1_install.py --import-sound N OUT.json      writes it into slot N
//   node tools/dnsnd_to_sound.mjs IN.dnsnd OUT.json
import { readFileSync, writeFileSync } from "node:fs";
import { parseDnsnd } from "../web/fm1dnsnd.js";
import { soundFromDnsnd } from "../web/fm1sounds.js";
import { soundToFile } from "../web/fm1manager.js";

const [src, dst] = process.argv.slice(2);
if (!src || !dst) { console.error("usage: node tools/dnsnd_to_sound.mjs IN.dnsnd OUT.json"); process.exit(2); }
let parsed;
try { parsed = await parseDnsnd(new Uint8Array(readFileSync(src))); }
catch (e) { console.error(`${src}: ${e.message}`); process.exit(1); }
const s = soundFromDnsnd(parsed), notes = [...parsed.warnings, ...s.notes];
writeFileSync(dst, JSON.stringify(soundToFile(s, { dnsnd: { name: parsed.name, tags: parsed.tags, notes } }), null, 2) + "\n");
console.log(`${s.name} (FM TONE) -> ${dst}`);
for (const n of notes) console.log(`  ${n}`);
