"""Summarize a petrihd-swimmer-trace CSV into one row per large adult."""

import argparse
import csv
import itertools
import sys


def summarize(source):
    swimmers = {}
    key = lambda row: (int(row["seed"]), int(row["tick"]), row["phase"], int(row["id"]))
    for (seed, tick, phase, swimmer), group in itertools.groupby(csv.DictReader(source), key):
        rows = list(group)
        a = rows[0]
        if (seed, swimmer) not in swimmers:
            swimmers[seed, swimmer] = dict(
                seed=seed, id=swimmer, contacted_ids=set(), first_contact=None, contact_ticks=0,
                first_below_half=None, first_empty=None, passes=0, above_five_eighths=0, below_half=0,
                max_drive=0, first_ready=None, last_ready=None, ready_ticks=0, ready_and_near_ticks=0,
                peak_nearby_drive_while_ready=None, both_ready_ticks=0, both_ready_near_ticks=0,
                nearest_ready_partner_dist2=None, ready_passes=0, ready_wants_mate_passes=0,
                both_ready_near_passes=0, both_want_near_passes=0, ate_own_kill=0, offspring=0,
                death_tick=None, cause="", killer=None, last_pass=None,
            )
        record = swimmers[seed, swimmer]
        energy, capacity, drive = int(a["energy"]), int(a["capacity"]), int(a["drive"])
        record["ate_own_kill"] = max(record["ate_own_kill"], int(a["ate_own_kill"]))
        record["offspring"] = max(record["offspring"], int(a["offspring"]))
        if phase == "death":
            # Suffocation ends the turn before mating. Starvation damage follows
            # the mating pass. A lethal strike always records a killer.
            record["death_tick"] = tick
            if int(a["killer"]):
                record["cause"], record["killer"] = "attack", int(a["killer"])
            elif record["last_pass"] == tick and energy == 0:
                record["cause"] = "starvation"
            elif int(a["breath"]) == 0 and record["last_pass"] != tick:
                record["cause"] = "suffocation"
            else:
                raise ValueError(f"Unrecognized death at seed {seed}, tick {tick}, swimmer {swimmer}")
            continue

        partners = [b for b in rows if b["other_id"]]
        near = [b for b in partners if b["in_range"] == "1"]
        ready = a["ready"] == "1"
        ready_partners = [b for b in partners if b["other_ready"] == "1"]
        ready_near = [b for b in near if b["other_ready"] == "1"]
        record["max_drive"] = max(record["max_drive"], drive)
        if phase == "pass":
            record["last_pass"] = tick
            record["passes"] += 1
            record["above_five_eighths"] += energy > capacity * 5 // 8
            if energy < capacity // 2:
                record["below_half"] += 1
                if record["first_below_half"] is None:
                    record["first_below_half"] = tick
            if energy <= 0 and record["first_empty"] is None:
                record["first_empty"] = tick
            record["ready_passes"] += ready
            record["ready_wants_mate_passes"] += ready and a["wants_mate"] == "1"
            record["both_ready_near_passes"] += ready and bool(ready_near)
            record["both_want_near_passes"] += (ready and a["wants_mate"] == "1"
                                                and any(b["other_wants_mate"] == "1" for b in ready_near))
        elif phase == "sample":
            if near:
                record["contacted_ids"].update(int(b["other_id"]) for b in near)
                record["contact_ticks"] += 1
                if record["first_contact"] is None:
                    record["first_contact"] = tick
            if ready:
                if record["first_ready"] is None:
                    record["first_ready"] = tick
                record["last_ready"] = tick
                record["ready_ticks"] += 1
                if near:
                    record["ready_and_near_ticks"] += 1
                    peak = max(int(b["other_drive"]) for b in near)
                    previous = record["peak_nearby_drive_while_ready"]
                    record["peak_nearby_drive_while_ready"] = peak if previous is None else max(previous, peak)
                if ready_partners:
                    record["both_ready_ticks"] += 1
                    record["both_ready_near_ticks"] += bool(ready_near)
                    nearest = min(int(b["distance_squared"]) for b in ready_partners)
                    previous = record["nearest_ready_partner_dist2"]
                    record["nearest_ready_partner_dist2"] = nearest if previous is None else min(previous, nearest)
        else:
            raise ValueError(f"Unknown trace phase {phase}")
    for _, record in sorted(swimmers.items()):
        record["contacted_ids"] = "|".join(map(str, sorted(record["contacted_ids"])))
        del record["last_pass"]
        yield record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=argparse.FileType("r", encoding="utf-8-sig"))
    args = parser.parse_args()
    with args.trace as source:
        writer = None
        for row in summarize(source):
            if writer is None:
                writer = csv.DictWriter(sys.stdout, fieldnames=row.keys(), lineterminator="\n")
                writer.writeheader()
            writer.writerow(row)


if __name__ == "__main__":
    main()
