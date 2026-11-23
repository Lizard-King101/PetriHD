#include "nursery_report.hpp"

namespace nursery {
namespace {

// Kept in pieces: some compilers limit the length of one string literal.
const char* const head = R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PetriHD nursery</title>
)PAGE";

const char* const style = R"PAGE(<style>
:root{color-scheme:light;--surface:#fcfcfb;--panel:#ffffff;--line:#e4e3de;--text:#0b0b0b;--text2:#52514e;--muted:#8a8982;
--s1:#2a78d6;--s2:#eb6834;--s3:#1baf7a;--s4:#eda100;--good:#008300;--warn:#b45f00;--idle:#c9c8c1;--up:#e7f4e7;--down:#fbeedd}
@media (prefers-color-scheme:dark){:root{color-scheme:dark;--surface:#1a1a19;--panel:#222221;--line:#3a3a37;--text:#f0efec;--text2:#c3c2b7;--muted:#8f8e86;
--s1:#3987e5;--s2:#d95926;--s3:#199e70;--s4:#c98500;--good:#0ca30c;--warn:#e0913a;--idle:#4a4a46;--up:#18301a;--down:#3a2a14}}
*{box-sizing:border-box}
body{margin:0;background:var(--surface);color:var(--text);font:14px/1.45 system-ui,"Segoe UI",sans-serif}
main{max-width:1100px;margin:0 auto;padding:24px 16px 64px}
h1{font-size:22px;margin:0 0 4px}h2{font-size:17px;margin:0;text-transform:capitalize}h3{font-size:13px;margin:18px 0 6px;color:var(--text2);font-weight:600}
p{margin:0 0 8px;color:var(--text2)}
.stats{display:flex;flex-wrap:wrap;gap:8px 28px;margin:14px 0 22px}
.stat b{display:block;font-size:20px;font-weight:600}.stat span{color:var(--text2);font-size:12px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:16px;margin:0 0 16px}
.cardhead{display:flex;flex-wrap:wrap;align-items:baseline;gap:6px 14px;margin-bottom:10px}
.cardhead span{color:var(--text2)}
table{border-collapse:collapse;width:100%;font-size:13px}
th,td{text-align:left;padding:5px 8px;border-bottom:1px solid var(--line);vertical-align:top}
th{color:var(--text2);font-weight:600}td.n,th.n{text-align:right;font-variant-numeric:tabular-nums}
.scroll{overflow-x:auto}
.ladder td{white-space:nowrap}
.step{display:inline-block;min-width:74px;padding:2px 7px;border-radius:5px;border:1px solid var(--line);font-size:12px;margin-right:2px}
.step.done{border-color:var(--good)}.step.now{border-color:var(--s1)}.step.stuck{border-color:var(--warn)}.step.todo{color:var(--muted)}
.key{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px}
.legend{display:flex;flex-wrap:wrap;gap:4px 16px;font-size:12px;color:var(--text2);margin:2px 0 4px}
svg{display:block;width:100%;height:auto;overflow:visible}
svg text{fill:var(--text2);font-size:11px}
.tip{position:fixed;pointer-events:none;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:6px 9px;font-size:12px;box-shadow:0 2px 8px rgba(0,0,0,.15);display:none;z-index:5}
.note{font-size:12px;color:var(--muted)}
.card table.report{min-width:820px}
.report th,.report td{border-bottom:1px solid var(--line);padding:8px 6px}
.report th.lv{font-weight:600;color:var(--text);min-width:78px;white-space:nowrap}.report th.lv small{display:block;font-weight:400;color:var(--muted)}
.report td.who b{display:block;text-transform:capitalize;font-size:15px}.report td.who span{color:var(--text2);font-size:12px}
.cell{border-radius:7px;padding:6px 7px;border:2px solid transparent}
.cell.up{background:var(--up)}.cell.down{background:var(--down)}.cell.now{border-color:var(--s1)}
.pips{display:flex;gap:3px;margin-bottom:4px}
.pip{width:12px;height:12px;border-radius:3px;background:var(--idle)}.pip.on{background:var(--text)}
.cell .v{font-weight:600;font-variant-numeric:tabular-nums}.cell .d{font-size:12px;margin-left:4px}
.cell.up .d{color:var(--good)}.cell.down .d{color:var(--warn)}
.cell .u{display:block;font-size:11px;color:var(--text2);white-space:nowrap}.cell .t{display:block;font-size:11px;color:var(--s1);font-weight:600}
.charts{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,430px),1fr));gap:14px 22px}
.charts h3{margin:0 0 2px;text-transform:capitalize;color:var(--text);font-size:14px}.charts h3 span{color:var(--text2);font-weight:400;text-transform:none}
details{margin:0 0 10px}summary{cursor:pointer;font-weight:600;padding:6px 0;text-transform:capitalize}
</style>
)PAGE";

const char* const script_a = R"PAGE(<script>
const D = JSON.parse(document.getElementById('data').textContent);
const pct = v => (v / 10).toFixed(0) + '%';
const el = (tag, attrs, text) => { const e = document.createElement(tag); for (const k in attrs || {}) e.setAttribute(k, attrs[k]); if (text != null) e.textContent = text; return e; };
const svgEl = (tag, attrs, text) => { const e = document.createElementNS('http://www.w3.org/2000/svg', tag); for (const k in attrs || {}) e.setAttribute(k, attrs[k]); if (text != null) e.textContent = text; return e; };
const colors = ['var(--s1)', 'var(--s2)', 'var(--s3)', 'var(--s4)'];
const tip = el('div', {class: 'tip'}); document.body.appendChild(tip);
const main = document.querySelector('main');

function time(s) { return s < 90 ? s + ' s' : s < 5400 ? Math.round(s / 60) + ' min' : (s / 3600).toFixed(1) + ' h'; }
function gateText(g) {
  const parts = [];
  if (g.fed) parts.push('fed on ' + g.fed + '% of passes');
  if (g.none_starve) parts.push('no founder starves');
  if (g.births) parts.push('at least ' + g.births + ' birth' + (g.births > 1 ? 's' : ''));
  if (g.recruits) parts.push(g.recruits + ' young grown up');
  if (g.generation >= 2) parts.push('grandchildren');
  return parts.join(', ') + ', on ' + g.seeds + ' of ' + D.gate_seeds + ' unseen seeds';
}

const L = D.levels.length, S = D.gate_seeds;
const examOf = r => r.slice(10, 10 + L);
const sum = a => a.reduce((x, y) => x + y, 0);
const levelsPassed = e => e.filter((v, l) => v >= D.levels[l].gate.seeds).length;
// The family that speaks for a kind: the one furthest up the ladder, then the one doing best in the exam.
function lead(kind) {
  let best = null;
  for (const f of kind.families) { if (!f.log.length) continue;
    const e = sum(examOf(f.log[f.log.length - 1]));
    if (!best || f.level > best.f.level || (f.level === best.f.level && e > best.e)) best = {f, e}; }
  return best && best.f;
}
const generations = kind => Math.max(0, ...kind.families.map(f => f.log.length));

function header() {
  main.appendChild(el('h1', {}, 'PetriHD nursery'));
  main.appendChild(el('p', {}, D.running ? 'Training. This page reloads itself every few seconds.' : 'Training finished.'));
  const stats = el('div', {class: 'stats'});
  const rounds = D.kinds.reduce((n, k) => n + k.families.reduce((m, f) => m + f.log.length, 0), 0);
  const through = D.kinds.filter(k => { const f = lead(k); return f && levelsPassed(examOf(f.log[f.log.length - 1])) === L; }).length;
  for (const [value, label] of [[through + ' of ' + D.kinds.length, 'kinds passing every level of the exam'], [rounds.toLocaleString(), 'generations bred, all families'],
      [D.trials.toLocaleString(), 'arena trials run'], [time(D.elapsed), 'elapsed on ' + D.jobs + ' threads']]) {
    const s = el('div', {class: 'stat'}); s.appendChild(el('b', {}, value)); s.appendChild(el('span', {}, label)); stats.appendChild(s);
  }
  main.appendChild(stats);
}

function pips(n) { const p = el('div', {class: 'pips'}); for (let i = 0; i < S; ++i) p.appendChild(el('span', {class: 'pip' + (i < n ? ' on' : '')})); return p; }

// Kinds down, levels across: what each kind's best brain can do now, against the untrained brain.
function reportCard() {
  const card = el('section', {class: 'card'});
  card.appendChild(el('div', {class: 'cardhead'})).appendChild(el('h2', {}, 'Where each kind stands'));
  card.appendChild(el('p', {}, 'The exam: each generation, a family’s best brain is tried at every level in ' + S + ' arenas it was never bred in. ' +
    'Each square is an arena passed. Green cells are levels breeding has won; amber cells are levels it has lost. The outlined cell is the level being bred at now.'));
  const t = el('table', {class: 'report'}); const head = el('tr'); head.appendChild(el('th', {}, 'Kind'));
  D.levels.forEach((lv, l) => { const th = el('th', {class: 'lv', title: lv.task}, (l + 1) + ' ' + lv.name); th.appendChild(el('small', {}, 'needs ' + lv.gate.seeds + ' of ' + S)); head.appendChild(th); });
  head.appendChild(el('th', {}, 'Bred for')); t.appendChild(head);
  D.kinds.forEach(kind => {
    const f = lead(kind), now = f ? examOf(f.log[f.log.length - 1]) : null, hand = kind.hand_exam;
    const row = el('tr'); const who = el('td', {class: 'who'}); who.appendChild(el('b', {}, kind.name));
    who.appendChild(el('span', {}, now ? 'passes ' + levelsPassed(now) + ' of ' + L + ' levels; untrained ' + levelsPassed(hand)
      : hand.length ? 'untrained passes ' + levelsPassed(hand) + ' of ' + L : 'waiting')); row.appendChild(who);
    D.levels.forEach((lv, l) => {
      const td = el('td'); const u = hand[l], v = now ? now[l] : u; if (v == null) { row.appendChild(td); return; }
      const here = f && f.status === 'training' && f.level === l;
      const cell = el('div', {class: 'cell' + (now && v > u ? ' up' : now && v < u ? ' down' : '') + (here ? ' now' : '')});
      cell.appendChild(pips(v));
      cell.appendChild(el('span', {class: 'v'}, v + '/' + S + (v >= lv.gate.seeds ? ' ✓' : '')));
      if (now && v !== u) cell.appendChild(el('span', {class: 'd'}, (v > u ? '+' : '−') + Math.abs(v - u)));
      if (now && v !== u) cell.appendChild(el('span', {class: 'u'}, 'untrained ' + u + '/' + S));
      if (here) cell.appendChild(el('span', {class: 't'}, 'breeding here'));
      td.appendChild(cell); row.appendChild(td);
    });
    const stalled = kind.families.filter(x => x.status === 'stalled').length;
    const bred = el('td', {class: 'who'}); bred.appendChild(el('b', {}, generations(kind)));
    bred.appendChild(el('span', {}, 'generations' + (!f ? '' : kind.families.every(x => x.status === 'finished' || x.status === 'stalled') ? '; done' + (stalled ? ', ' + stalled + ' of ' + kind.families.length + ' families stalled' : '')
      : '; family ' + (f.id + 1) + ' leads'))); row.appendChild(bred);
    t.appendChild(row);
  });
  const wrap = el('div', {class: 'scroll'}); wrap.appendChild(t); card.appendChild(wrap);
  return card;
}

function chart(kind, n) {
  // One line per family: arenas of the exam its best brain passed, each generation.
  const fams = kind.families.filter(f => f.log.length);
  const top = L * S, base = sum(kind.hand_exam);
  const W = 520, H = 190, Lm = 30, R = 62, T = 10, B = 22;
  const x = i => Lm + (n < 2 ? 0 : i * (W - Lm - R) / (n - 1)), y = v => T + (1 - v / top) * (H - T - B);
  const svg = svgEl('svg', {viewBox: '0 0 ' + W + ' ' + H, role: 'img', 'aria-label': 'Exam arenas passed each generation by every ' + kind.name + ' family'});
  for (const g of [0, Math.round(top / 2), top]) {
    svg.appendChild(svgEl('line', {x1: Lm, x2: W - R, y1: y(g), y2: y(g), stroke: 'var(--line)', 'stroke-width': 1}));
    svg.appendChild(svgEl('text', {x: Lm - 6, y: y(g) + 4, 'text-anchor': 'end'}, g));
  }
  svg.appendChild(svgEl('text', {x: Lm, y: H - 4}, 'generation 1'));
  svg.appendChild(svgEl('text', {x: W - R, y: H - 4, 'text-anchor': 'end'}, n));
  svg.appendChild(svgEl('line', {x1: Lm, x2: W - R, y1: y(base), y2: y(base), stroke: 'var(--muted)', 'stroke-width': 1.5, 'stroke-dasharray': '5 4'}));
  svg.appendChild(svgEl('text', {x: W - R + 6, y: y(base) + 4}, 'untrained'));
  fams.forEach(f => {
    const c = colors[f.id % colors.length];
    let d = '';
    f.log.forEach((r, i) => { d += (i ? 'L' : 'M') + x(i).toFixed(1) + ' ' + y(sum(examOf(r))).toFixed(1); });
    svg.appendChild(svgEl('path', {d, fill: 'none', stroke: c, 'stroke-width': 2, 'stroke-linejoin': 'round'}));
    const last = f.log.length - 1;
    svg.appendChild(svgEl('circle', {cx: x(last), cy: y(sum(examOf(f.log[last]))), r: 4, fill: c, stroke: 'var(--panel)', 'stroke-width': 2}));
  });
  const cross = svgEl('line', {y1: T, y2: H - B, stroke: 'var(--muted)', 'stroke-width': 1, visibility: 'hidden'});
  svg.appendChild(cross);
  svg.addEventListener('mousemove', e => {
    const box = svg.getBoundingClientRect();
    const i = Math.max(0, Math.min(n - 1, Math.round(((e.clientX - box.left) * W / box.width - Lm) / ((W - Lm - R) / Math.max(1, n - 1)))));
    cross.setAttribute('x1', x(i)); cross.setAttribute('x2', x(i)); cross.setAttribute('visibility', 'visible');
    tip.textContent = '';
    tip.appendChild(el('b', {}, 'Generation ' + (i + 1)));
    fams.forEach(f => {
      const r = f.log[i]; if (!r) return;
      const row = el('div'); const k = el('span', {class: 'key'}); k.style.background = colors[f.id % colors.length]; row.appendChild(k);
      row.appendChild(document.createTextNode('family ' + (f.id + 1) + ': ' + sum(examOf(r)) + ' of ' + top + ', breeding at ' + D.levels[r[0]].name));
      tip.appendChild(row);
    });
    tip.appendChild(el('div', {}, 'untrained: ' + base + ' of ' + top));
    tip.style.display = 'block';
    tip.style.left = Math.min(window.innerWidth - tip.offsetWidth - 8, e.clientX + 14) + 'px'; tip.style.top = (e.clientY + 14) + 'px';
  });
  svg.addEventListener('mouseleave', () => { tip.style.display = 'none'; cross.setAttribute('visibility', 'hidden'); });
  const wrap = el('div');
  const f = lead(kind); const h = el('h3', {}, kind.name + ' ');
  h.appendChild(el('span', {}, f ? sum(examOf(f.log[f.log.length - 1])) + ' of ' + top + ' now, ' + base + ' untrained' : 'no generations yet'));
  wrap.appendChild(h); wrap.appendChild(svg);
  return wrap;
}

function chartsCard() {
  const card = el('section', {class: 'card'});
  card.appendChild(el('div', {class: 'cardhead'})).appendChild(el('h2', {}, 'Over the generations'));
  card.appendChild(el('p', {}, 'Exam arenas passed by each family’s best brain, out of ' + (L * S) + ' (' + L + ' levels, ' + S + ' arenas each). ' +
    'A line that rises is a family getting better at the whole ladder; a line that falls has lost something an earlier level taught it. The dashed line is the untrained brain.'));
  const legend = el('div', {class: 'legend'});
  const most = Math.max(0, ...D.kinds.map(k => k.families.length));
  for (let i = 0; i < most; ++i) { const item = el('span'); const k = el('span', {class: 'key'}); k.style.background = colors[i % colors.length]; item.appendChild(k);
    const starts = D.kinds[0].families[i]; item.appendChild(document.createTextNode('family ' + (i + 1) + (starts ? ' (' + starts.start + ' start)' : ''))); legend.appendChild(item); }
  card.appendChild(legend);
  const n = Math.max(2, ...D.kinds.map(generations));
  const grid = el('div', {class: 'charts'});
  D.kinds.forEach(k => grid.appendChild(chart(k, n)));
  card.appendChild(grid);
  return card;
}
</script>
)PAGE";

const char* const script_b = R"PAGE(<script>
function ladder(kind) {
  const t = el('table', {class: 'ladder'});
  const head = el('tr'); head.appendChild(el('th', {}, 'Family')); head.appendChild(el('th', {}, 'Ladder')); head.appendChild(el('th', {}, 'Now')); t.appendChild(head);
  const hand = el('tr'); hand.appendChild(el('td', {}, 'hand-wired brain'));
  const hs = el('td');
  kind.levels.forEach((lv, l) => { const h = lv.hand; const ok = h && h.passed >= D.levels[l].gate.seeds;
    hs.appendChild(el('span', {class: 'step ' + (h ? (ok ? 'done' : 'stuck') : 'todo'), title: h ? 'passed ' + h.passed + ' of ' + D.gate_seeds + ' gate seeds' : ''},
      D.levels[l].name + (h ? (ok ? ' ✓ ' : ' ✗ ') + h.passed + '/' + D.gate_seeds : ''))); });
  hand.appendChild(hs); hand.appendChild(el('td', {}, 'untrained baseline')); t.appendChild(hand);
  kind.families.forEach(f => {
    const row = el('tr');
    const name = el('td'); const k = el('span', {class: 'key'}); k.style.background = colors[f.id % colors.length]; name.appendChild(k);
    name.appendChild(document.createTextNode('family ' + (f.id + 1) + ', ' + f.start + ' start' + (kind.champion === f.id ? ' ★' : ''))); row.appendChild(name);
    const steps = el('td');
    D.levels.forEach((lv, l) => {
      const state = f.passed[l] ? 'done' : l === f.level ? (f.status === 'stalled' ? 'stuck' : f.status === 'training' ? 'now' : 'todo') : 'todo';
      steps.appendChild(el('span', {class: 'step ' + state}, lv.name + (state === 'done' ? ' ✓ ' + f.passed[l] : state === 'stuck' ? ' ✗' : state === 'now' ? ' …' : '')));
    });
    row.appendChild(steps);
    const rounds = f.log.filter(r => r[0] === f.level).length;
    row.appendChild(el('td', {}, f.status === 'finished' ? 'through every level' : f.status === 'stalled' ? 'stalled after ' + rounds + ' rounds, held up at: ' + f.held
      : f.status === 'waiting' ? 'waiting its turn' : D.levels[f.level].name + ', round ' + (rounds + 1)));
    t.appendChild(row);
  });
  const wrap = el('div', {class: 'scroll'}); wrap.appendChild(t); return wrap;
}

function terms(kind) {
  // The five parts of the score for each family's best brain in its latest round, beside the hand-wired brain at the same level.
  const t = el('table');
  const head = el('tr');
  for (const [h, n] of [['Best brain of', 0], ['Level', 0], ['Fed', 1], ['Ready', 1], ['Bred', 1], ['Grown up', 1], ['Lasting', 1], ['Score', 1]]) head.appendChild(el('th', n ? {class: 'n'} : {}, h));
  t.appendChild(head);
  const add = (who, level, v, score) => { const r = el('tr'); r.appendChild(el('td', {}, who)); r.appendChild(el('td', {}, level));
    for (const x of v) r.appendChild(el('td', {class: 'n'}, pct(x))); r.appendChild(el('td', {class: 'n'}, (score * 100 / D.full).toFixed(1) + '%')); t.appendChild(r); };
  kind.families.forEach(f => { if (!f.log.length) return; const r = f.log[f.log.length - 1];
    add('family ' + (f.id + 1), D.levels[r[0]].name, [r[4], r[5], r[6], r[7], r[8]], r[2]);
    const h = kind.levels[r[0]].hand; if (h && f.id === 0) add('hand-wired', D.levels[r[0]].name, [h.fed, h.ready, h.bred, h.recruited, h.sustained], h.score); });
  const wrap = el('div', {class: 'scroll'}); wrap.appendChild(t); return wrap;
}

function kindCard(kind) {
  const fold = el('details', {id: 'kind-' + kind.name});
  fold.appendChild(el('summary', {}, kind.name + ': each family on the ladder'));
  fold.appendChild(el('p', {}, kind.champion < 0 ? 'No trained brains yet: founders use the hand-wired brain.'
    : 'The founders file holds family ' + (kind.champion + 1) + ', through ' + D.levels[kind.champion_level - 1].name + ' (★).'));
  fold.appendChild(ladder(kind));
  fold.appendChild(el('h3', {}, 'What each family’s best brain manages at the level it is on, as the nursery scores it'));
  fold.appendChild(terms(kind));
  return fold;
}

function levelsCard() {
  const card = el('details', {id: 'levels'});
  card.appendChild(el('summary', {}, 'The levels: arenas, food and gates'));
  card.appendChild(el('p', {}, 'Each level is an arena set by these dials. A family moves up when ' + D.needed + ' of its best ' + D.checked + ' brains pass the gate.'));
  const t = el('table'); const head = el('tr');
  for (const h of ['Level', 'Task', 'Arena', 'Founders', 'Food', 'Gate']) head.appendChild(el('th', {}, h)); t.appendChild(head);
  D.levels.forEach((lv, l) => { const r = el('tr');
    r.appendChild(el('td', {}, (l + 1) + ' ' + lv.name)); r.appendChild(el('td', {}, lv.task));
    r.appendChild(el('td', {}, lv.width + ' × ' + lv.height + ' cells' + (lv.hazard ? ', ' + lv.hazard + ' columns to keep off' : '') + (lv.days ? ', day and night' : '') + (lv.company ? ', shared with other kinds' : '')));
    r.appendChild(el('td', {}, lv.founders + ' at ' + lv.start + '% energy'));
    r.appendChild(el('td', {}, (lv.patches ? lv.patches + ' patches of ' + lv.patch + ' × ' + lv.patch : 'every cell') + ', ' +
      (lv.real ? (lv.real / 10) + '× what a real cell holds, ' : lv.bites + ' mouthfuls a cell, ') +
      (lv.plenty ? 'growing ' + (lv.plenty / 10) + '× what the bodies there burn' : 'refilled every tick')));
    r.appendChild(el('td', {}, gateText(lv.gate))); t.appendChild(r); });
  const wrap = el('div', {class: 'scroll'}); wrap.appendChild(t); card.appendChild(wrap);
  card.appendChild(el('h3', {}, 'Energy budget by kind: raw food grown per metabolic pass against what the founders burn at a full walk or swim'));
  const b = el('table'); const bh = el('tr'); bh.appendChild(el('th', {}, 'Kind'));
  D.levels.forEach(lv => bh.appendChild(el('th', {class: 'n'}, lv.name))); b.appendChild(bh);
  D.kinds.forEach(k => { const r = el('tr'); r.appendChild(el('td', {}, k.name));
    k.levels.forEach((lv, l) => r.appendChild(el('td', {class: 'n'}, D.levels[l].plenty ? lv.supply.toLocaleString() + ' / ' + lv.need.toLocaleString() + ', ' + lv.ticks.toLocaleString() + ' ticks'
      : 'unlimited, ' + lv.ticks.toLocaleString() + ' ticks'))); b.appendChild(r); });
  const bw = el('div', {class: 'scroll'}); bw.appendChild(b); card.appendChild(bw);
  return card;
}

header();
main.appendChild(reportCard());
main.appendChild(chartsCard());
const detail = el('section', {class: 'card'});
detail.appendChild(el('div', {class: 'cardhead'})).appendChild(el('h2', {}, 'Detail'));
D.kinds.forEach(k => detail.appendChild(kindCard(k)));
detail.appendChild(levelsCard());
main.appendChild(detail);
// Folds stay as they were left across the page's reloads.
document.querySelectorAll('details').forEach(d => {
  try { d.open = sessionStorage.getItem(d.id) === '1'; } catch (e) {}
  d.addEventListener('toggle', () => { try { sessionStorage.setItem(d.id, d.open ? '1' : '0'); } catch (e) {} });
});
main.appendChild(el('p', {class: 'note'}, 'Score terms: fed is the share of the founders’ metabolic passes spent above half energy; ready is the mating drive the second-readiest founder reached; bred and grown up are births and young reaching adulthood against the founding number; lasting is the number alive at the end against twice the founding number. A full score weights them 1, 2, 4, 8 and 16.'));
</script>
)PAGE";

} // namespace

std::string report_page(const std::string& json, bool running) {
    std::string page = head;
    if (running) page += "<meta http-equiv=\"refresh\" content=\"5\">\n";
    page += style;
    page += "</head><body><main></main>\n<script type=\"application/json\" id=\"data\">";
    page += json;
    page += "</script>\n";
    page += script_a;
    page += script_b;
    page += "</body></html>\n";
    return page;
}

} // namespace nursery
