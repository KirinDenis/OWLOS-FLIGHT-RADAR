/* the boards out there, as our server counts them: how many were flashed from this site, how many are online, how
 * many are on a cable to this site now, by board and by country. counts only, never a board or a person: the
 * server keeps no MAC, no address, no place, and a board or a page sends anything only when its owner allowed it.
 * no answer from the server: nothing of this shows. numbers are never made up on this page.
 *
 * what GET API answers, JSON:
 *   { "updated": "2026-11-10T13:00:00Z",         when the server counted
 *     "flashed": 1234, "online": 321, "cable": 12, all the boards: flashed here, online now, on a cable to this site now
 *     "boards": [ { "build": "st7796", "flashed": 50, "online": 12, "versions": [ ["0.11", 30], ["0.10", 20] ] } ],
 *     "countries": [ { "cc": "DE", "flashed": 120, "online": 40 } ] }
 *   build is a firmware build's name (BOARDS in flow.js), cc a country's ISO 3166 two letters, versions what the
 *   flashes were, the newest first. "sample": true marks the made-up file of a local test (tools/stats-sample.json)
 */
/* our server (server/ in the repository), as the site's version.json says it: "stats", from
 * server/public-url.txt. the boards read the same line. empty: the page asks nothing and sends nothing, not
 * even the map, and shows none of this. a local look: index.html?stats=sample (made-up counts, nothing
 * sent) or index.html?stats=local (the server run on this PC, server/README.md) */
let SERVER = '';
const LOCAL_SERVER = 'http://127.0.0.1:5088';
const EVERY = 60000;                                   // the server counts once a minute too

function server() {
  const asked = new URLSearchParams(location.search).get('stats');
  return asked === 'local' ? LOCAL_SERVER : asked === 'sample' ? '' : SERVER;
}
/* countries left out: on the map only land, no shade, no number, nothing over it, and not in the list. the
 * server does not count them either; this is so a count that came anyway shows nowhere */
const IGNORED = new Set(['RU']);

const fmt = (n) => Number(n).toLocaleString('en-US');
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const regionName = (() => {
  try {
    const names = new Intl.DisplayNames(['en'], { type: 'region' });
    return (cc) => {
      try {
        return names.of(cc) || cc;
      } catch {
        return cc;
      }
    };
  } catch {
    return (cc) => cc;
  }
})();

/* a country's shade by how many boards it has: one hue, darker to brighter on the dark page, the brightest the
 * page's cyan. checked against a country with none (#0a1c36): the darkest at 2.4:1, each step a visible one */
export const STEPS = [
  { from: 1, fill: '#1b5d87' },
  { from: 2, fill: '#1f7aa8' },
  { from: 10, fill: '#2a99c8' },
  { from: 50, fill: '#40bce6' },
  { from: 200, fill: '#8aeaff' },
];
export function shade(n) {
  let fill = '';
  for (const s of STEPS) {
    if (n >= s.from) {
      fill = s.fill;
    }
  }
  return fill;
}

function sourceUrl() {
  const asked = new URLSearchParams(location.search).get('stats');
  if (asked === 'sample') {
    return 'stats-sample.json';                        // the sample is only in a local build, the site has none
  }
  return server() ? `${server()}/api/stats` : '';
}

function line(s) {
  const el = document.getElementById('statline');
  el.innerHTML = `<b>${fmt(s.flashed)}</b> boards flashed <i>/</i> <b>${fmt(s.online)}</b> online now <i>/</i> `
    + `<b>${fmt(s.cable)}</b> on a cable to this site${s.sample ? ' <em>SAMPLE NUMBERS</em>' : ''}`;
  el.hidden = false;
}

function boardTable(s, boards) {
  const rows = [...(s.boards || [])].sort((a, b) => b.flashed - a.flashed);
  const most = Math.max(1, ...rows.map((r) => r.flashed));
  const body = rows.map((r) => {
    const name = boards[r.build]?.name ?? r.build;
    const versions = (r.versions || []).map(([v, n]) => `${esc(v)}: ${fmt(n)}`).join(', ');
    return `<tr><th scope="row">${esc(name)}</th>
      <td><span class="num">${fmt(r.flashed)}</span>${versions ? ` <small>(${versions})</small>` : ''}
        <i class="meter" style="width:${(100 * r.flashed / most).toFixed(1)}%"></i></td>
      <td class="num">${fmt(r.online)}</td></tr>`;
  }).join('');
  document.getElementById('boardstats').innerHTML = `<table>
    <thead><tr><th scope="col">Board</th><th scope="col">Flashed <small>(by version)</small></th><th scope="col">Online</th></tr></thead>
    <tbody>${body}</tbody></table>`;
}

function countryList(s) {
  const rows = [...(s.countries || [])].sort((a, b) => b.flashed - a.flashed);
  document.getElementById('countrylist').innerHTML = rows.map((c) => `<li title="${esc(regionName(c.cc))}: ${fmt(c.flashed)} flashed, ${fmt(c.online)} online">`
    + `<i style="background:${shade(c.flashed)}"></i>${esc(regionName(c.cc))} <b>${fmt(c.flashed)}</b></li>`).join('');
}

function legend() {
  const ends = STEPS.map((s, i) => (i + 1 < STEPS.length ? (STEPS[i + 1].from - 1 === s.from ? `${s.from}` : `${s.from}-${STEPS[i + 1].from - 1}`) : `${s.from}+`));
  document.getElementById('worldlegend').innerHTML = 'Boards flashed: ' + STEPS.map((s, i) => `<span><i style="background:${s.fill}"></i>${ends[i]}</span>`).join('')
    + '<span class="hint">wheel or + to zoom, drag to move</span>';
}

/* --- the map: world.js's countries, each shaded by its boards, and the number in a ring where its name would go.
 * the rings are in screen pixels, so the map is drawn again at another width, and they push each other apart
 * where countries are small and close (Europe), a thin line back to where each belongs --- */
let world = null;                                      // world.js, once it came
let last = null;                                       // the counts drawn last, for a redraw at another width
const RING_GAP = 2;

function relax(spots, x0, y0, x1, y1) {
  for (let pass = 0; pass < 140; pass++) {
    for (let i = 0; i < spots.length; i++) {
      for (let j = i + 1; j < spots.length; j++) {
        const a = spots[i];
        const b = spots[j];
        let dx = b.x - a.x;
        let dy = b.y - a.y;
        let d = Math.hypot(dx, dy);
        const need = a.r + b.r + RING_GAP;
        if (d < need) {
          if (d < 0.01) {
            dx = 1;
            dy = 0;
            d = 1;
          }
          const push = (need - d) / 2;                 // half each, the bigger one moves as much as the small
          a.x -= dx / d * push;
          a.y -= dy / d * push;
          b.x += dx / d * push;
          b.y += dy / d * push;
        }
      }
    }
    for (const p of spots) {                           // and a little back home every pass, never out of the view
      p.x = Math.min(x1 - p.r - 1, Math.max(x0 + p.r + 1, p.x + (p.ox - p.x) * 0.04));
      p.y = Math.min(y1 - p.r - 1, Math.max(y0 + p.r + 1, p.y + (p.oy - p.y) * 0.04));
    }
  }
}

/* what part of the map shows, in its units: the wheel, a drag, two fingers and the buttons move it. w 0: all of
 * it. it stays as it is when new counts come */
const view = { x: 0, y: 0, w: 0 };
const ZOOM_MAX = 8;
let counted = new Map();                               // the counts on the map now, by country

const viewH = () => view.w * world.h / world.w;
const fitW = (w) => Math.min(world.w, Math.max(world.w / ZOOM_MAX, w || world.w));

function clampView() {
  view.w = fitW(view.w);
  view.x = Math.min(world.w - view.w, Math.max(0, view.x));
  view.y = Math.min(world.h - viewH(), Math.max(0, view.y));
}

// closer by k (below 1: further), the point fx, fy (0..1 across what shows) staying where it is
function zoom(k, fx, fy) {
  clampView();
  const mx = view.x + fx * view.w;
  const my = view.y + fy * viewH();
  view.w = fitW(view.w / k);
  view.x = mx - fx * view.w;
  view.y = my - fy * viewH();
}

function drawWorld(el, countries) {
  if (!world || el.clientWidth < 50) {
    return;
  }
  counted = new Map(countries.filter((c) => c.flashed > 0).map((c) => [c.cc, c]));
  const land = world.countries.map(([cc, , , , d]) => (counted.has(cc)
    ? `<path class="land has" data-cc="${cc}" d="${d}" style="fill:${shade(counted.get(cc).flashed)}"/>`
    : `<path class="land" d="${d}"/>`)).join('');
  el.innerHTML = `<svg role="img" aria-label="A map of the boards flashed, by country: the list under it says the same">`
    + `<g fill-rule="evenodd">${land}</g><g class="rings"></g></svg>`
    + '<div class="mapzoom"><button type="button" data-zoom="in" aria-label="Closer">+</button>'
    + '<button type="button" data-zoom="out" aria-label="Further">&minus;</button>'
    + '<button type="button" data-zoom="all" aria-label="The whole world">ALL</button></div><div id="worldtip" hidden></div>';
  look(el);
}

// the view onto the map, and the rings for it: a ring's size is in screen pixels, at any zoom
function look(el) {
  const svg = el.querySelector('svg');
  if (!svg || !world) {
    return;
  }
  clampView();
  const h = viewH();
  svg.setAttribute('viewBox', `${view.x.toFixed(2)} ${view.y.toFixed(2)} ${view.w.toFixed(2)} ${h.toFixed(2)}`);
  svg.style.touchAction = view.w < world.w ? 'none' : 'pan-y';   // closer in, a finger moves the map; else the page
  const u = view.w / el.clientWidth;                   // map units in a screen pixel
  const spots = [];
  for (const [cc, , lx, ly] of world.countries) {
    const c = counted.get(cc);
    if (c && lx >= view.x && lx <= view.x + view.w && ly >= view.y && ly <= view.y + h) {
      const r = Math.min(17, 8 + 1.6 * Math.log2(c.flashed)) * u;   // 8 px for one board, 17 at most
      spots.push({ cc, n: c.flashed, r, x: lx, y: ly, ox: lx, oy: ly });
    }
  }
  spots.sort((a, b) => a.n - b.n);                     // the big ones drawn last, on top
  relax(spots, view.x, view.y, view.x + view.w, view.y + h);
  svg.querySelector('.rings').innerHTML = spots.map((s) => {
    const moved = Math.hypot(s.x - s.ox, s.y - s.oy) > s.r * 0.6;
    return `<g class="dot" data-cc="${s.cc}">${moved ? `<line x1="${s.ox.toFixed(2)}" y1="${s.oy.toFixed(2)}" x2="${s.x.toFixed(2)}" y2="${s.y.toFixed(2)}" stroke="#5ce1ff" stroke-opacity=".5" stroke-width="${(0.8 * u).toFixed(3)}"/>` : ''}`
      + `<circle cx="${s.x.toFixed(2)}" cy="${s.y.toFixed(2)}" r="${s.r.toFixed(2)}" stroke-width="${(1.2 * u).toFixed(3)}"/>`
      + `<text x="${s.x.toFixed(2)}" y="${s.y.toFixed(2)}" style="font-size:${((s.n > 999 ? 8.5 : 10) * u).toFixed(2)}px">${fmt(s.n)}</text></g>`;
  }).join('');
  el.querySelector('[data-zoom="in"]').disabled = view.w <= world.w / ZOOM_MAX + 0.01;
  el.querySelector('[data-zoom="out"]').disabled = view.w >= world.w;
  el.querySelector('[data-zoom="all"]').disabled = view.w >= world.w;
}

/* the wheel zooms where the pointer is, a drag moves the map, two fingers pinch it. the wheel going out over
 * the whole world is the page's, it scrolls on; so is one finger on the whole world */
function steer(el) {
  let frame = 0;
  const redraw = () => {
    if (!frame) {
      frame = requestAnimationFrame(() => {
        frame = 0;
        look(el);
      });
    }
  };
  const at = (e) => {
    const box = el.querySelector('svg').getBoundingClientRect();
    return [(e.clientX - box.left) / box.width, (e.clientY - box.top) / box.height];
  };
  el.addEventListener('wheel', (e) => {
    if (!world || !e.target.closest('svg') || (e.deltaY > 0 && fitW(view.w) >= world.w)) {
      return;
    }
    e.preventDefault();
    const [fx, fy] = at(e);
    zoom(Math.exp(-e.deltaY * (e.deltaMode ? 0.05 : 0.0018)), fx, fy);
    redraw();
  }, { passive: false });
  el.addEventListener('dblclick', (e) => {
    if (world && e.target.closest('svg')) {
      const [fx, fy] = at(e);
      zoom(2, fx, fy);
      redraw();
    }
  });
  el.addEventListener('click', (e) => {
    const b = e.target.closest('[data-zoom]');
    if (!b || !world) {
      return;
    }
    if (b.dataset.zoom === 'all') {
      view.w = world.w;
    } else {
      zoom(b.dataset.zoom === 'in' ? 2 : 0.5, 0.5, 0.5);
    }
    redraw();
  });
  const fingers = new Map();
  let from = null;                                     // the view and the fingers when this drag or pinch began
  const begin = () => {
    const pts = [...fingers.values()];
    const cx = pts.reduce((s, p) => s + p.x, 0) / pts.length;
    const cy = pts.reduce((s, p) => s + p.y, 0) / pts.length;
    const d = pts.length > 1 ? Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y) : 0;
    clampView();
    from = { cx, cy, d, x: view.x, y: view.y, w: view.w, box: el.querySelector('svg').getBoundingClientRect() };
  };
  el.addEventListener('pointerdown', (e) => {
    if (!world || !e.target.closest('svg') || e.button > 0 || (e.pointerType === 'touch' && view.w >= world.w && !fingers.size)) {
      return;
    }
    fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    try {
      el.setPointerCapture(e.pointerId);
    } catch {
      // a pointer the page could not keep: the drag still goes while it stays over the map
    }
    begin();
    el.classList.add('grabbing');
  });
  el.addEventListener('pointermove', (e) => {
    if (!fingers.has(e.pointerId) || !from) {
      return;
    }
    fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    const pts = [...fingers.values()];
    const cx = pts.reduce((s, p) => s + p.x, 0) / pts.length;
    const cy = pts.reduce((s, p) => s + p.y, 0) / pts.length;
    const per = from.w / from.box.width;               // map units a pixel, when it began
    const mx = from.x + (from.cx - from.box.left) * per;   // the map's point under the fingers then
    const my = from.y + (from.cy - from.box.top) * per;
    view.w = pts.length > 1 && from.d > 0 ? fitW(from.w * from.d / Math.max(1, Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y))) : from.w;
    const now = view.w / from.box.width;
    view.x = mx - (cx - from.box.left) * now;          // stays under them now
    view.y = my - (cy - from.box.top) * now;
    redraw();
  });
  const end = (e) => {
    if (fingers.delete(e.pointerId)) {
      if (fingers.size) {
        begin();
      } else {
        from = null;
        el.classList.remove('grabbing');
      }
    }
  };
  el.addEventListener('pointerup', end);
  el.addEventListener('pointercancel', end);
}

// what a country has, over it: the pointer's or a tap's
function tipFor(el) {
  el.addEventListener('pointermove', (e) => {
    const tip = document.getElementById('worldtip');
    const hit = e.target.closest?.('[data-cc]');
    el.querySelectorAll('.land.on').forEach((p) => p.classList.remove('on'));
    if (!tip || !hit || !last || el.classList.contains('grabbing')) {
      if (tip) {
        tip.hidden = true;
      }
      return;
    }
    const c = (last.countries || []).find((x) => x.cc === hit.dataset.cc);
    if (!c) {
      tip.hidden = true;
      return;
    }
    el.querySelector(`.land[data-cc="${c.cc}"]`)?.classList.add('on');
    tip.textContent = `${regionName(c.cc)}: ${fmt(c.flashed)} flashed, ${fmt(c.online)} online`;
    tip.hidden = false;
    const box = el.getBoundingClientRect();
    const x = Math.min(e.clientX - box.left + 12, box.width - tip.offsetWidth - 4);
    tip.style.left = `${Math.max(4, x)}px`;
    tip.style.top = `${e.clientY - box.top + 14}px`;
  });
  el.addEventListener('pointerleave', () => {
    const tip = document.getElementById('worldtip');
    if (tip) {
      tip.hidden = true;
    }
    el.querySelectorAll('.land.on').forEach((p) => p.classList.remove('on'));
  });
}

function hideStats() {
  reveal('statline', false);
  reveal('world', false);
}

async function show(boards) {
  let s;
  try {
    const r = await fetch(sourceUrl(), { cache: 'no-store' });
    if (!r.ok) {
      throw new Error(`HTTP ${r.status}`);
    }
    s = await r.json();
    s.countries = (s.countries || []).filter((c) => !IGNORED.has(c.cc));
  } catch {
    hideStats();
    return false;
  }
  last = s;
  line(s);
  boardTable(s, boards);
  countryList(s);
  legend();
  document.getElementById('world').hidden = false;     // shown first: the map needs its width
  drawWorld(document.getElementById('worldmap'), s.countries || []);
  return true;
}

/* --- what this page and its boards tell the server, only when the visitor allowed it: asked once, when a
 * board is connected or flashed, the answer kept in this browser. the boards are told the same answer
 * (OWLOS SET {"help":1} or 2), flow.js does that through onHelp. no server: never asked, nothing sent --- */
const ASKED = 'owlos-radar-help';
const listeners = [];
let given = null;                                      // the answer on this page, for a browser that keeps nothing
const reveal = (id, on) => {                             // none of this may stop the page, a flash least of all
  const el = document.getElementById(id);
  if (el) {
    el.hidden = !on;
  }
};

// 'yes', 'no', or null: not asked yet
export function helpAnswer() {
  try {
    const a = localStorage.getItem(ASKED);
    if (a === 'yes' || a === 'no') {
      return a;
    }
  } catch {
    // a browser that keeps nothing: the answer given on this page, if any
  }
  return given;
}

export function onHelp(f) {
  listeners.push(f);
}

export function askHelp() {
  if (server() && !helpAnswer()) {
    reveal('helpask', true);
  }
}

function setHelp(a) {
  given = a;
  try {
    localStorage.setItem(ASKED, a);
  } catch {
    // a browser that keeps nothing: this page goes by the answer, the next one asks again
  }
  reveal('helpask', false);
  helpLine(a);
  for (const w of waiting.splice(0)) {
    if (a === 'yes') {
      post('/api/flash', w);
    }
  }
  for (const f of listeners) {
    f(a);
  }
}

function helpLine(a = helpAnswer()) {
  reveal('helpline', !!server());
  const state = document.getElementById('helpstate');
  if (state) {
    state.textContent = a === 'yes' ? 'on' : 'off';
  }
}

function post(path, body) {
  if (!server() || helpAnswer() !== 'yes') {
    return;
  }
  fetch(`${server()}${path}`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), keepalive: true })
    .catch((e) => console.warn('the stats server did not take it', e));
}

// a flash before the visitor answered waits for the answer: sent on ALLOW, dropped on NO
const waiting = [];

export function reportFlash(build, version) {
  if (server() && !helpAnswer()) {
    waiting.push({ build, version });
    askHelp();
    return;
  }
  post('/api/flash', { build, version });
}

export function reportCable(n) {
  if (n > 0) {
    post('/api/cable', { n });
  }
}

function startHelp() {
  document.getElementById('helpyes')?.addEventListener('click', () => setHelp('yes'));
  document.getElementById('helpno')?.addEventListener('click', () => setHelp('no'));
  document.getElementById('helpchange')?.addEventListener('click', () => reveal('helpask', true));
  helpLine();
  // the page shown: a count a day and a country, no cookie. not when the visitor said no
  if (server() && helpAnswer() !== 'no') {
    try {
      navigator.sendBeacon(`${server()}/api/visit`);
    } catch (e) {
      console.warn(e);
    }
  }
}

export async function startStats(boards) {
  try {
    const v = await (await fetch('version.json', { cache: 'no-store' })).json();
    SERVER = typeof v.stats === 'string' && /^https:\/\/[^\s"?#\\]+$/.test(v.stats) ? v.stats.replace(/\/+$/, '') : '';
  } catch {
    SERVER = '';                                       // no version.json: no server either
  }
  startHelp();
  if (!sourceUrl()) {
    return;                                            // no server yet: nothing to ask
  }
  const map = document.getElementById('worldmap');
  tipFor(map);
  steer(map);
  let width = 0;
  new ResizeObserver(() => {
    if (last && Math.abs(map.clientWidth - width) > 8) {
      width = map.clientWidth;
      drawWorld(map, last.countries || []);
    }
  }).observe(map);
  import('./world.js').then((m) => {
    world = m.WORLD;
  }).catch((e) => {
    console.warn('no map of the world', e);            // the list of countries under it says it all
  }).finally(() => {
    // at once, then every minute while the page is seen; a server that did not answer is asked less often
    let timer = 0;
    const ask = async () => {
      clearTimeout(timer);
      let ok = false;
      try {
        ok = await show(boards);
      } catch (e) {
        console.warn('the stats could not be shown', e);   // an odd answer: hidden, asked again later
        hideStats();
      }
      timer = setTimeout(() => {
        if (!document.hidden) {
          ask();
        }
      }, ok ? EVERY : 10 * EVERY);
    };
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) {
        ask();
      }
    });
    ask();
  });
}
