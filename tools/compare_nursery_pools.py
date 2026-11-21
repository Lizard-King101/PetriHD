"""Compare three large-swimmer founder pools and their mixture on matched dev maps."""

import argparse
import csv
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nursery", type=Path)
    parser.add_argument("candidate_a", type=Path)
    parser.add_argument("candidate_b", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--years", type=int, default=3)
    parser.add_argument("--first-seed", type=int, default=7201, help="start of an eight-seed block")
    parser.add_argument("--pond", action="store_true", help="compare four swimmers with and without extra reed patches at normal drive loss")
    args = parser.parse_args()
    if args.jobs < 1 or args.years < 1:
        parser.error("jobs and years must be positive")
    command = [str(args.nursery.resolve()), "pool-trial", str(args.candidate_a.resolve()), str(args.candidate_b.resolve())]
    assignments = [("a", 0), ("b", 0), ("founder", 0)] + [("mixed", k) for k in range(4)]
    conditions = [("pond", 16), ("stocked", 16)] if args.pond else [("dev", loss) for loss in (16, 8, 4)]
    cases = [(seed, setup, loss, name, k) for seed in range(args.first_seed, args.first_seed + 8)
             for setup, loss in conditions for name, k in assignments]

    def trial(case):
        seed, setup, loss, name, rotation = case
        result = subprocess.run(command + [str(seed), str(args.years), str(loss), name, str(rotation), setup],
                                check=True, capture_output=True, text=True)
        row, = csv.DictReader(result.stdout.splitlines())
        if ((int(row["seed"]), row["setup"], int(row["loss"]), row["pool"], int(row["rotation"])) != case
                or int(row["tick"]) != args.years * 3840 or int(row["founders"]) != (4 if args.pond else 8)):
            raise ValueError(f"Incomplete or mismatched nursery trial: {row}")
        return row

    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        writer = None
        for row in workers.map(trial, cases):
            if writer is None:
                writer = csv.DictWriter(sys.stdout, fieldnames=row.keys(), lineterminator="\n")
                writer.writeheader()
            writer.writerow(row)
            sys.stdout.flush()


if __name__ == "__main__":
    main()
