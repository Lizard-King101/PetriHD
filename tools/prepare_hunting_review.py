"""Build an isolated, instrumented copy; never edits simulation sources.

Run: python tools/prepare_hunting_review.py
Then: cmake --build build-hunting-review/bin
      build-hunting-review/bin/hunting-review.exe 4 20 > build-hunting-review/results.csv
      build-hunting-review/bin/hunting-review.exe arena > build-hunting-review/arenas.csv
      build-hunting-review/bin/hunting-control.exe 1 20 7101 1 0 > build-hunting-review/control.csv

The baseline's world fingerprint must match hunting-control, which has no
instrumented simulation code. Counterfactuals intentionally alter behavior.
Reruns assess the current checkout; the archived hunting-review data records
the earlier rules in its source-hashes.json manifest.
"""
from pathlib import Path
import shutil
import subprocess
import hashlib
import json

root = Path(__file__).resolve().parents[1]
work = root / 'build-hunting-review'
source = work / 'source'
for directory in ('core', 'apps'):
    shutil.copytree(root / directory, source / directory, dirs_exist_ok=True)
shutil.copytree(root / 'core', source / 'control-core', dirs_exist_ok=True)
manifest = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for directory in ('core', 'apps') for p in (root / directory).glob('*') if p.is_file()}
(work / 'source-hashes.json').write_text(json.dumps(manifest, indent=2))

hooks = r'''
#pragma once
namespace probe {
inline thread_local bool no_sprint = false, no_flight = false, reachable_sight = false, gate_strikes = false;
inline thread_local int wind_percent = 100;
struct Costs { long long basal=0, wind=0, calls=0, growth=0, healing=0, ticks=0, sprint=0, spent=0; };
inline thread_local Costs costs;
inline bool hunter(const phd::Animal& a) { return phd::diet_of(a.genome) == phd::Diet::carnivore; }
}
'''
(source / 'core/probe_hooks.hpp').write_text(hooks)
p = source / 'core/animals.cpp'
s = p.read_text()
def replace(old, new, count=1):
    global s
    assert s.count(old) == count, (old, s.count(old))
    s = s.replace(old, new)
replace('#include "world.hpp"', '#include "world.hpp"\n#include "probe_hooks.hpp"')
replace('if (f.air_speed && (a.wants & wants_fly)', 'if (!(probe::no_flight && !probe::hunter(a)) && f.air_speed && (a.wants & wants_fly)')
replace('int pace = ate ? 0 : a.pace;', '''int pace = ate ? 0 : a.pace;
        if (probe::hunter(a)) {
            ++probe::costs.ticks;
            probe::costs.sprint += pace > cruise_pace;
            probe::costs.spent += a.stamina < f.stamina / 5;
            if (probe::no_sprint) pace = std::min(pace, cruise_pace);
        }''')
replace('a.energy -= resting ? upkeep * 3 / 4 : upkeep;', '''a.energy -= resting ? upkeep * 3 / 4 : upkeep;
            if (probe::hunter(a)) probe::costs.basal += resting ? upkeep * 3 / 4 : upkeep;''')
replace('a.energy -= std::max(1, f.upkeep * gain * (resting ? wind_cost / 2 : wind_cost) / f.stamina);', '''int wind_charge = std::max(1, f.upkeep * gain * (resting ? wind_cost / 2 : wind_cost) / f.stamina);
                if (probe::hunter(a)) {
                    wind_charge = std::max(1, wind_charge * probe::wind_percent / 100);
                    probe::costs.wind += wind_charge;
                }
                a.energy -= wind_charge;''')
replace('a.energy -= 1 + f.upkeep / 16;', 'a.energy -= 1 + f.upkeep / 16;\n            if (probe::hunter(a)) probe::costs.calls += 1 + f.upkeep / 16;')
replace('a.energy -= std::max(1, f.mass75 / 16);', 'a.energy -= std::max(1, f.mass75 / 16);\n                if (probe::hunter(a)) probe::costs.growth += std::max(1, f.mass75 / 16);')
replace('a.energy -= std::max(1, f.mass75 / 64);', 'a.energy -= std::max(1, f.mass75 / 64);\n                if (probe::hunter(a)) probe::costs.healing += std::max(1, f.mass75 / 64);')
replace('if (a.stamina >= body.stamina / lunges_in_a_wind) strike(a, body);', 'if (a.stamina >= body.stamina / lunges_in_a_wind && !(probe::gate_strikes && probe::hunter(a) && bite_target(a, body.length) < 0)) strike(a, body);')
replace('if (!them.health || std::size_t(j) == index)', 'if ((probe::reachable_sight && probe::hunter(me) && them.medium != me.medium) || !them.health || std::size_t(j) == index)', count=2)
p.write_text(s)

cmake = '''cmake_minimum_required(VERSION 3.16)
project(HuntingReview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
find_package(Threads REQUIRED)
file(GLOB CORE source/core/*.cpp)
add_executable(hunting-review "{root}/tools/hunting_review.cpp" source/apps/nursery_report.cpp ${{CORE}})
target_include_directories(hunting-review PRIVATE source/core source/apps)
target_link_libraries(hunting-review PRIVATE Threads::Threads)
file(GLOB CONTROL_CORE source/control-core/*.cpp)
add_executable(hunting-control "{root}/tools/hunting_review.cpp" source/apps/nursery_report.cpp ${{CONTROL_CORE}})
target_include_directories(hunting-control PRIVATE source/control-core source/apps source/core)
target_link_libraries(hunting-control PRIVATE Threads::Threads)
'''.format(root=root.as_posix())
(work / 'CMakeLists.txt').write_text(cmake)
cache = (root / 'build/CMakeCache.txt').read_text()
args = []
for name in ('CMAKE_CXX_COMPILER', 'CMAKE_MAKE_PROGRAM'):
    value = next(line.split('=', 1)[1] for line in cache.splitlines() if line.startswith(name + ':'))
    args.append(f'-D{name}={value}')
subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'bin'), '-G', 'Ninja', *args], check=True)
