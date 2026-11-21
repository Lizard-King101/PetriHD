"""Validate the distinct top-scoring brains from an archived Lesson 1 pond batch."""

import argparse
import csv
import subprocess
from pathlib import Path


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def write_rows(path, rows):
    with path.open("w", newline="", encoding="utf-8") as dest:
        writer = csv.DictWriter(dest, fieldnames=rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def training_score(row):
    return int(row["training_births"]), float(row["mean_timing"]), float(row["mean_feeding"])


def summary(candidate, trials):
    def mean(key):
        return sum(int(t[key]) for t in trials) / len(trials)

    row = {"pool_slot": candidate["pool_slot"], "brain": candidate["brain"],
           "previously_validated": int(bool(candidate["validation_a_births"]))}
    for panel in ("a", "b"):
        row[f"validation_{panel}_births"] = sum(int(t["birth_tick"]) > 0 for t in trials
                                                if t["panel"] == f"validation-{panel}")
    row["mean_timing"] = mean("near_drive") / 200
    row["mean_feeding"] = mean("fed_checks") / 25
    row["mean_first_ready_to_end_ticks"] = sum(int(t["end_tick"]) - int(t["first_ready_tick"])
                                               for t in trials if int(t["first_ready_tick"])) / len(trials)
    for key in ("both_ready_ticks", "both_ready_out_of_reach_ticks"):
        row[f"mean_{key}"] = mean(key)
    for role in ("candidate", "reference"):
        for key in ("only_ready_ticks", "only_ready_active", "only_ready_energy", "both_ready_energy",
                    "wait_move", "wait_call", "wait_strike", "wait_upkeep"):
            row[f"mean_{role}_{key}"] = mean(f"{role}_{key}")
        active = sum(row[f"mean_{role}_wait_{charge}"] for charge in ("move", "call", "strike"))
        row[f"mean_{role}_active"] = active
        row[f"mean_{role}_energy"] = active + row[f"mean_{role}_wait_upkeep"]
    for key in ("active", "energy"):
        row[f"mean_pair_{key}"] = row[f"mean_candidate_{key}"] + row[f"mean_reference_{key}"]
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nursery", type=Path)
    parser.add_argument("batch", type=Path, help="original pond.cfg, rankings.csv and trials.csv")
    parser.add_argument("--output", type=Path, help="defaults to the original batch directory")
    args = parser.parse_args()
    rankings = read_rows(args.batch / "rankings.csv")
    best = max(map(training_score, rankings))
    candidates = {}
    for row in rankings:
        if training_score(row) == best:
            candidates.setdefault(row["brain"], row)

    previous = [r for r in read_rows(args.batch / "trials.csv") if r["panel"] in ("validation-a", "validation-b")]
    archived = {(r["brain"], r["seed"]): r for r in previous}
    design = {(r["panel"], r["seed"], r["candidate_id"], r["reference"]) for r in previous}
    if len(design) != 8 or any(sum(d[0] == panel for d in design) != 4 for panel in ("validation-a", "validation-b")):
        raise ValueError("Expected two archived four-seed validation panels")
    trials, summaries = [], []
    checked = 0
    for brain, candidate in candidates.items():
        run = subprocess.run([str(args.nursery.resolve()), "pond-validation", brain, str(args.batch / "pond.cfg")],
                             check=True, stdout=subprocess.PIPE, text=True)
        rows = list(csv.DictReader(run.stdout.splitlines()))
        observed = {(r["panel"], r["seed"], r["candidate_id"], r["reference"]) for r in rows}
        if observed != design or len(rows) != len(design) or any(r["brain"] != brain for r in rows):
            raise ValueError(f"Validation setups differ for {brain}")
        for r in rows:
            old = archived.get((brain, r["seed"]))
            if old is not None:
                if any(r[k] != v for k, v in old.items()):
                    raise ValueError(f"Trial changed for {brain} on seed {r['seed']}")
                checked += 1
        trials.extend(rows)
        summaries.append(summary(candidate, rows))

    dest = args.output or args.batch
    dest.mkdir(parents=True, exist_ok=True)
    write_rows(dest / "validation-ties-trials.csv", trials)
    write_rows(dest / "validation-ties-summary.csv", summaries)
    print(f"{len(candidates)} brains, {len(trials)} trials, {checked} matched archived controls.")


if __name__ == "__main__":
    main()
