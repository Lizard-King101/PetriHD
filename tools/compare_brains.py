"""Run the four brain inheritance modes on the same eight development maps."""

import argparse
import csv
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def census(runner, seed, mode, ticks):
    result = subprocess.run(
        [str(runner), "layout=development", "animals=56", f"seed={seed}",
         f"brains={mode}", f"ticks={ticks}", "every=0"],
        check=True, stdout=subprocess.PIPE, text=True,
    )
    fields = dict(word.split("=", 1) for word in result.stdout.split() if "=" in word)
    if fields.get("brains") != mode or fields.get("tick") != str(ticks):
        raise ValueError(f"Incomplete census for seed {seed}, mode {mode}: {result.stdout}")
    return {"seed": seed, **fields}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runner", type=Path, help="path to the built petrihd-run executable")
    parser.add_argument("--ticks", type=int, default=153600, help="ticks per world (default: 40 years)")
    parser.add_argument("--jobs", type=int, default=1, help="worlds to run concurrently")
    args = parser.parse_args()
    if args.ticks < 0 or args.jobs < 1:
        parser.error("ticks must be nonnegative and jobs must be positive")
    runner = args.runner.resolve()
    columns = ("seed", "brains", "tick", "animals", "walkers", "fliers", "swimmers", "young", "fed_pct",
               "large_swimmers", "small_fish", "live_hunter_parents", "swimmer_kills", "small_fish_killed",
               "own_kill_energy", "scavenged_energy", "hunter_breeders", "hunter_young", "hunter_generations",
               "last_hunter_birth", "fingerprint")
    writer = csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow(columns)
    sys.stdout.flush()
    modes = ("evolving", "frozen", "shuffled", "neutral")
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        runs = [pool.submit(census, runner, seed, mode, args.ticks)
                for seed in range(1, 9) for mode in modes]
        for run in runs:
            row = run.result()
            writer.writerow([row[key] for key in columns])
            sys.stdout.flush()


if __name__ == "__main__":
    main()
