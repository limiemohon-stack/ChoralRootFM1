#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""web/releases.json: the versions the web installer offers, checked against the repository.

  python3 tests/releases_test.py          (from the repo root; needs nothing built, no network)

Every version has its notes in docs/releases/, a date, a tag, 4 to 12 bullets and a well-formed sha256; the list is
newest first without doubles; nothing older than the minimum (0.16: the boot stub) is offered; every version but
the newest says what going back to it means; a version with a single-core package (sha256_1core, optional: 0.16 ..
0.21 have none) lists a well-formed sha256 of its own for it; the firmware's own version (firmware/src/choralroot.c) is in the list
once it is tagged. A package found in build/scratch/packages (or CR_PACKAGES_DIR) must have the listed sha256.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<72} {'ok' if cond else 'FAIL'}")
    failed += not cond
    return cond


def num(v):
    """the order of versions: X.Y-betaN comes after X.(Y-1) and before X.Y (docs/HANDOFF.md, 'A beta release')"""
    m = re.fullmatch(r"(\d+(?:\.\d+)*)(?:-beta(\d+))?", v)
    base = tuple(int(x) for x in m.group(1).split("."))
    return (base + (0,) * (3 - len(base)), int(m.group(2)) if m.group(2) else float("inf"))


def fetch_check():
    """tools/fetch_packages.py on a made-up list, its download replaced (no network): a release with sha256_1core gets
    its choralroot-X.Y-1core.fwsc too, from the same release, kept only with the listed sha256; one without does not"""
    import importlib.util
    import shutil
    import tempfile
    spec = importlib.util.spec_from_file_location("fetch_packages", ROOT / "tools" / "fetch_packages.py")
    fp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fp)
    files = {"choralroot-9.9.fwsc": b"standard", "choralroot-9.9-1core.fwsc": b"single", "choralroot-9.8.fwsc": b"older"}
    h = lambda b: hashlib.sha256(b).hexdigest()
    tmp = Path(tempfile.mkdtemp(prefix="cr-fetch-"))
    try:
        (tmp / "web").mkdir()
        rel = [{"version": "9.9", "tag": "v9.9", "sha256": h(b"standard"), "sha256_1core": h(b"single")},
               {"version": "9.8", "tag": "v9.8", "sha256": h(b"older")}]
        (tmp / "web" / "releases.json").write_text(json.dumps({"repo": "https://example.invalid/r", "releases": rel}))
        asked = []
        fp.ROOT = tmp
        fp.download = lambda url: (asked.append(url), (files.get(url.rsplit("/", 1)[1]), "test"))[1]
        rc = fp.main(tmp / "out")
        ok(rc == 0 and sorted(p.name for p in (tmp / "out").iterdir()) == sorted(files) and
           "https://example.invalid/r/releases/download/v9.9/choralroot-9.9-1core.fwsc" in asked and len(asked) == 3,
           "fetch_packages.py: the single-core package of a release that lists it, from its release; none for 9.8")
        shutil.rmtree(tmp / "out")
        files["choralroot-9.9-1core.fwsc"] = b"tampered"
        rc = fp.main(tmp / "out")
        ok(rc == 1 and not (tmp / "out" / "choralroot-9.9-1core.fwsc").exists() and (tmp / "out" / "choralroot-9.9.fwsc").is_file(),
           "fetch_packages.py: a single-core file whose sha256 is not sha256_1core is not kept, and fails")
    finally:
        shutil.rmtree(tmp)


def main():
    data = json.loads(Path(os.environ.get("CR_RELEASES_JSON") or ROOT / "web" / "releases.json").read_text(encoding="utf-8"))
    rel = data["releases"]
    vers = [r["version"] for r in rel]
    ok(len(rel) >= 1 and all(re.fullmatch(r"\d+\.\d+(\.\d+)?(-beta\d+)?", v) for v in vers), f"versions: {', '.join(vers)}")
    ok(all(r.get("beta", False) is ("-beta" in r["version"]) for r in rel), 'a beta (X.Y-betaN) says "beta": true, no other entry does')
    ok(any(not r.get("beta") for r in rel), "at least one stable release (the installer's default)")
    ok(len(set(vers)) == len(vers) and vers == sorted(vers, key=num, reverse=True), "newest first, none twice")
    minimum = data["minimum"]["version"]
    ok(minimum == "0.16" and all(num(v) >= num(minimum) for v in vers) and "0.14" not in vers and "0.15" not in vers,
       "nothing older than 0.16 (no boot stub; the idle fault of 0.14 / 0.15)")
    ok(all(len(data["minimum"][k]) > 40 for k in ("note", "boot_safe")) and re.match(r"https://github\.com/[\w-]+/[\w-]+$", data["repo"]) is not None,
       "the minimum's note, the boot-safety note, the repository")
    for i, r in enumerate(rel):
        v = r["version"]
        notes = ROOT / "docs" / "releases" / f"{v}.md"
        ok(notes.is_file() and notes.stat().st_size > 200, f"{v}: docs/releases/{v}.md")
        try:
            d = date.fromisoformat(r["date"])
        except (KeyError, ValueError):
            d = None
        ok(d is not None and date(2026, 10, 1) <= d and (i == 0 or d <= date.fromisoformat(rel[i - 1]["date"])),
           f"{v}: a date ({r.get('date')}), not after the newer version's")
        ok(r.get("tag") == "v" + v, f"{v}: tag v{v}")
        ok(re.fullmatch(r"[0-9a-f]{64}", r.get("sha256", "")) is not None and [x["sha256"] for x in rel].count(r["sha256"]) == 1,
           f"{v}: a sha256 of its own")
        if "sha256_1core" in r:                     # optional: the single-core package (docs/DUALCORE.md "Two packages")
            all_sums = [x["sha256"] for x in rel] + [x.get("sha256_1core") for x in rel]
            ok(re.fullmatch(r"[0-9a-f]{64}", r["sha256_1core"] or "") is not None and all_sums.count(r["sha256_1core"]) == 1,
               f"{v}: the single-core package's sha256 (sha256_1core), a sha256 of its own")
        b = r.get("bullets", [])
        ok(4 <= len(b) <= 12 and all(isinstance(x, str) and 20 <= len(x) <= 240 for x in b) and bool(r.get("title")),
           f"{v}: a title and {len(b)} bullets (4 to 12, each one or two sentences)")
        ok(all(isinstance(x, str) and x for x in r.get("notes", [])), f"{v}: its notes are text")
        stable0 = next(x for x in rel if not x.get("beta"))
        if r.get("beta"):
            pass                                    # a beta: opt-in, replaced by a stable at any time; going_back optional
        elif r is stable0:
            ok("going_back" not in r, f"{v}: the newest stable release has no going-back note")
        else:
            g = r.get("going_back", {})
            ok(40 <= len(g.get("short", "")) <= 260 and len(g.get("points", [])) >= 2 and all(isinstance(x, str) and x for x in g.get("points", [])),
               f"{v}: what going back means (one sentence, and the points)")
    src = (ROOT / "firmware" / "src" / "choralroot.c").read_text(encoding="utf-8", errors="replace")
    m = re.search(r'FELUCCA_VERSION\s+"ChoralRoot ([0-9.]+(?:-beta[0-9]+)?)"', src)
    cur = m.group(1) if m else None
    tags = subprocess.run(["git", "-C", str(ROOT), "tag", "--list", "v*"], capture_output=True, text=True).stdout.split()
    if cur and "v" + cur in tags:
        ok(cur in vers, f"the firmware's version {cur} is tagged: it is in the list")
    else:
        print(f"note: the firmware reads {cur}, not tagged here: it need not be in the list yet (docs/HANDOFF.md, 'Release steps')")
    for t in [t for t in tags if num(t[1:]) >= num(minimum)]:
        ok(t[1:] in vers, f"the tag {t} is in the list")
    pk = Path(os.environ.get("CR_PACKAGES_DIR") or ROOT / "build" / "scratch" / "packages")
    for r in rel:
        for name, key in ((f"choralroot-{r['version']}.fwsc", "sha256"), (f"choralroot-{r['version']}-1core.fwsc", "sha256_1core")):
            if key not in r:
                continue
            f = pk / name
            if f.is_file():
                ok(hashlib.sha256(f.read_bytes()).hexdigest() == r[key], f"{r['version']}: {f.relative_to(ROOT) if ROOT in f.parents else f} has the listed {key}")
            else:
                print(f"skipped: {f.name} not in {pk}: its sha256 is not compared (python3 tools/fetch_packages.py)")
    ok(all(set(r) <= {"version", "date", "tag", "sha256", "sha256_1core", "title", "bullets", "notes", "going_back", "beta"} for r in rel),
       "no entry has a key the site does not know (sha256_1core: optional)")
    fetch_check()
    print("releases: ALL OK" if not failed else f"releases: {failed} FAILED")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
