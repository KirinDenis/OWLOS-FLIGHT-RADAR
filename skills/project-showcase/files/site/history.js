/* how the radar grew, at the bottom of the page, three ways with one panel of the version beside them:
 *   TOWER   the history as a tower of time in WebGL: the parts of the product on the floor, time rising, a file a
 *           thread, a commit a bead with light to what it changed and its own words (tower.js, from history.txt)
 *   CODE    the files growing commit by commit, in the manner of Gource (growth.js, from history.txt)
 *   BOARDS  the versions on the radar's own dial: a ring a version, the first in the middle and the newest at the
 *           edge, and each board a line at a bearing of its own, from the version that brought it out to the last
 *           one it is in, its colour its chip and nothing else
 * what a version brought is CHANGES.md's (the line under its heading, then its points), the boards it had are
 * fw/versions.json's, the list of firmware the site keeps: all of it lies next to this page, so nothing here is
 * written twice and a new version draws itself. a version picked eases there as a spring does, never in a jump */

import { startGrowth } from './growth.js';
import { startTower } from './tower.js';

const NS = 'http://www.w3.org/2000/svg';
/* one colour a chip. checked for colour blind eyes: the nearest two of them stay apart under each kind, and
 * wherever a board is named its chip is said in words too */
const CHIP_COLOUR = { esp32: '#4fc8ff', c3: '#ffb848', s3: '#7dffa8', c5: '#b48cff', p4: '#ff5fa8' };
const CHIP_ORDER = ['esp32', 'c3', 's3', 'c5', 'p4'];
const SIZE = 520;
const MID = SIZE / 2;
const INNER = 46;                                      // the first version's ring: wide enough for its boards apart
const OUTER = 210;                                     // the newest's
const INTRO_S = 0.26;                                  // seconds a version while the dial first opens out
const SPRING_W = 10;                                   // the spring to a picked version: how stiff, rad/s
const SPRING_Z = 0.72;                                 // and how damped: a little past it, then back
const SWEEP_S = 6;                                     // a turn of the beam
const NEAR_PX = 8;                                     // how near a line the pointer picks its board

const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
// CHANGES.md's `code` and **bold**, the rest as text
const inline = (t) => esc(t).replace(/`([^`]+)`/g, '<code>$1</code>').replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>');
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

/* CHANGES.md: a "## " heading a version, the newest first, the line under it the version in a few words, then
 * its points, each "- " and the lines under it. the file's own words above the first heading are no version */
function readChanges(md) {
  const out = [];
  let v = null;
  let inPoint = false;
  for (const line of md.split(/\r?\n/)) {
    const h = line.match(/^## ([0-9][0-9.]*)(.*)$/);
    if (h) {
      v = { version: h[1], beta: /\(beta\)/.test(h[2]), head: '', points: [] };
      out.push(v);
      inPoint = false;
    } else if (!v) {
      continue;
    } else if (/^- /.test(line)) {
      v.points.push(line.slice(2).trim());
      inPoint = true;
    } else if (inPoint && /^\s+\S/.test(line)) {
      v.points[v.points.length - 1] += ' ' + line.trim();
    } else if (!line.trim()) {
      inPoint = false;
    } else if (!v.points.length && !v.head) {
      v.head = line.trim();
    }
  }
  return out.reverse();                                // the first version first, as the rings go
}

/* a version with no line of its own under the heading says its first point's first sentence */
function headOf(v) {
  if (v.head) {
    return v.head;
  }
  const first = (v.points[0] || '').replace(/[`*]/g, '');
  const cut = first.search(/[.:](\s|$)/);
  return cut > 0 ? first.slice(0, cut + 1) : first;
}

/* the versions with their boards. a version the site keeps no firmware of is taken to have had the boards of
 * the one before it: no board has left yet, and the line runs on through it */
function model(changes, kept) {
  const byVersion = new Map();
  for (const e of kept.versions || []) {
    byVersion.set(e.version, e);
  }
  if (kept.beta) {
    byVersion.set(kept.beta.version, kept.beta);
  }
  const first = new Map();
  const last = new Map();
  let before = [];
  const versions = changes.map((c, i) => {
    const e = byVersion.get(c.version);
    const boards = e ? e.boards.slice() : before.slice();
    for (const b of boards) {
      if (!first.has(b)) {
        first.set(b, i);
      }
      last.set(b, i);
    }
    before = boards;
    return { ...c, kept: Boolean(e), dir: e ? e.dir : '', boards };
  });
  return { versions, first, last };
}

const el = (name, attrs, parent) => {
  const e = document.createElementNS(NS, name);
  for (const k in attrs) {
    e.setAttribute(k, attrs[k]);
  }
  parent.appendChild(e);
  return e;
};

export async function startHistory(BOARDS, CHIP_NAME, desk) {
  const section = document.getElementById('history');
  if (!section) {
    return;
  }
  const get = (url, json) => fetch(url, { cache: 'no-cache' }).then((r) => (r.ok ? (json ? r.json() : r.text()) : null)).catch(() => null);
  const [md, kept, story] = await Promise.all([get('CHANGES.md'), get('fw/versions.json', true), get('history.txt')]);
  const changes = md ? readChanges(md) : [];
  if (changes.length < 2 || !kept || !kept.versions) {
    return;                                            // not on the site (a local look of site/): no history
  }
  const { versions, first, last } = model(changes, kept);
  const n = versions.length;
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const ringR = (i) => INNER + (clamp(i, 0, n - 1) * (OUTER - INNER)) / Math.max(1, n - 1);
  const chipOf = (b) => (BOARDS[b] && BOARDS[b].chip) || 'esp32';
  const nameOf = (b) => (BOARDS[b] ? BOARDS[b].name : b);
  const chipName = (c) => CHIP_NAME[c] || c;

  /* the bearings: the boards by chip, each chip's in the order they came, a slot left empty between two chips
   * and two at the top, where the versions' numbers stand */
  const boards = [...first.keys()].sort((a, b) =>
    CHIP_ORDER.indexOf(chipOf(a)) - CHIP_ORDER.indexOf(chipOf(b)) || first.get(a) - first.get(b) || a.localeCompare(b));
  const slots = [null];
  let chip = null;
  for (const b of boards) {
    if (chip !== null && chipOf(b) !== chip) {
      slots.push(null);
    }
    chip = chipOf(b);
    slots.push(b);
  }
  slots.push(null);
  const turn = (k) => ((-90 + ((k + 0.5) * 360) / slots.length) * Math.PI) / 180;
  const at = (r, a) => [MID + r * Math.cos(a), MID + r * Math.sin(a)];

  /* ---- the dial ---- */
  const box = document.getElementById('histdial');
  const scope = document.createElement('div');
  scope.className = 'histscope';
  box.appendChild(scope);
  const svg = el('svg', { viewBox: `0 0 ${SIZE} ${SIZE}`, role: 'slider', tabindex: '0', 'aria-label': 'Version',
    'aria-valuemin': '0', 'aria-valuemax': String(n - 1) }, scope);
  const defs = el('defs', {}, svg);
  // the glow: what is lit drawn twice more, blurred, under itself
  const glow = el('filter', { id: 'hglow', filterUnits: 'userSpaceOnUse', x: 0, y: 0, width: SIZE, height: SIZE }, defs);
  el('feGaussianBlur', { in: 'SourceGraphic', stdDeviation: 3.4, result: 'wide' }, glow);
  el('feGaussianBlur', { in: 'SourceGraphic', stdDeviation: 1.2, result: 'near' }, glow);
  const merge = el('feMerge', {}, glow);
  el('feMergeNode', { in: 'wide' }, merge);
  el('feMergeNode', { in: 'near' }, merge);
  el('feMergeNode', { in: 'SourceGraphic' }, merge);
  // the glass: a little light in the middle going dark to the edge
  const face = el('radialGradient', { id: 'hface' }, defs);
  el('stop', { offset: '0', 'stop-color': '#0f3561', 'stop-opacity': '.7' }, face);
  el('stop', { offset: '.65', 'stop-color': '#08203f', 'stop-opacity': '.45' }, face);
  el('stop', { offset: '1', 'stop-color': '#03081a', 'stop-opacity': '0' }, face);
  el('circle', { cx: MID, cy: MID, r: OUTER + 26, fill: 'url(#hface)' }, svg);

  for (let d = 0; d < 360; d += 5) {                   // the scope's own ring of degrees
    const a = (d * Math.PI) / 180;
    const [x1, y1] = at(OUTER + 30, a);
    const [x2, y2] = at(OUTER + (d % 30 ? 34 : 38), a);
    el('line', { x1, y1, x2, y2, class: d % 30 ? 'tick' : 'tick big' }, svg);
  }
  const rings = versions.map((v, i) => el('circle', { cx: MID, cy: MID, r: ringR(i), class: v.beta ? 'ring beta' : 'ring' }, svg));
  const lit = el('g', { filter: 'url(#hglow)' }, svg);
  const edge = el('circle', { cx: MID, cy: MID, r: INNER, class: 'edge' }, lit);
  const lines = new Map();
  slots.forEach((b, k) => {
    if (!b) {
      return;
    }
    const a = turn(k);
    const colour = CHIP_COLOUR[chipOf(b)] || CHIP_COLOUR.esp32;
    const [x, y] = at(ringR(first.get(b)), a);
    const [lx, ly] = at(OUTER + 18, a);
    // the line fades towards its past: bright where it is now, a half tone where the board came in
    const id = `hg-${b}`;
    const grad = el('linearGradient', { id, gradientUnits: 'userSpaceOnUse', x1: x, y1: y, x2: x, y2: y }, defs);
    el('stop', { offset: '0', 'stop-color': colour, 'stop-opacity': '.22' }, grad);
    el('stop', { offset: '1', 'stop-color': colour, 'stop-opacity': '1' }, grad);
    const label = el('text', { x: lx, y: ly, class: 'inch', 'text-anchor': Math.cos(a) > 0.3 ? 'start' : Math.cos(a) < -0.3 ? 'end' : 'middle',
      'dominant-baseline': 'central' }, svg);
    label.textContent = BOARDS[b] ? `${BOARDS[b].inch}"` : b;
    const line = el('line', { x1: x, y1: y, x2: x, y2: y, stroke: `url(#${id})`, class: 'board' }, lit);
    // where the beam goes over it, the line flares: the same turn as the beam, started with it, its bearing later
    const bearing = ((a * 180) / Math.PI + 90 + 360) % 360;
    const flare = el('line', { x1: x, y1: y, x2: x, y2: y, stroke: colour, class: 'flare' }, lit);
    flare.style.animationDelay = `${((bearing / 360) * SWEEP_S).toFixed(3)}s`;
    lines.set(b, {
      a,
      grad,
      line,
      flare,
      ping: el('circle', { cx: x, cy: y, r: 5, stroke: colour, class: 'ping' }, lit),
      dot: el('circle', { cx: x, cy: y, r: 0, fill: colour, class: 'dot' }, lit),
      label,
    });
  });
  for (const m of lines.values()) {
    m.ping.addEventListener('animationend', () => m.ping.classList.remove('go'));
  }
  const numbers = versions.map((v, i) => {
    // just inside its ring, between it and the one before: on the ring, the ring struck it through
    const t = el('text', { x: MID, y: MID - ringR(i) + 2, class: 'num', 'text-anchor': 'middle', 'dominant-baseline': 'hanging' }, svg);
    t.textContent = v.version;
    return t;
  });
  const every = (OUTER - INNER) / Math.max(1, n - 1) < 13 ? 2 : 1;   // too close for every number: every other one
  const beam = document.createElement('div');
  beam.className = 'histbeam';
  const pad = OUTER + 6;
  Object.assign(beam.style, { left: `${((MID - pad) / SIZE) * 100}%`, top: `${((MID - pad) / SIZE) * 100}%`,
    width: `${((2 * pad) / SIZE) * 100}%`, height: `${((2 * pad) / SIZE) * 100}%` });
  scope.appendChild(beam);
  scope.style.setProperty('--sweep', `${SWEEP_S}s`);

  const legend = document.getElementById('histlegend');
  const chips = CHIP_ORDER.filter((c) => boards.some((b) => chipOf(b) === c));
  legend.innerHTML = chips.map((c) => `<span><i style="background:${CHIP_COLOUR[c]}"></i>${esc(chipName(c))}</span>`).join('') +
    '<button class="plain" id="histplay">PLAY AGAIN</button>';
  const tip = document.createElement('div');
  tip.id = 'histtip';
  tip.hidden = true;
  box.appendChild(tip);
  const panel = document.getElementById('histpanel');

  /* the dial at a point of the history: t a version's index, or between two while it eases. everything in it
   * follows t smoothly: the edge of what is shown, the lines, the dots coming in, the rings and numbers */
  let tPrev = 0;
  function draw(t) {
    const tc = clamp(t, 0, n - 1);
    const now = Math.round(tc);
    rings.forEach((c, j) => {
      const come = clamp(tc - j + 1, 0, 1);            // fades in as the edge comes to it
      const age = 0.3 + (0.7 * j) / Math.max(1, tc);   // the older, the fainter: the half tones of the glass
      c.style.opacity = (come * clamp(age, 0.3, 1)).toFixed(3);
    });
    numbers.forEach((c, j) => {
      const come = clamp(tc - j + 1, 0, 1);
      const shown = j === now || j === 0 || (n - 1 - j) % every === 0;
      c.style.opacity = (come * (j === now ? 1 : shown ? 0.75 : 0)).toFixed(3);
      c.classList.toggle('now', j === now);
    });
    edge.setAttribute('r', ringR(tc).toFixed(2));
    for (const [b, m] of lines) {
      const f = first.get(b);
      const come = clamp((tc - f) * 4 + 1, 0, 1);       // a board shows a little before the edge reaches it
      const [x2, y2] = at(ringR(clamp(tc, f, last.get(b))), m.a);
      for (const l of [m.line, m.flare]) {
        l.setAttribute('x2', x2.toFixed(2));
        l.setAttribute('y2', y2.toFixed(2));
        l.style.visibility = come > 0 && tc >= f ? '' : 'hidden';   // a line of no length still draws its round ends
      }
      m.grad.setAttribute('x2', x2.toFixed(2));
      m.grad.setAttribute('y2', y2.toFixed(2));
      m.dot.setAttribute('r', (come * 3.4 + 1.8 * Math.max(0, 1 - Math.abs(tc - f) * 1.5)).toFixed(2));
      m.dot.style.opacity = come.toFixed(3);
      m.label.style.opacity = (come * 0.85).toFixed(3);
      if (tPrev < f && tc >= f && !still) {            // the edge just passed it: a ring goes out from it
        m.ping.classList.remove('go');
        m.ping.getBoundingClientRect();
        m.ping.classList.add('go');
      }
    }
    tPrev = tc;
  }

  /* the panel: what the version brought. its numbers count up or down to the new ones */
  let said = -1;
  let detailsOpen = false;
  const counted = { boards: 0, fresh: 0, chips: 0 };
  function count(elm, from, to, sign) {
    if (still || from === to) {
      elm.textContent = (sign && to > 0 ? '+' : '') + to;
      return;
    }
    const t0 = performance.now();
    const tick = (ms) => {
      const k = clamp((ms - t0) / 450, 0, 1);
      const e = 1 - Math.pow(1 - k, 3);
      const v = Math.round(from + (to - from) * e);
      elm.textContent = (sign && v > 0 ? '+' : '') + v;
      if (k < 1) {
        requestAnimationFrame(tick);
      }
    };
    requestAnimationFrame(tick);
  }
  /* under the versions: the parts of the product to light one alone (the tower), and a way back to everything
   * when anything was picked, turned or brought closer. a version picked again lets it go too */
  let picked = { v: -1, part: -1, moved: false };
  function renderExtra() {
    const extra = panel.querySelector('.histextra');
    if (!extra) {
      return;
    }
    let html = '';
    if (view === 'tower' && tower) {
      html += '<div class="histparts"><span>Parts</span>' +
        `<button data-part="-1" aria-pressed="${picked.part < 0}">ALL</button>` +
        tower.parts.map((p, i) => (p.here ? `<button data-part="${i}" aria-pressed="${picked.part === i}"><i style="background:${p.colour}"></i>${esc(p.name)}</button>` : '')).join('') +
        '</div>';
    }
    if (picked.v >= 0 || picked.part >= 0 || picked.moved) {
      html += '<button class="histall" data-all>SHOW ALL AGAIN</button>';
    }
    extra.innerHTML = html;
  }
  function say(i, board) {
    const v = versions[i];
    if (!v) {
      return;
    }
    const fresh = boards.filter((b) => first.get(b) === i);
    const chipCount = new Set(v.boards.map(chipOf)).size;
    const newest = i === n - 1;
    const where = !v.kept ? '<p class="histnote">The site does not keep this version\'s firmware.</p>'
      : newest ? '' : '<p class="histnote">Its firmware is kept: the <a href="previous/">previous page</a> flashes it.</p>';
    panel.innerHTML =
      `<div class="histver"><b>${esc(v.version)}</b>${v.beta ? '<em>BETA</em>' : ''}${newest ? '<span>the newest</span>' : ''}</div>` +
      `<p class="histhead">${inline(headOf(v))}</p>` +
      `<div class="histnums"><span><b class="nb"></b> boards</span><span><b class="nf"></b> new</span>` +
      `<span><b class="nc"></b> ${chipCount === 1 ? 'chip' : 'chips'}</span></div>` +
      (fresh.length ? `<ul class="histnew">${fresh.map((b) => `<li${b === board ? ' class="picked"' : ''}><i style="background:${CHIP_COLOUR[chipOf(b)]}"></i>` +
        `${esc(nameOf(b))} <small>${esc(chipName(chipOf(b)))}</small></li>`).join('')}</ul>` : '') +
      `<details${detailsOpen ? ' open' : ''}><summary>What changed: ${v.points.length} ${v.points.length === 1 ? 'point' : 'points'}</summary>` +
      `<ul>${v.points.map((p) => `<li>${inline(p)}</li>`).join('')}</ul></details>` + where +
      // every version as a button too: a thin ring is hard to hit with a finger
      `<div class="histvers">${versions.map((w, j) => `<button data-v="${j}"${j === i ? ' aria-current="true"' : ''}>${esc(w.version)}</button>`).join('')}</div>` +
      '<div class="histextra"></div>';
    renderExtra();
    count(panel.querySelector('.nb'), counted.boards, v.boards.length, false);
    count(panel.querySelector('.nf'), counted.fresh, fresh.length, true);
    count(panel.querySelector('.nc'), counted.chips, chipCount, false);
    counted.boards = v.boards.length;
    counted.fresh = fresh.length;
    counted.chips = chipCount;
    panel.querySelector('details').addEventListener('toggle', (ev) => {
      detailsOpen = ev.target.open;
    });
    said = i;
    svg.setAttribute('aria-valuenow', String(i));
    svg.setAttribute('aria-valuetext', `version ${v.version}, ${v.boards.length} boards`);
  }

  /* the dial's motion: a spring to the version picked, or the slow opening out from the first ring */
  let t = n - 1;
  let vel = 0;
  let want = n - 1;
  let intro = null;
  let raf = 0;
  let lastMs = 0;
  function run(ms) {
    const dt = Math.max(0, Math.min(0.05, (ms - lastMs) / 1000 || 0.016));   // a frame stamped before the loop woke: no step back
    lastMs = ms;
    if (intro) {
      const k = clamp((ms - intro.t0) / (Math.max(1, intro.to) * INTRO_S * 1000), 0, 1);
      const e = k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2;
      t = intro.to * e;
      const i = Math.floor(t + 1e-6);
      if (i !== said) {
        say(i);
      }
      if (k >= 1) {
        intro = null;
        want = t;
        vel = 0;
      }
    } else {
      const acc = SPRING_W * SPRING_W * (want - t) - 2 * SPRING_Z * SPRING_W * vel;
      vel += acc * dt;
      t += vel * dt;
      if (Math.abs(want - t) < 0.002 && Math.abs(vel) < 0.01) {
        t = want;
        vel = 0;
      }
    }
    draw(t);
    raf = intro || t !== want ? requestAnimationFrame(run) : 0;
  }
  function moveDial() {
    if (still) {
      t = want;
      vel = 0;
      draw(t);
      return;
    }
    if (!raf) {
      lastMs = performance.now();
      raf = requestAnimationFrame(run);
    }
  }
  function glide(i, board) {
    want = clamp(i, 0, n - 1);
    intro = null;
    if (want !== said || board) {
      say(want, board);
    }
    moveDial();
  }
  function openOut(to) {
    intro = { t0: performance.now(), to };
    t = 0;
    vel = 0;
    want = to;
    draw(0);
    moveDial();
  }

  /* the pointer: how far from the middle is the version, a line within NEAR_PX of it its board */
  function pick(ev) {
    const p = svg.createSVGPoint();
    p.x = ev.clientX;
    p.y = ev.clientY;
    const q = p.matrixTransform(svg.getScreenCTM().inverse());
    const dx = q.x - MID;
    const dy = q.y - MID;
    const d = Math.hypot(dx, dy);
    if (d > OUTER + 40) {
      tip.hidden = true;
      return;
    }
    const i = clamp(Math.round(((d - INNER) * (n - 1)) / (OUTER - INNER)), 0, n - 1);
    const a = Math.atan2(dy, dx);
    let board = null;
    for (const [b, m] of lines) {
      let da = Math.abs(a - m.a);
      if (da > Math.PI) {
        da = 2 * Math.PI - da;
      }
      if (first.get(b) <= i && d * da < NEAR_PX && d >= ringR(first.get(b)) - NEAR_PX) {
        board = b;
      }
    }
    if (i !== want || intro || board) {
      glide(i, board);
    }
    if (board) {
      const since = versions[first.get(board)].version;
      tip.innerHTML = `<b>${esc(nameOf(board))}</b> ${esc(chipName(chipOf(board)))}, since ${esc(since)}`;
      const r = box.getBoundingClientRect();
      tip.style.left = `${Math.min(ev.clientX - r.left + 12, r.width - 220)}px`;
      tip.style.top = `${ev.clientY - r.top + 14}px`;
      tip.hidden = false;
    } else {
      tip.hidden = true;
    }
  }
  svg.addEventListener('pointermove', pick);
  svg.addEventListener('pointerdown', pick);
  svg.addEventListener('pointerleave', () => {
    tip.hidden = true;
  });
  svg.addEventListener('keydown', (ev) => {
    const to = { ArrowLeft: want - 1, ArrowDown: want - 1, ArrowRight: want + 1, ArrowUp: want + 1, Home: 0, End: n - 1 }[ev.key];
    if (to !== undefined) {
      ev.preventDefault();
      glide(to);
    }
  });
  document.getElementById('histplay').addEventListener('click', () => {
    dialOpened = true;
    openOut(n - 1);
  });

  /* ---- the code growing, and the two views ---- */
  const versionIndex = (ver) => {
    if (!ver || ver === '-') {
      return 0;                                        // before CHANGES.md was: the first version
    }
    const num = ver.replace(/b$/, '');
    let best = -1;
    versions.forEach((v, i) => {                       // a beta since folded into its version: 0.16.1 is 0.16's
      if (num === v.version || num.startsWith(v.version + '.')) {
        best = i;
      }
    });
    return best;
  };
  let view = 'boards';
  let visible = false;
  let dialOpened = false;
  /* the panel follows the picture that is shown; the dial waits at that version, to ease on from it when shown */
  const follow = (name) => ({
    versionIndex,
    versionName: (i) => (versions[i] ? versions[i].version : ''),
    onVersion(i) {
      if (view === name) {
        say(i);
        want = i;
        t = i;
      }
    },
    onFocus(state) {
      if (view === name) {
        picked = state;
        renderExtra();
      }
    },
  });
  /* one window of the page's own windows, docked here at the bottom: its maximize button opens it over the whole
   * page, its minimize puts it in the corner's tray, and closed it leaves a line to bring it back */
  const grid = section.querySelector('.historygrid');
  const reopen = document.getElementById('histopen');
  const shownNow = () => visible && !win.root.hidden;
  const win = desk.make('history', 'How it grew, version by version', {
    className: 'history',
    dock: section,
    onResize() {
      if (win.maxed) {
        visible = true;
      }
      update();
      window.dispatchEvent(new Event('resize'));     // the pictures measure themselves again
    },
    onClose(act) {
      update();
      if (act === 'close') {
        reopen.hidden = false;
      }
    },
    onFront() {
      reopen.hidden = true;
      update();
    },
  });
  win.body.append(grid);
  reopen.addEventListener('click', () => {
    reopen.hidden = true;
    win.show(reopen);
    win.front();
  });
  const growBox = document.getElementById('histgrow');
  const towerBox = document.getElementById('histtower');
  const growth = story ? startGrowth(growBox, story, follow('code')) : null;
  /* the tower needs WebGL 2 and three.js, 1.4 MB: asked for only when the section is first seen, and where it does
   * not come up the other two views are all there is */
  let tower = null;
  let towerAsked = false;
  const towerLikely = Boolean(story) && 'WebGL2RenderingContext' in window;
  const tabs = document.getElementById('histtabs');
  if (growth) {
    view = towerLikely ? 'tower' : 'code';
    tabs.hidden = false;
  }
  tabs.querySelector('[data-view="tower"]').hidden = !towerLikely;
  function show(which) {
    view = which;
    for (const b of tabs.querySelectorAll('button')) {
      b.setAttribute('aria-selected', String(b.dataset.view === which));
    }
    towerBox.hidden = which !== 'tower';
    growBox.hidden = which !== 'code';
    box.hidden = which !== 'boards';
    legend.hidden = which !== 'boards';
    if (which === 'boards') {
      draw(t);
    }
    picked = { v: -1, part: -1, moved: false };
    renderExtra();
    update();
  }
  /* what runs follows what is seen: the tower's and the code's loops, the beam, the dial opening out the first time */
  function update() {
    const on = shownNow();
    if (on && view === 'tower' && !towerAsked) {
      towerAsked = true;
      towerBox.classList.add('loading');
      startTower(towerBox, story, follow('tower')).then((made) => {
        towerBox.classList.remove('loading');
        tower = made;
        if (!tower) {                                  // no WebGL after all: the code growing instead
          tabs.querySelector('[data-view="tower"]').hidden = true;
          show('code');
          return;
        }
        renderExtra();
        update();
      });
    }
    if (tower) {
      tower.setActive(on && view === 'tower');
    }
    if (growth) {
      growth.setActive(on && view === 'code');
    }
    scope.classList.toggle('sweep', on && view === 'boards' && !still);
    if (on && view === 'boards' && !dialOpened) {
      dialOpened = true;
      if (still) {
        draw(want);
      } else {
        openOut(Math.round(want));
      }
    }
  }
  tabs.addEventListener('click', (ev) => {
    const b = ev.target.closest('button[data-view]');
    if (b && b.dataset.view !== view) {
      show(b.dataset.view);
    }
  });
  /* the menu: a version, a part alone, or everything back as it was */
  panel.addEventListener('click', (ev) => {
    const all = ev.target.closest('button[data-all]');
    if (all) {
      if (view === 'tower' && tower) {
        tower.showAll();
      } else if (view === 'code' && growth) {
        growth.showAll();
      } else {
        glide(n - 1);
      }
      return;
    }
    const part = ev.target.closest('button[data-part]');
    if (part && tower) {
      const i = Number(part.dataset.part);
      tower.setPart(i === picked.part ? -1 : i);
      return;
    }
    const b = ev.target.closest('button[data-v]');
    if (!b) {
      return;
    }
    const i = Number(b.dataset.v);
    if (view === 'tower' && tower) {
      const shown = tower.seekVersion(i);
      say(shown >= 0 ? shown : i);
    } else if (view === 'code' && growth) {
      growth.seekVersion(i);
      say(i);
    } else {
      glide(i);
    }
  });

  section.hidden = false;
  win.show();
  draw(n - 1);
  say(n - 1);
  show(view);
  if (!('IntersectionObserver' in window)) {
    visible = true;
    update();
    return;
  }
  new IntersectionObserver((entries) => {
    visible = entries.some((e) => e.isIntersecting) || win.maxed;
    update();
  }, { threshold: 0.2 }).observe(section);
}
