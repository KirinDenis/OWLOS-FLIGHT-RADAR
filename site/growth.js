/* the radar's code growing from its first commit, in the manner of Gource: a folder a branch, a file a dot round
 * its folder in the colour of its kind, and whoever wrote a commit flies to the files it touched and lights them.
 * history.txt is the site workflow's, from git log (.github/history.sh): of the people their names and nothing
 * else, a version is CHANGES.md's at that commit, and the time on top is the commits' own. the folders hold apart
 * and to their parent as springs do, the camera follows what grew, and nothing moves when nothing is played */

/* a colour a kind of file, at most eight: the rest is "other". checked for colour blind eyes like the dial's chips,
 * and a file's kind is also said in words under the pointer and in the list in the corner */
const KINDS = [
  { name: '.c', colour: '#4fc8ff', ext: ['c'] },
  { name: '.h', colour: '#7dffa8', ext: ['h', 'inc'] },
  { name: '.cpp', colour: '#ff8a5c', ext: ['cpp'] },
  { name: '.js .html', colour: '#ffd25c', ext: ['js', 'mjs', 'html', 'css'] },
  { name: '.cs', colour: '#ff5fa8', ext: ['cs', 'csproj', 'sln'] },
  { name: 'pictures', colour: '#b48cff', ext: ['png', 'gif', 'jpg', 'svg', 'mp4', 'webm'] },
  { name: '.md .txt', colour: '#eaf7ff', ext: ['md', 'txt', 'csv', 'vtt'] },
  { name: 'other', colour: '#5d6b85', ext: [] },
];
const OTHER = KINDS.length - 1;
const KIND_OF = new Map();
KINDS.forEach((k, i) => {
  for (const e of k.ext) {
    KIND_OF.set(e, i);
  }
});
const OP_COLOUR = { A: '#7dffa8', M: '#ffd25c', D: '#ff6070' };

const HOUR_S = 0.3;                                    // seconds of playing an hour between two commits
const GAP_MIN = 0.35;                                  // and never less or more
const GAP_MAX = 1.8;
const LEAD_S = 0.6;                                    // before the first commit
const TAIL_S = 3;                                      // after the last, the tree settling
const SPACE = 4.2;                                     // a file's room round its folder, world units
const BEAM_S = 1.8;                                    // a touched file's beam fades in that long
const BG = '#03081a';

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function hash(s) {                                     // a path's own number in 0..1: the same tree for everyone
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) {
    h = Math.imul(h ^ s.charCodeAt(i), 16777619);
  }
  return (h >>> 0) / 4294967296;
}
function rgba(hex, a) {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`;
}

/* history.txt: C time version author, then A, M, D or R lines, tabs between. in the order they were written */
function readHistory(text) {
  const out = [];
  let c = null;
  for (const line of text.split(/\r?\n/)) {
    const f = line.split('\t');
    if (f[0] === 'C' && f.length >= 4) {
      c = { t: Number(f[1]), ver: f[2], who: f[3], ch: [] };
      out.push(c);
    } else if (c && /^[AMDR]$/.test(f[0]) && f[1]) {
      c.ch.push(f);
    }
  }
  return out.filter((x) => Number.isFinite(x.t)).sort((a, b) => a.t - b.t);
}

/* a glow: a soft round spot of a colour, drawn once and laid over and over, added to what is under it */
function glowSprite(colour) {
  const s = document.createElement('canvas');
  s.width = 64;
  s.height = 64;
  const c = s.getContext('2d');
  const g = c.createRadialGradient(32, 32, 0, 32, 32, 32);
  g.addColorStop(0, rgba(colour, 1));
  g.addColorStop(0.18, rgba(colour, 0.55));
  g.addColorStop(0.5, rgba(colour, 0.12));
  g.addColorStop(1, rgba(colour, 0));
  c.fillStyle = g;
  c.fillRect(0, 0, 64, 64);
  return s;
}

/* the version at each commit: CHANGES.md's heading at it, but only ever a step on. a branch written before its
 * merge can carry a higher number than the line it joins (the Tab5's commits said 0.8 while the main line was at
 * 0.6): it takes the version of its time, and a version comes where the one before it has come. a version no
 * commit ever had is stepped over */
export function versionSteps(commits, versionIndex) {
  const raw = commits.map((c) => versionIndex(c.ver));
  const present = [...new Set(raw.filter((v) => v >= 0))].sort((a, b) => a - b);
  let cur = -1;
  commits.forEach((c, i) => {
    const next = present.find((v) => v > cur);
    if (next !== undefined && (raw[i] === next || (raw[i] > next && !raw.slice(i).includes(next)))) {
      cur = raw[i];
    }
    c.vi = cur;
  });
}

/* box: where the picture goes. hooks.versionIndex(version) the version's index in the panel's list, -1 none;
 * hooks.versionName(index) its number; hooks.onVersion(index) when the playing comes to a new version. null when the history is too short */
export function startGrowth(box, text, hooks) {
  const commits = readHistory(text);
  if (commits.length < 2) {
    return null;
  }
  let at = LEAD_S;
  commits.forEach((c, i) => {
    if (i) {
      at += clamp(((c.t - commits[i - 1].t) / 3600) * HOUR_S, GAP_MIN, GAP_MAX);
    }
    c.at = at;
  });
  versionSteps(commits, hooks.versionIndex);
  const END = at + TAIL_S;

  const canvas = document.createElement('canvas');
  canvas.setAttribute('role', 'img');
  canvas.setAttribute('aria-label', 'The radar\'s files growing commit by commit, each folder a branch, each file a dot in the colour of its kind');
  box.appendChild(canvas);
  const tip = document.createElement('div');
  tip.className = 'growtip';
  tip.hidden = true;
  box.appendChild(tip);
  const bar = document.createElement('div');
  bar.className = 'growbar';
  bar.innerHTML = '<button class="growplay" type="button">PAUSE</button><input type="range" min="0" step="1" aria-label="Commits played">' +
    '<span class="growwhen"></span>';
  box.appendChild(bar);
  const hint = document.createElement('p');
  hint.className = 'growhint';
  box.appendChild(hint);
  const full = () => Boolean(box.closest('.win.maxed'));   // its window over the whole page
  const look = { zoom: 1, px: 0, py: 0 };             // what the person did to the camera: closer, moved
  const playBtn = bar.querySelector('button');
  const range = bar.querySelector('input');
  const when = bar.querySelector('.growwhen');
  range.max = String(commits.length);
  const g = canvas.getContext('2d');
  const sprites = KINDS.map((k) => glowSprite(k.colour));
  const whiteSprite = glowSprite('#eaf7ff');
  // the bloom's small canvas, where the browser can blur on a canvas (not every Safari can): without it, no bloom
  const bloom = 'filter' in CanvasRenderingContext2D.prototype ? document.createElement('canvas') : null;
  const beamSprites = Object.fromEntries(Object.entries(OP_COLOUR).map(([k, c]) => [k, glowSprite(c)]));
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;

  let W = 0;
  let H = 0;
  let dpr = 1;
  function fit() {
    W = Math.max(200, box.clientWidth);
    if (full()) {                                      // the full screen: all the height under the tabs and over the bar
      // down to the bottom of the window's body, less its padding, the bar and the hint under the picture
      const body = box.closest('.winbody');
      const bottom = body ? body.getBoundingClientRect().bottom - 14 : window.innerHeight - 12;
      H = Math.round(Math.max(240, bottom - box.getBoundingClientRect().top - bar.offsetHeight - hint.offsetHeight - 22));
    } else {
      H = Math.round(clamp(W * 0.92, 300, 640));
    }
    hint.textContent = full()
      ? 'Drag to move it, the wheel to come closer. Double click: back.'
      : 'Drag to move it, ctrl and the wheel to come closer, or open the window to the whole screen.';
    dpr = Math.min(2, window.devicePixelRatio || 1);
    canvas.width = Math.round(W * dpr);
    canvas.height = Math.round(H * dpr);
    canvas.style.width = `${W}px`;
    canvas.style.height = `${H}px`;
  }

  /* the tree as the commits so far made it */
  let dirs;
  let files;
  let people;
  let next;                                            // the next commit to come
  let now;                                             // seconds of playing
  let verShown;
  let flash;                                           // the version just come: its words and when
  let cam;
  let shownCount;                                      // the corner's list, eased
  function reset() {
    dirs = new Map();
    files = new Map();
    people = new Map();
    next = 0;
    now = 0;
    verShown = -2;
    flash = null;
    cam = { x: 0, y: 0, s: 2.2 };
    shownCount = KINDS.map(() => 0);
    dirs.set('', { path: '', name: '', parent: null, kids: [], files: [], x: 0, y: 0, vx: 0, vy: 0, r: 8, alpha: 1, spin: 0, empty: 0 });
  }
  function dirOf(path) {
    let d = dirs.get(path);
    if (d) {
      d.empty = 0;
      return d;
    }
    const cut = path.lastIndexOf('/');
    const up = dirOf(cut < 0 ? '' : path.slice(0, cut));
    const a = hash(path) * Math.PI * 2;
    d = { path, name: path.slice(cut + 1), parent: up, kids: [], files: [], x: up.x + Math.cos(a) * 24, y: up.y + Math.sin(a) * 24,
      vx: 0, vy: 0, r: 6, alpha: 0, spin: hash(path + '/') * Math.PI * 2, empty: 0 };
    up.kids.push(d);
    dirs.set(path, d);
    return d;
  }
  function addFile(path, quiet) {
    let f = files.get(path);
    if (f) {
      f.dying = 0;
      f.heat = quiet ? 0 : 1;
      return f;
    }
    const cut = path.lastIndexOf('/');
    const d = dirOf(cut < 0 ? '' : path.slice(0, cut));
    const dot = path.lastIndexOf('.');
    const ext = dot > cut ? path.slice(dot + 1).toLowerCase() : '';
    f = { path, dir: d, kind: KIND_OF.has(ext) ? KIND_OF.get(ext) : OTHER, born: quiet ? -99 : now, heat: quiet ? 0 : 1, dying: 0,
      x: d.x, y: d.y, alpha: quiet ? 1 : 0 };
    d.files.push(f);
    files.set(path, f);
    return f;
  }
  function apply(c, quiet) {
    const touched = [];
    for (const [op, p, p2] of c.ch) {
      if (op === 'A' || op === 'M') {
        const had = files.has(p) && !files.get(p).dying;   // a change to a file not seen yet is its coming
        touched.push([addFile(p, quiet), had ? 'M' : 'A']);
      } else if (op === 'D') {
        const f = files.get(p);
        if (f) {
          f.dying = quiet ? -99 : now;
          touched.push([f, 'D']);
        }
      } else if (op === 'R' && p2) {
        const f = files.get(p);
        if (f) {
          f.dying = quiet ? -99 : now;
        }
        touched.push([addFile(p2, quiet), 'A']);
      }
    }
    let who = people.get(c.who);
    if (!who) {
      who = { name: c.who, x: 0, y: 0, vx: 0, vy: 0, beams: [], last: now, alpha: 0 };
      const first = touched[0] ? touched[0][0] : null;
      if (first) {
        who.x = first.dir.x + 40;
        who.y = first.dir.y - 40;
      }
      people.set(c.who, who);
    }
    who.last = now;
    who.beams = quiet ? [] : touched.map(([f, op]) => ({ f, op, t: now }));
    if (c.vi !== verShown) {
      const fresh = verShown !== -2;
      verShown = c.vi;
      if (!quiet && fresh) {
        flash = { text: c.vi >= 0 ? hooks.versionName(c.vi) : '', t: now };
      }
      if (!quiet && c.vi >= 0) {
        hooks.onVersion(c.vi);
      }
    }
    if (quiet) {
      for (const f of files.values()) {
        if (f.dying) {
          remove(f);
        }
      }
    }
  }
  function remove(f) {
    files.delete(f.path);
    const list = f.dir.files;
    list.splice(list.indexOf(f), 1);
  }

  /* the springs: a folder to its parent, at the length their files need; all folders apart */
  function physics(dt) {
    for (const d of dirs.values()) {
      d.r = 7 + SPACE * Math.sqrt(d.files.length + 0.5);
    }
    const all = [...dirs.values()];
    for (const d of all) {
      if (!d.parent) {
        continue;
      }
      const p = d.parent;
      const dx = d.x - p.x;
      const dy = d.y - p.y;
      const dist = Math.hypot(dx, dy) || 0.01;
      const want = p.r + d.r + 16;
      const f = (dist - want) * 7 * dt;
      d.vx -= (dx / dist) * f;
      d.vy -= (dy / dist) * f;
      if (p.parent) {
        p.vx += (dx / dist) * f * 0.3;
        p.vy += (dy / dist) * f * 0.3;
      }
    }
    for (let i = 0; i < all.length; i++) {
      const a = all[i];
      for (let j = i + 1; j < all.length; j++) {
        const b = all[j];
        let dx = b.x - a.x;
        let dy = b.y - a.y;
        let d2 = dx * dx + dy * dy;
        if (d2 < 0.01) {
          dx = 0.1;
          dy = 0.1;
          d2 = 0.02;
        }
        const dist = Math.sqrt(d2);
        const room = a.r + b.r + 12;
        let f = (1600 / (d2 + 400)) * dt * 60;
        if (dist < room) {
          f += (room - dist) * 9 * dt;
        }
        const fx = (dx / dist) * f;
        const fy = (dy / dist) * f;
        if (a.parent) {
          a.vx -= fx;
          a.vy -= fy;
        }
        if (b.parent) {
          b.vx += fx;
          b.vy += fy;
        }
      }
    }
    const damp = Math.pow(0.02, dt);
    for (const d of all) {
      if (!d.parent) {
        d.x = 0;
        d.y = 0;
        continue;
      }
      d.vx *= damp;
      d.vy *= damp;
      d.x += d.vx * dt;
      d.y += d.vy * dt;
    }
    const ease = 1 - Math.exp(-dt * 7);
    for (const d of all) {
      d.files.forEach((f, k) => {                      // a sunflower round the folder: each file its own place
        const r = SPACE * Math.sqrt(k + 0.7);
        const a = k * 2.39996 + d.spin;
        f.x += (d.x + Math.cos(a) * r - f.x) * ease;
        f.y += (d.y + Math.sin(a) * r - f.y) * ease;
      });
    }
    for (const who of people.values()) {             // whoever wrote flies to what they touched, a little aside
      const live = who.beams.filter((b) => files.has(b.f.path) || b.op === 'D');
      if (live.length) {
        let tx = 0;
        let ty = 0;
        for (const b of live) {
          tx += b.f.x;
          ty += b.f.y;
        }
        tx /= live.length;
        ty /= live.length;
        const out = Math.hypot(tx, ty) || 1;
        tx += (tx / out) * 34;
        ty += (ty / out) * 34 - 10;
        who.vx += (tx - who.x) * 5 * dt;
        who.vy += (ty - who.y) * 5 * dt;
      }
      for (const other of people.values()) {           // two of them never on one spot
        if (other === who || other.alpha < 0.05) {
          continue;
        }
        const dx = who.x - other.x;
        const dy = who.y - other.y;
        const d = Math.hypot(dx, dy) || 0.1;
        if (d < 70) {
          who.vx += (dx / d) * (70 - d) * 6 * dt;
          who.vy += (dy / d) * (70 - d) * 6 * dt;
        }
      }
      who.vx *= Math.pow(0.08, dt);
      who.vy *= Math.pow(0.08, dt);
      who.x += who.vx * dt;
      who.y += who.vy * dt;
    }
  }

  /* the time: one step of playing and of everything easing */
  function step(dt, playing) {
    if (playing) {
      now += dt;
      while (next < commits.length && commits[next].at <= now) {
        apply(commits[next], false);
        next++;
        range.value = String(next);
      }
    }
    physics(Math.min(dt, 1 / 20));
    for (const f of [...files.values()]) {
      f.heat *= Math.exp(-dt / 1.1);
      f.alpha = f.dying ? 1 - clamp((now - f.dying) / 1.2, 0, 1) : clamp((now - f.born) / 0.5, 0, 1);
      if (f.dying && now - f.dying > 1.2) {
        remove(f);
      }
    }
    for (const d of [...dirs.values()]) {
      const has = d.files.length || d.kids.length;
      d.alpha += ((has ? 1 : 0) - d.alpha) * (1 - Math.exp(-dt * 3));
      if (!has && d.parent) {
        d.empty += dt;
        if (d.empty > 1.5) {
          dirs.delete(d.path);
          d.parent.kids.splice(d.parent.kids.indexOf(d), 1);
        }
      }
    }
    for (const who of people.values()) {
      const idle = now - who.last;
      who.alpha += ((idle < 4.5 ? 1 : 0) - who.alpha) * (1 - Math.exp(-dt * 2));
    }
    // the camera: all that grew in view, eased
    let x0 = Infinity;
    let y0 = Infinity;
    let x1 = -Infinity;
    let y1 = -Infinity;
    for (const d of dirs.values()) {
      x0 = Math.min(x0, d.x - d.r);
      y0 = Math.min(y0, d.y - d.r);
      x1 = Math.max(x1, d.x + d.r);
      y1 = Math.max(y1, d.y + d.r);
    }
    // the room left of the list of kinds and under the date
    const free = view();
    const s = clamp(Math.min(free.w / (x1 - x0 + 60), free.h / (y1 - y0 + 70)), 0.2, 3) * look.zoom;
    const k = 1 - Math.exp(-dt * (moving ? 14 : 1.6));
    cam.x += ((x0 + x1) / 2 - (free.cx - W / 2) / s + look.px - cam.x) * k;
    cam.y += ((y0 + y1) / 2 - (free.cy - H / 2) / s + look.py - cam.y) * k;
    cam.s += (s - cam.s) * k;
    const counts = KINDS.map(() => 0);
    for (const f of files.values()) {
      if (!f.dying) {
        counts[f.kind]++;
      }
    }
    shownCount = shownCount.map((v, i) => v + (counts[i] - v) * (1 - Math.exp(-dt * 5)));
  }

  function view() {                                    // where the tree goes: right of the list on a wide picture
    const left = W >= 420 ? 150 : 8;
    const top = 30;
    const bottom = W >= 420 ? 8 : 40;
    return { w: W - left - 8, h: H - top - bottom, cx: left + (W - left - 8) / 2, cy: top + (H - top - bottom) / 2 };
  }

  function realTime() {                                // the commits' own time where the playing is now
    if (next <= 0) {
      return commits[0].t;
    }
    if (next >= commits.length) {
      return commits[commits.length - 1].t;
    }
    const a = commits[next - 1];
    const b = commits[next];
    return a.t + ((b.t - a.t) * clamp((now - a.at) / (b.at - a.at), 0, 1));
  }

  let hover = null;
  function draw() {
    const sx = (x) => (x - cam.x) * cam.s + W / 2;
    const sy = (y) => (y - cam.y) * cam.s + H / 2;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.globalCompositeOperation = 'source-over';
    g.globalAlpha = 1;
    g.clearRect(0, 0, W, H);                           // the lights first on nothing, the night under them after
    g.globalCompositeOperation = 'lighter';
    // a haze round each folder in the colour most of its files have: the half tones under everything
    for (const d of dirs.values()) {
      if (!d.files.length) {
        continue;
      }
      const n = KINDS.map(() => 0);
      for (const f of d.files) {
        n[f.kind]++;
      }
      const kind = n.indexOf(Math.max(...n));
      const r = (d.r * 2.6 + 14) * cam.s;
      g.globalAlpha = 0.16 * d.alpha;
      g.drawImage(sprites[kind], sx(d.x) - r, sy(d.y) - r, r * 2, r * 2);
    }
    // the branches: a wide faint line under a thin bright one
    g.lineCap = 'round';
    for (const pass of [[6, 0.07], [1.4, 0.5]]) {
      g.lineWidth = pass[0];
      for (const d of dirs.values()) {
        if (!d.parent) {
          continue;
        }
        g.globalAlpha = pass[1] * d.alpha;
        g.strokeStyle = '#8fd8ff';
        g.beginPath();
        g.moveTo(sx(d.parent.x), sy(d.parent.y));
        g.lineTo(sx(d.x), sy(d.y));
        g.stroke();
      }
    }
    // the files: a glow, more of it while just touched, and a dot
    const dotR = clamp(2.1 * cam.s, 1.1, 3.4);
    for (const f of files.values()) {
      const x = sx(f.x);
      const y = sy(f.y);
      const r = (5 + 22 * f.heat) * clamp(cam.s, 0.6, 1.6);
      g.globalAlpha = f.alpha * (0.35 + 0.65 * f.heat);
      g.drawImage(sprites[f.kind], x - r, y - r, r * 2, r * 2);
      g.globalAlpha = f.alpha;
      g.fillStyle = KINDS[f.kind].colour;
      g.beginPath();
      g.arc(x, y, dotR * (1 + f.heat * 0.8), 0, Math.PI * 2);
      g.fill();
    }
    // whoever wrote: beams to the files, a spark running along each while it is new
    for (const who of people.values()) {
      if (who.alpha < 0.02) {
        continue;
      }
      const px = sx(who.x);
      const py = sy(who.y);
      for (const b of who.beams) {
        const age = (now - b.t) / BEAM_S;
        if (age >= 1) {
          continue;
        }
        const fx = sx(b.f.x);
        const fy = sy(b.f.y);
        g.globalAlpha = (1 - age) * 0.55 * who.alpha;
        g.strokeStyle = OP_COLOUR[b.op];
        g.lineWidth = 1.1;
        g.beginPath();
        g.moveTo(px, py);
        g.lineTo(fx, fy);
        g.stroke();
        const run = clamp(age * 3, 0, 1);
        const r = 7;
        g.globalAlpha = (1 - run) * who.alpha;
        g.drawImage(beamSprites[b.op], px + (fx - px) * run - r, py + (fy - py) * run - r, r * 2, r * 2);
      }
      const r = 16;
      g.globalAlpha = 0.8 * who.alpha;
      g.drawImage(whiteSprite, px - r, py - r, r * 2, r * 2);
    }
    if (hover && hover.f) {
      g.globalAlpha = 1;
      g.strokeStyle = '#eaf7ff';
      g.lineWidth = 1.2;
      g.beginPath();
      g.arc(sx(hover.f.x), sy(hover.f.y), dotR + 4, 0, Math.PI * 2);
      g.stroke();
    }
    // the bloom: all the light so far, small and blurred, laid over itself; then the night under it all
    if (bloom) {
      const o = bloom.getContext('2d');
      const ow = Math.ceil(canvas.width / 3);
      const oh = Math.ceil(canvas.height / 3);
      if (bloom.width !== ow || bloom.height !== oh) {
        bloom.width = ow;
        bloom.height = oh;
      }
      o.clearRect(0, 0, ow, oh);
      o.filter = 'blur(5px)';
      o.drawImage(canvas, 0, 0, ow, oh);
      g.setTransform(1, 0, 0, 1, 0, 0);
      g.globalAlpha = 0.85;
      g.drawImage(bloom, 0, 0, canvas.width, canvas.height);
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
    }
    g.globalCompositeOperation = 'destination-over';
    g.globalAlpha = 1;
    g.fillStyle = BG;
    g.fillRect(0, 0, W, H);
    // words over the light, each in a dark halo
    g.globalCompositeOperation = 'source-over';
    g.lineJoin = 'round';
    g.lineWidth = 3;
    g.strokeStyle = 'rgba(3, 8, 26, .85)';
    const say = (text, x, y) => {
      g.strokeText(text, x, y);
      g.fillText(text, x, y);
    };
    g.textBaseline = 'middle';
    g.font = '400 11px "Cascadia Mono", Consolas, ui-monospace, monospace';
    g.textAlign = 'center';
    for (const d of dirs.values()) {
      const depth = d.path ? d.path.split('/').length : 0;
      if (!d.parent || (depth > 1 && d.files.length < 10 && d !== (hover && hover.d))) {
        continue;
      }
      g.globalAlpha = 0.85 * d.alpha;
      g.fillStyle = depth === 1 ? '#eaf7ff' : '#a5c6e2';
      say(d.name, sx(d.x), sy(d.y) - d.r * cam.s - 7);
    }
    g.font = '700 12px "Cascadia Mono", Consolas, ui-monospace, monospace';
    for (const who of people.values()) {
      if (who.alpha < 0.02) {
        continue;
      }
      g.globalAlpha = who.alpha;
      g.fillStyle = '#eaf7ff';
      say(who.name, sx(who.x), sy(who.y) - 16);
    }
    // the date, as the commits have it
    g.globalAlpha = 0.9;
    g.fillStyle = '#a5c6e2';
    g.font = '400 12px "Cascadia Mono", Consolas, ui-monospace, monospace';
    const t = new Date(realTime() * 1000);
    const date = t.toLocaleDateString('en-GB', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' });
    const time = t.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });
    say(`${date}, ${time}`, W / 2, 16);
    when.textContent = `${next} of ${commits.length} commits`;
    // the kinds of file now, in the corner: a bar each as long as how many
    g.textAlign = 'left';
    g.font = '400 11px "Cascadia Mono", Consolas, ui-monospace, monospace';
    const order = KINDS.map((k, i) => i).filter((i) => shownCount[i] > 0.5).sort((a, b) => shownCount[b] - shownCount[a]);
    const most = Math.max(1, ...shownCount);
    if (W >= 420) {
      order.forEach((i, row) => {
        const y = 40 + row * 16;
        g.globalAlpha = 0.9;
        g.fillStyle = KINDS[i].colour;
        g.fillRect(10, y - 4, 4 + (46 * shownCount[i]) / most, 8);
        g.fillStyle = '#a5c6e2';
        say(`${KINDS[i].name} ${Math.round(shownCount[i])}`, 66, y);
      });
    } else {                                           // a narrow picture: the list in rows along the bottom
      g.font = '400 10px "Cascadia Mono", Consolas, ui-monospace, monospace';
      let x = 8;
      let y = H - 30;
      for (const i of order) {
        const text = `${KINDS[i].name} ${Math.round(shownCount[i])}`;
        const w = 14 + g.measureText(text).width + 10;
        if (x + w > W - 4) {
          x = 8;
          y += 14;
        }
        g.globalAlpha = 0.9;
        g.fillStyle = KINDS[i].colour;
        g.fillRect(x, y - 4, 8, 8);
        g.fillStyle = '#a5c6e2';
        say(text, x + 12, y);
        x += w;
      }
    }
    // a new version: its number big over everything, glowing and going
    if (flash && flash.text) {
      const age = (now - flash.t) / 2.6;
      if (age < 1) {
        g.globalAlpha = Math.sin(Math.PI * clamp(age * 1.4, 0, 1)) * 0.95;
        g.textAlign = 'center';
        g.font = `700 ${Math.round(clamp(W / 9, 34, 64))}px "Cascadia Mono", Consolas, ui-monospace, monospace`;
        g.fillStyle = '#5ce1ff';
        g.shadowColor = '#5ce1ff';
        g.shadowBlur = 26;
        g.fillText(flash.text, W / 2, H / 2 - 10 - age * 18);
        g.shadowBlur = 0;
      }
    }
    g.globalAlpha = 1;
  }

  /* the loop runs only while the picture is seen and something moves */
  let active = false;
  let playing = false;
  let raf = 0;
  let last = 0;
  let quietFor = 0;
  function frame(ms) {
    const dt = Math.max(0, Math.min(0.05, (ms - last) / 1000 || 0.016));   // a frame stamped before the loop woke: no step back
    last = ms;
    step(dt, playing);
    if (playing && now >= END) {
      playing = false;
      playBtn.textContent = 'PLAY AGAIN';
    }
    draw();
    quietFor = playing ? 0 : quietFor + dt;
    raf = active && (playing || moving || quietFor < 4) ? requestAnimationFrame(frame) : 0;
  }
  function wake() {
    quietFor = 0;
    if (active && !raf) {
      last = performance.now();
      raf = requestAnimationFrame(frame);
    }
  }
  /* to a commit at once: those before it laid down quiet, the springs let settle */
  function seek(k) {
    reset();
    k = clamp(k, 0, commits.length);
    for (let i = 0; i < k; i++) {
      now = commits[i].at;
      apply(commits[i], true);
    }
    next = k;
    now = k ? commits[k - 1].at + 0.01 : 0;
    for (let i = 0; i < 260; i++) {
      physics(1 / 30);
    }
    for (const f of files.values()) {
      f.alpha = 1;
    }
    for (let i = 0; i < 120; i++) {                  // the camera where it would have eased to
      step(1 / 30, false);
    }
    range.value = String(k);
    const vi = k ? commits[k - 1].vi : commits[0].vi;
    if (vi >= 0) {
      hooks.onVersion(vi);
    }
  }
  function play(fromStart) {
    if (fromStart || now >= END) {
      seek(0);
    }
    playing = !still;
    playBtn.textContent = playing ? 'PAUSE' : 'PLAY';
    wake();
  }

  playBtn.addEventListener('click', () => {
    if (playing) {
      playing = false;
      playBtn.textContent = 'PLAY';
    } else {
      play(now >= END);
    }
    wake();
  });
  range.addEventListener('input', () => {
    seek(Number(range.value));
    playing = false;
    playBtn.textContent = 'PLAY';
    draw();
    wake();
  });
  canvas.addEventListener('pointermove', (ev) => {
    const r = canvas.getBoundingClientRect();
    const mx = ev.clientX - r.left;
    const my = ev.clientY - r.top;
    const wx = (mx - W / 2) / cam.s + cam.x;
    const wy = (my - H / 2) / cam.s + cam.y;
    let best = null;
    let bestD = (9 / cam.s) ** 2;
    for (const f of files.values()) {
      const d2 = (f.x - wx) ** 2 + (f.y - wy) ** 2;
      if (d2 < bestD) {
        bestD = d2;
        best = { f };
      }
    }
    if (!best) {
      for (const d of dirs.values()) {
        if (d.parent && (d.x - wx) ** 2 + (d.y - wy) ** 2 < (d.r + 4) ** 2) {
          best = { d };
        }
      }
    }
    hover = best;
    if (best) {
      tip.innerHTML = best.f
        ? `<b>${best.f.path.replace(/&/g, '&amp;').replace(/</g, '&lt;')}</b> ${KINDS[best.f.kind].name}`
        : `<b>${best.d.path.replace(/&/g, '&amp;').replace(/</g, '&lt;')}/</b> ${best.d.files.length} files`;
      tip.style.left = `${clamp(mx + 12, 0, W - 240)}px`;
      tip.style.top = `${my + 14}px`;
      tip.hidden = false;
    } else {
      tip.hidden = true;
    }
    if (!raf) {
      draw();
    }
  });
  /* moving it by hand: a drag moves the tree, the wheel comes closer to where the pointer is (in the full screen,
   * or with ctrl: on the page the wheel scrolls the page), two fingers pinch; a double click brings the view back */
  let moving = false;
  let dragFrom = null;
  let pinch = null;
  const fingers = new Map();
  canvas.addEventListener('pointerdown', (ev) => {
    fingers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
    if (fingers.size === 2) {
      const [a, b] = [...fingers.values()];
      pinch = { d: Math.hypot(a.x - b.x, a.y - b.y) || 1, zoom: look.zoom };
      dragFrom = null;
    } else if (ev.pointerType !== 'touch' || full()) {   // a finger on the page scrolls the page
      dragFrom = { x: ev.clientX, y: ev.clientY, px: look.px, py: look.py };
      canvas.setPointerCapture(ev.pointerId);
    }
    moving = true;
    wake();
  });
  const lifted = (ev) => {
    fingers.delete(ev.pointerId);
    if (fingers.size < 2) {
      pinch = null;
    }
    if (!fingers.size) {
      dragFrom = null;
      moving = false;
      told();
    }
  };
  canvas.addEventListener('pointerup', lifted);
  canvas.addEventListener('pointercancel', lifted);
  canvas.addEventListener('pointermove', (ev) => {
    if (fingers.has(ev.pointerId)) {
      fingers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
    }
    if (pinch && fingers.size === 2) {
      const [a, b] = [...fingers.values()];
      look.zoom = clamp((pinch.zoom * Math.hypot(a.x - b.x, a.y - b.y)) / pinch.d, 0.4, 12);
      wake();
    } else if (dragFrom) {
      look.px = dragFrom.px - (ev.clientX - dragFrom.x) / cam.s;
      look.py = dragFrom.py - (ev.clientY - dragFrom.y) / cam.s;
      wake();
    }
  });
  canvas.addEventListener('wheel', (ev) => {
    if (!full() && !ev.ctrlKey) {
      return;                                          // the page's own scroll
    }
    ev.preventDefault();
    const r = canvas.getBoundingClientRect();
    const mx = ev.clientX - r.left - W / 2;
    const my = ev.clientY - r.top - H / 2;
    const f = Math.exp(-ev.deltaY * 0.0015);
    const zoom = clamp(look.zoom * f, 0.4, 12);
    const k = zoom / look.zoom;
    look.zoom = zoom;
    // the point under the pointer stays under it: the camera moves towards it by what the zoom took away
    look.px += (mx / cam.s) * (1 - 1 / k);
    look.py += (my / cam.s) * (1 - 1 / k);
    cam.x += (mx / cam.s) * (1 - 1 / k);
    cam.y += (my / cam.s) * (1 - 1 / k);
    cam.s *= k;
    told();
    wake();
  }, { passive: false });
  const told = () => {
    if (hooks.onFocus) {
      hooks.onFocus({ v: -1, part: -1, moved: look.zoom !== 1 || look.px !== 0 || look.py !== 0 });
    }
  };
  function showAll() {
    look.zoom = 1;
    look.px = 0;
    look.py = 0;
    told();
    wake();
  }
  canvas.addEventListener('dblclick', showAll);
  canvas.addEventListener('pointerleave', () => {
    hover = null;
    tip.hidden = true;
    if (!raf) {
      draw();
    }
  });

  fit();
  reset();
  let lastSize = '';
  const refit = () => {
    const size = `${box.clientWidth}x${window.innerHeight}:${full()}`;
    if (box.clientWidth && size !== lastSize) {
      lastSize = size;
      fit();
      if (!raf) {
        draw();
      }
      wake();
    }
  };
  new ResizeObserver(refit).observe(box);
  window.addEventListener('resize', refit);

  let started = false;
  return {
    showAll,
    /* seen or not: the loop stops when it is not. the first time it is seen it plays from the first commit */
    setActive(on) {
      active = on;
      if (!on) {
        if (raf) {
          cancelAnimationFrame(raf);
          raf = 0;
        }
        return;
      }
      refit();                                         // made hidden, its box had no width then
      if (!started) {
        started = true;
        if (still) {
          seek(commits.length);
          draw();
          return;
        }
        play(true);
        return;
      }
      draw();
      wake();
    },
    /* the panel's version buttons: from that version's first commit, played on */
    seekVersion(vi) {
      const k = commits.findIndex((c) => c.vi === vi);
      if (k < 0) {
        return;
      }
      started = true;
      seek(k);
      playing = !still;
      playBtn.textContent = playing ? 'PAUSE' : 'PLAY';
      draw();
      wake();
    },
  };
}
