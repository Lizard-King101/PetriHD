"""Summarize the diagnostic CSVs and verify the baseline control fingerprint."""
import csv
import json
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[1]
work = root / 'build-hunting-review'


def read(name):
    with (work / name).open(encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f))


def total(rows, key):
    return sum(int(r[key]) for r in rows)


def ratio(a, b):
    return round(a / b, 6) if b else None


summary = {
    'design': {
        'wild': 'Body seed 1, maps 7101-7104, 20 years; 60 carnivores introduced at year 8 where settlement succeeds.',
        'interventions': 'Applied from founding; prey_no_flight changes the whole prey ecology. Diet-selective changes are diagnostic counterfactuals.',
        'cost': 'Recorded upkeep, wind recovery, calls, growth and healing; excludes reproductive transfers and changes in reserves and carcass biomass.',
        'arena_cost_cohort': 'Costs follow animals classified carnivorous; food follows the initial hunter family and its descendants.',
        'scope': 'One body seed and existing brains, without retraining; these results do not establish generalization to other body sets.',
    },
    'arena': {}, 'wild': {},
}
arenas = read('arenas.csv')
for wet in ('0', '1'):
    rows = [r for r in arenas if r['wet'] == wet]
    meat, plants = total(rows, 'meat'), total(rows, 'plants')
    cost = sum(total(rows, key) for key in ('basal', 'wind', 'calls', 'growth', 'healing'))
    summary['arena']['water' if wet == '1' else 'land'] = {
        'trials': len(rows), 'meat': meat, 'plants': plants,
        'recorded_cost': cost,
        'plant_share_of_intake': ratio(plants, meat + plants),
        'meat_to_recorded_cost': ratio(meat, cost),
        'mean_living': total(rows, 'alive') / len(rows),
    }

wild = read('results.csv')
groups = defaultdict(list)
for row in wild:
    groups[row['variant']].append(row)
for variant, rows in groups.items():
    meat, plants = total(rows, 'meat'), total(rows, 'plants')
    cost = sum(total(rows, key) for key in ('basal', 'wind', 'calls', 'growth', 'healing'))
    summary['wild'][variant] = {
        'maps': len(rows),
        'living_y12': [int(r['carn_y12']) for r in rows],
        'living_y16': [int(r['carn_y16']) for r in rows],
        'living_y20': [int(r['carn_y20']) for r in rows],
        'births': total(rows, 'born'),
        'starved': total(rows, 'starved'),
        'suffocated': total(rows, 'suffocated'),
        'killed': total(rows, 'killed'),
        'meat_to_recorded_cost': ratio(meat, cost),
        'total_food_to_recorded_cost': ratio(meat + plants, cost),
        'wind_to_basal': ratio(total(rows, 'wind'), total(rows, 'basal')),
        'hit_share_of_lunges': ratio(total(rows, 'hits'), total(rows, 'lunges')),
        'fed_share_of_passes': ratio(total(rows, 'fed_passes'), total(rows, 'metabolic_passes')),
        'sprint_requested_share_of_ticks': ratio(total(rows, 'sprint_requested_ticks'), total(rows, 'animal_ticks')),
        'low_stamina_share_of_ticks': ratio(total(rows, 'low_stamina_ticks'), total(rows, 'animal_ticks')),
    }

for control in read('control.csv'):
    matches = [r for r in wild if r['variant'] == 'baseline'
               and r['map'] == control['map'] and r['body_seed'] == control['body_seed']]
    assert len(matches) == 1, 'Missing or ambiguous baseline control'
    baseline = matches[0]
    assert baseline['fingerprint'] == control['fingerprint'], 'Instrumentation changed the world'
    for key in control:
        if key in ('variant', 'map', 'body_seed', 'fingerprint') or key.startswith('carn_') or key in (
                'born', 'starved', 'suffocated', 'killed', 'kills', 'lunges', 'hits', 'meat', 'plants',
                'metabolic_passes', 'fed_passes'):
            assert baseline[key] == control[key], (key, baseline[key], control[key])
summary['control_verified'] = True
(work / 'summary.json').write_text(json.dumps(summary, indent=2))
print('variant                          year12       year16       year20       births   meat/cost    hit%')
for variant, r in summary['wild'].items():
    means = [sum(r[f'living_y{y}']) / r['maps'] for y in (12, 16, 20)]
    print(f'{variant:32s} {means[0]:8.2f}     {means[1]:8.2f}     {means[2]:8.2f}     '
          f'{r["births"]:6d}   {r["meat_to_recorded_cost"] or 0:8.3f} '
          f'{100 * (r["hit_share_of_lunges"] or 0):7.2f}')
print('Baseline matches the uninstrumented control, including the world fingerprint.')
