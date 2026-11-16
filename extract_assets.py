#!/usr/bin/env python3
"""The game's files from the user's own League of Legends installation, as
<out>/<Name>.wad/... (Rift Ascend's league-assets layout), with CDTB
(CommunityDragon's toolbox): several archives at a time, each in a CDTB of
its own. The Rift Ascend port for PeOS (docs/rift-ascend-port-plan.md).

    extract_assets.py --league "F:\\Riot Games\\League of Legends" --out G:\\league-assets \\
        --hashes build/rift/hashes.game.txt [--jobs 4] [--cdtb <cdtb executable>]

Runs where the destination disk is local: on Windows straight to a USB disk
(through WSL every file was a 9p round trip: 2.3 % CPU, 5 hours for a third,
2026-11-14). A finished archive leaves <out>/.done/<Name>.wad: a run again
skips it at once (the half-done one is taken again with --lazy). The files
without a known name are left out (-u no): the game looks files up by name.
Never in git or in an image: the user's files (AGENTS.md)."""
import argparse
import concurrent.futures
import os
import pathlib
import shutil
import subprocess
import sys
import threading
import time


def ltk_names(root):
    """A file whose path in the WAD has no extension (Global.wad's "items",
    "globals"): CDTB writes it as items.cdtb(.bin), Rift Ascend looks for
    items.ltk(.bin), the name its own extractor gives. Renamed in place."""
    renamed = 0
    for dirpath, _, files in os.walk(root):
        for f in files:
            i = f.find(".cdtb")
            if i < 0 or not (f.endswith(".cdtb") or f[i:].startswith(".cdtb.")):
                continue
            src = os.path.join(dirpath, f)
            dst = os.path.join(dirpath, f[:i] + ".ltk" + f[i + len(".cdtb"):])
            os.replace(src, dst)
            renamed += 1
    return renamed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--league", required=True, help="the League of Legends folder")
    ap.add_argument("--out", required=True, help="the league-assets folder to write")
    ap.add_argument("--hashes", required=True, help="CommunityDragon's hashes.game.txt")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--cdtb", default=shutil.which("cdtb") or "cdtb")
    ap.add_argument("--log", help="a copy of the progress lines")
    args = ap.parse_args()

    league = pathlib.Path(args.league)
    final = league / "Game" / "DATA" / "FINAL"
    if not final.is_dir():
        final = league / "DATA" / "FINAL"
    if not final.is_dir():
        print(f"extract_assets: no Game/DATA/FINAL in {league}", file=sys.stderr)
        return 1
    out = pathlib.Path(args.out)
    done_dir = out / ".done"
    done_dir.mkdir(parents=True, exist_ok=True)
    wads = sorted(final.rglob("*.wad.client"), key=lambda p: p.name.lower())
    todo = [w for w in wads if not (done_dir / w.name[: -len(".client")]).exists()]
    if not todo:   # all there: an extraction from before the renaming
        print(f"extract_assets: {ltk_names(out)} .cdtb names made .ltk")

    lock = threading.Lock()
    log = open(args.log, "a", encoding="utf-8") if args.log else None
    counter = {"n": len(wads) - len(todo), "failed": 0}
    start = time.time()

    def say(line):
        with lock:
            print(line, flush=True)
            if log:
                log.write(line + "\n")
                log.flush()

    say(f"extract_assets: {len(wads)} archives, {len(wads) - len(todo)} done already, "
        f"{len(todo)} to go, {args.jobs} at a time")

    def extract(wad):
        name = wad.name[: -len(".client")]   # Map11.wad.client -> Map11.wad
        t0 = time.time()
        r = subprocess.run([args.cdtb, "wad-extract", "--lazy", "-u", "no", "-H", args.hashes,
                            "-o", str(out / name), str(wad)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        with lock:
            counter["n"] += 1
            n = counter["n"]
        if r.returncode == 0:
            ltk_names(out / name)
            (done_dir / name).write_text("")
            say(f"extract_assets: [{n}/{len(wads)}] {name} ({time.time() - t0:.0f} s)")
        else:
            with lock:
                counter["failed"] += 1
            tail = (r.stderr or "").strip().splitlines()[-1:] or ["?"]
            say(f"extract_assets: [{n}/{len(wads)}] {name} FAILED: {tail[0]}")

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(extract, todo))

    say(f"extract_assets: done in {(time.time() - start) / 60:.0f} min, {counter['failed']} failed")
    return 1 if counter["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
