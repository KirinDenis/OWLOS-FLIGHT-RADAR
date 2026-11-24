/* the radar's history as a tower of time, in WebGL (three.js, site/vendor, MIT): the floor is the radar's dial cut
 * into the parts of the product (the screen's code, the boards, the site, the server, the tools, the pictures, the
 * texts), and the height is time, a commit a step up. a file is a thread rising from the commit that brought it to
 * the one that took it away, thicker the more of its lines were changed; a commit is a bead on the axis with curved
 * light to each file it touched, gathered through its part's gate, so what changed together shows; a version is a
 * glass floor with its number. hovering a bead says the commit's own words. history.txt is the workflow's
 * (.github/history.sh): of the people their names only. no WebGL here: null, and the page keeps its other views.
 *
 * one version is lit at a time, the rest of the tower stands dim: while it plays the version being written, after
 * it the one picked, by its floor's number or the panel. a part's name on the ground leaves that part alone lit.
 * all of it lit at once was a white haze where no structure showed */

import { versionSteps } from './growth.js';

const PARTS = [
  { name: 'Screen', where: 'core/', colour: '#4fc8ff', test: (p) => p.startsWith('core/') },
  { name: 'Boards', where: 'firmware/', colour: '#7dffa8', test: (p) => p.startsWith('firmware/') },
  { name: 'Site', where: 'site/ web/', colour: '#ffd25c', test: (p) => p.startsWith('site/') || p.startsWith('web/') },
  { name: 'Server', where: 'server/', colour: '#ff5fa8', test: (p) => p.startsWith('server/') },
  { name: 'Tools', where: 'tools/', colour: '#ff8a5c', test: (p) => p.startsWith('tools/') },
  { name: 'Pictures', where: 'shots/', colour: '#b48cff', test: (p) => p.startsWith('shots/') },
  { name: 'Texts and build', where: 'README, CHANGES, .github', colour: '#a9b8cc', test: () => true },
];
const GONE = '#ff6070';                                // a file taken out: the one red in the tower
const R = 100;                                         // the floor's radius
const R0 = 20;                                         // where the files begin, out from the axis
const STEP = 3.4;                                      // a commit's height
const HOUR_S = 0.3;                                    // seconds of playing an hour between two commits
const GAP_MIN = 0.45;
const GAP_MAX = 1.6;
const TURN = 0.075;                                    // the tower's own slow turn while it plays, rad/s
const BG = '#03081a';
const LIT = 1;                                         // a thread's brightness in the version lit
const DIM = 0.36;                                      // and out of it: the tower as context, still read

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
function hash(s) {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) {
    h = Math.imul(h ^ s.charCodeAt(i), 16777619);
  }
  return (h >>> 0) / 4294967296;
}

/* history.txt into commits, the oldest first, and files, each with its lives and its touches */
function readHistory(text) {
  const commits = [];
  let c = null;
  for (const line of text.split(/\r?\n/)) {
    const f = line.split('\t');
    if (f[0] === 'C' && f.length >= 4) {
      c = { t: Number(f[1]), ver: f[2], who: f[3], subject: f.slice(4).join(' ').trim(), ops: [], lines: new Map() };
      commits.push(c);
    } else if (c && /^[AMDR]$/.test(f[0]) && f[1]) {
      c.ops.push(f);
    } else if (c && f[0] === 'N' && f.length >= 4) {
      const n = (Number(f[1]) || 0) + (Number(f[2]) || 0);
      c.lines.set(f[3], { add: Number(f[1]) || 0, del: Number(f[2]) || 0, n });
    }
  }
  const list = commits.filter((x) => Number.isFinite(x.t)).sort((a, b) => a.t - b.t);
  const files = new Map();
  list.forEach((k, ci) => {
    k.touch = [];
    const touch = (path, op) => {
      let f = files.get(path);
      if (!f) {
        f = { path, part: PARTS.findIndex((p) => p.test(path)), lives: [], touches: [], total: 0 };
        files.set(path, f);
      }
      const life = f.lives[f.lives.length - 1];
      if (op !== 'D' && (!life || life.to !== null)) {
        f.lives.push({ from: ci, to: null });
      }
      if (op === 'D' && life && life.to === null) {
        life.to = ci;
      }
      const ln = k.lines.get(path);
      const n = ln ? ln.n : 0;
      f.total += n;
      const t = { ci, op, n, add: ln ? ln.add : 0, del: ln ? ln.del : 0, file: f, sum: f.total };
      f.touches.push(t);
      k.touch.push(t);
    };
    for (const [op, p, p2] of k.ops) {
      if (op === 'R' && p2) {
        touch(p, 'D');
        touch(p2, 'A');
      } else {
        touch(p, op);
      }
    }
  });
  return { commits: list, files };
}

/* box: where the picture goes; hooks.versionIndex, hooks.versionName and hooks.onVersion as growth.js has them */
export async function startTower(box, text, hooks) {
  const { commits, files } = readHistory(text);
  if (commits.length < 2) {
    return null;
  }
  let THREE;
  try {
    THREE = await import('./vendor/three.module.js');
  } catch {
    return null;
  }
  const canvas = document.createElement('canvas');
  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ canvas, antialias: false, powerPreference: 'high-performance' });
  } catch {
    return null;                                       // no WebGL: the other views stay
  }
  THREE.ColorManagement.enabled = false;               // the colours as written, the same in every pass
  renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
  canvas.setAttribute('role', 'img');
  canvas.setAttribute('aria-label', 'The radar\'s history as a tower: the floor cut into the parts of the product, time rising, each file a thread, each commit a bead with light to the files it changed');
  box.appendChild(canvas);
  const labels = document.createElement('div');
  labels.className = 'towerlabels';
  box.appendChild(labels);
  const caption = document.createElement('div');
  caption.className = 'towercaption';
  box.appendChild(caption);
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
  const playBtn = bar.querySelector('button');
  const range = bar.querySelector('input');
  const when = bar.querySelector('.growwhen');
  range.max = String(commits.length);
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const full = () => Boolean(box.closest('.win.maxed'));   // its window over the whole page

  let at = 0.6;
  commits.forEach((c, i) => {
    if (i) {
      at += clamp(((c.t - commits[i - 1].t) / 3600) * HOUR_S, GAP_MIN, GAP_MAX);
    }
    c.at = at;
    c.y = i * STEP;
  });
  versionSteps(commits, hooks.versionIndex);
  const END = at + 2;
  for (const f of files.values()) {
    f.vers = new Set(f.touches.map((t) => commits[t.ci].vi));
  }
  const spans = new Map();                             // each version's first and last commit
  commits.forEach((c, i) => {
    const s = spans.get(c.vi);
    if (s) {
      s.to = i;
    } else {
      spans.set(c.vi, { from: i, to: i });
    }
  });

  /* ---- the floor plan: the parts as sectors, as wide as the root of how many files each ever had ---- */
  const count = PARTS.map(() => 0);
  for (const f of files.values()) {
    count[f.part]++;
  }
  const GAP = 0.07;
  const weights = count.map((n) => (n ? Math.sqrt(n) + 1.5 : 0));
  const wsum = weights.reduce((a, b) => a + b, 0);
  const used = PARTS.filter((p, i) => count[i]).length;
  let a0 = -Math.PI / 2;
  const sectors = PARTS.map((p, i) => {
    const width = count[i] ? ((Math.PI * 2 - GAP * used) * weights[i]) / wsum : 0;
    const s = { start: a0, width, mid: a0 + width / 2 };
    a0 += width + (count[i] ? GAP : 0);
    return s;
  });
  for (let i = 0; i < PARTS.length; i++) {             // a part's files in path order round its sector
    const mine = [...files.values()].filter((f) => f.part === i).sort((a, b) => a.path.localeCompare(b.path));
    mine.forEach((f, k) => {
      const a = sectors[i].start + ((k + 0.5) / mine.length) * sectors[i].width;
      const r = R0 + (R - R0) * (0.12 + 0.88 * ((k * 0.6180339 + hash(f.path) * 0.2) % 1));
      f.a = a;
      f.r = r;
      f.x = Math.cos(a) * r;
      f.z = Math.sin(a) * r;
    });
  }
  const maxSum = Math.max(1, ...[...files.values()].map((f) => f.total));

  /* ---- the scene ---- */
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(BG);
  const camera = new THREE.PerspectiveCamera(38, 1, 1, 5000);
  const additive = { transparent: true, depthWrite: false, blending: THREE.AdditiveBlending };
  const col = (hex, k = 1) => new THREE.Color(hex).multiplyScalar(k);

  // the dial on the ground: rings, the sectors' edges, the ring of degrees
  const ground = new THREE.Group();
  scene.add(ground);
  const lineMat = (hex, o) => new THREE.LineBasicMaterial({ color: col(hex), opacity: o, ...additive });
  for (const r of [R0, 40, 60, 80, R]) {
    const pts = [];
    for (let i = 0; i <= 128; i++) {
      const a = (i / 128) * Math.PI * 2;
      pts.push(new THREE.Vector3(Math.cos(a) * r, 0, Math.sin(a) * r));
    }
    ground.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), lineMat('#2a6aa5', r === R ? 0.8 : 0.35)));
  }
  {
    const pts = [];
    for (let d = 0; d < 360; d += 5) {
      const a = (d * Math.PI) / 180;
      const r1 = R + 6;
      const r2 = R + (d % 30 ? 9 : 13);
      pts.push(new THREE.Vector3(Math.cos(a) * r1, 0, Math.sin(a) * r1), new THREE.Vector3(Math.cos(a) * r2, 0, Math.sin(a) * r2));
    }
    ground.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(pts), lineMat('#1d4a78', 0.9)));
    const edges = [];
    sectors.forEach((s, i) => {
      if (!count[i]) {
        return;
      }
      for (const a of [s.start, s.start + s.width]) {
        edges.push(new THREE.Vector3(Math.cos(a) * R0, 0, Math.sin(a) * R0), new THREE.Vector3(Math.cos(a) * R, 0, Math.sin(a) * R));
      }
    });
    ground.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(edges), lineMat('#1d4a78', 0.7)));
  }
  // a faint glass under each sector, in its part's colour: the half tones of the floor
  const glass = sectors.map((s, i) => {
    if (!count[i]) {
      return null;
    }
    const g = new THREE.RingGeometry(R0, R, 48, 1, s.start, s.width);
    g.rotateX(Math.PI / 2);
    const m = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ color: col(PARTS[i].colour), opacity: 0.045, side: THREE.DoubleSide, ...additive }));
    ground.add(m);
    return m;
  });

  // the threads: one cylinder a stretch of a file's life between two touches, thicker as its lines add up. plain,
  // not added light: the tower in front hides the one behind, and what is lit stands out of what is not
  // cut where each version begins, so a version lights only its own stretch of a thread, not all of it above
  const starts = [...spans.values()].map((s) => s.from).filter((x) => x > 0).sort((a, b) => a - b);
  const segs = [];
  for (const f of files.values()) {
    for (const life of f.lives) {
      const mine = f.touches.filter((t) => t.ci >= life.from && (life.to === null || t.ci <= life.to));
      mine.forEach((t, k) => {
        const next = mine[k + 1];
        const end = next ? next.ci : life.to;
        if (end !== null && end <= t.ci) {
          return;
        }
        const r = 0.22 + 1.3 * Math.sqrt(t.sum / maxSum);
        let from = t.ci;
        for (const b of [...starts.filter((x) => x > t.ci && (end === null || x < end)), end]) {
          segs.push({ f, from, to: b, r });
          from = b;
        }
      });
    }
  }
  const cyl = new THREE.CylinderGeometry(1, 1, 1, 6, 1, true);
  cyl.translate(0, 0.5, 0);
  const threads = new THREE.InstancedMesh(cyl, new THREE.MeshBasicMaterial({ color: 0xffffff }), segs.length);
  threads.frustumCulled = false;
  scene.add(threads);

  // the beads: each touch on its thread, as big as how many lines it changed, shown in the version lit; the
  // commits' beads on the axis, all of them, the lit version's brighter
  const touches = [];
  for (const f of files.values()) {
    for (const t of f.touches) {
      touches.push(t);
    }
  }
  const ball = new THREE.IcosahedronGeometry(1, 1);
  const beads = new THREE.InstancedMesh(ball, new THREE.MeshBasicMaterial({ color: 0xffffff }), touches.length);
  beads.frustumCulled = false;
  touches.forEach((t, i) => {
    t.size = Math.min(2.2, 0.35 + 0.2 * Math.log2(1 + t.n));
    beads.setColorAt(i, t.op === 'D' ? col(GONE) : col(PARTS[t.file.part].colour).lerp(new THREE.Color('#ffffff'), 0.15));
  });
  scene.add(beads);
  const hubs = new THREE.InstancedMesh(ball, new THREE.MeshBasicMaterial({ color: 0xffffff }), commits.length);
  hubs.frustumCulled = false;
  scene.add(hubs);

  // the light from a commit to its files, curved through its part's gate: a part's lines run together. one line
  // set a commit and a part, so a part alone can be lit
  const pos = (f, y) => new THREE.Vector3(f.x, y, f.z);
  const arcs = [];
  commits.forEach((c, ci) => {
    const byPart = PARTS.map(() => ({ p: [], cl: [] }));
    const hub = new THREE.Vector3(0, c.y, 0);
    for (const t of c.touch) {
      const f = t.file;
      const s = sectors[f.part];
      const gate = new THREE.Vector3(Math.cos(s.mid) * R0 * 0.8, c.y + 1.6, Math.sin(s.mid) * R0 * 0.8);
      const near = new THREE.Vector3(Math.cos(f.a) * f.r * 0.55, c.y + 2.2, Math.sin(f.a) * f.r * 0.55);
      const curve = new THREE.CatmullRomCurve3([hub, gate, near, pos(f, c.y)]);
      const pts = curve.getPoints(18);
      const to = col(t.op === 'D' ? GONE : PARTS[f.part].colour);
      const from = to.clone().multiplyScalar(0.2);     // dim at the axis, its part's colour out at the file
      const into = byPart[f.part];
      for (let k = 0; k < pts.length - 1; k++) {
        into.p.push(pts[k].x, pts[k].y, pts[k].z, pts[k + 1].x, pts[k + 1].y, pts[k + 1].z);
        const c1 = from.clone().lerp(to, k / (pts.length - 1));
        const c2 = from.clone().lerp(to, (k + 1) / (pts.length - 1));
        into.cl.push(c1.r, c1.g, c1.b, c2.r, c2.g, c2.b);
      }
      t.curve = curve;
    }
    byPart.forEach((b, part) => {
      if (!b.p.length) {
        return;
      }
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute(b.p, 3));
      g.setAttribute('color', new THREE.Float32BufferAttribute(b.cl, 3));
      const line = new THREE.LineSegments(g, new THREE.LineBasicMaterial({ vertexColors: true, opacity: 0, ...additive }));
      line.visible = false;
      scene.add(line);
      arcs.push({ ci, part, line });
    });
  });

  // the sparks running out along the newest commit's light
  const dot = document.createElement('canvas');
  dot.width = 64;
  dot.height = 64;
  {
    const d = dot.getContext('2d');
    const gr = d.createRadialGradient(32, 32, 0, 32, 32, 32);
    gr.addColorStop(0, 'rgba(255,255,255,1)');
    gr.addColorStop(0.25, 'rgba(255,255,255,.5)');
    gr.addColorStop(1, 'rgba(255,255,255,0)');
    d.fillStyle = gr;
    d.fillRect(0, 0, 64, 64);
  }
  const dotTex = new THREE.CanvasTexture(dot);
  const SPARKS = 160;
  const sparkGeo = new THREE.BufferGeometry();
  sparkGeo.setAttribute('position', new THREE.Float32BufferAttribute(new Float32Array(SPARKS * 3), 3));
  sparkGeo.setAttribute('color', new THREE.Float32BufferAttribute(new Float32Array(SPARKS * 3), 3));
  const sparks = new THREE.Points(sparkGeo, new THREE.PointsMaterial({ size: 7, map: dotTex, vertexColors: true, sizeAttenuation: true, ...additive }));
  sparks.frustumCulled = false;
  scene.add(sparks);

  // the versions: a glass floor each, where its first commit is; its number picks it
  const floors = [];
  commits.forEach((c, i) => {
    if (c.vi < 0 || floors.some((f) => f.vi === c.vi)) {
      return;
    }
    const disc = new THREE.Mesh(new THREE.CircleGeometry(R + 4, 96).rotateX(-Math.PI / 2),
      new THREE.MeshBasicMaterial({ color: col('#5ce1ff'), opacity: 0, side: THREE.DoubleSide, ...additive }));
    disc.position.y = c.y;
    const pts = [];
    for (let k = 0; k <= 160; k++) {
      const a = (k / 160) * Math.PI * 2;
      pts.push(new THREE.Vector3(Math.cos(a) * (R + 4), c.y, Math.sin(a) * (R + 4)));
    }
    const rim = new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), lineMat('#5ce1ff', 0));
    scene.add(disc, rim);
    const label = document.createElement('button');
    label.type = 'button';
    label.className = 'towerfloor';
    label.textContent = hooks.versionName(c.vi);
    label.addEventListener('click', () => {
      if (focus.v === c.vi) {                         // picked again: let go, back to the newest
        focus.v = -1;
        view.lift = 0;
        view.zoom = 1;
        told();
        wake();
        hooks.onVersion(verShown);
        return;
      }
      pickVersion(c.vi);
      hooks.onVersion(c.vi);
    });
    labels.appendChild(label);
    floors.push({ ci: i, vi: c.vi, disc, rim, label });
  });

  // the top: the glass of now, rising, and the radar's beam turning on it
  const nowRim = new THREE.Line(new THREE.BufferGeometry().setFromPoints(
    Array.from({ length: 161 }, (v, k) => new THREE.Vector3(Math.cos((k / 160) * Math.PI * 2) * R, 0, Math.sin((k / 160) * Math.PI * 2) * R))),
  lineMat('#bff3ff', 0.7));
  scene.add(nowRim);
  const beam = (() => {
    const n = 40;
    const width = 0.9;
    const p = [0, 0, 0];
    const cl = [0.36, 0.88, 1, 0];
    for (let k = 0; k <= n; k++) {
      const a = -width + (k / n) * width;
      p.push(Math.cos(a) * R, 0, Math.sin(a) * R);
      const e = Math.pow(k / n, 3);
      cl.push(0.36 + 0.4 * e, 0.88 + 0.08 * e, 1, 0.24 * e);
    }
    const idx = [];
    for (let k = 1; k <= n; k++) {
      idx.push(0, k, k + 1);
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(p, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(cl, 4));
    g.setIndex(idx);
    const m = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide, ...additive }));
    scene.add(m);
    return m;
  })();

  // the parts' names on the ground: a click leaves that part alone lit, again brings them all back
  const partLabels = sectors.map((s, i) => {
    if (!count[i]) {
      return null;
    }
    const d = document.createElement('button');
    d.type = 'button';
    d.className = 'towerpart';
    d.innerHTML = `<i style="background:${PARTS[i].colour}"></i>${esc(PARTS[i].name)}<small>${esc(PARTS[i].where)}</small>`;
    d.addEventListener('click', () => {
      setPart(focus.part === i ? -1 : i);
    });
    labels.appendChild(d);
    return { d, p: new THREE.Vector3(Math.cos(s.mid) * (R + 22), 0, Math.sin(s.mid) * (R + 22)) };
  });

  /* ---- the bloom: the picture drawn to a texture, its bright light blurred small twice, laid over it. only what
   * is lit is bright enough to bloom: the dim tower stays sharp ---- */
  const vs = 'varying vec2 vUv; void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }';
  const quadMat = (fs, uniforms) => new THREE.ShaderMaterial({ vertexShader: vs, fragmentShader: fs, uniforms, depthTest: false, depthWrite: false });
  const bright = quadMat('uniform sampler2D tex; varying vec2 vUv; void main() { vec3 c = texture2D(tex, vUv).rgb;' +
    ' gl_FragColor = vec4(max(c - vec3(0.38), vec3(0.0)), 1.0); }', { tex: { value: null } });
  const blur = quadMat('uniform sampler2D tex; uniform vec2 dir; varying vec2 vUv; void main() {' +
    ' vec3 c = texture2D(tex, vUv).rgb * 0.2270;' +
    ' c += (texture2D(tex, vUv + dir * 1.3846).rgb + texture2D(tex, vUv - dir * 1.3846).rgb) * 0.3162;' +
    ' c += (texture2D(tex, vUv + dir * 3.2308).rgb + texture2D(tex, vUv - dir * 3.2308).rgb) * 0.0703;' +
    ' gl_FragColor = vec4(c, 1.0); }', { tex: { value: null }, dir: { value: new THREE.Vector2() } });
  const mix = quadMat('uniform sampler2D tex; uniform sampler2D glow; uniform sampler2D glow2; varying vec2 vUv; void main() {' +
    ' vec3 c = texture2D(tex, vUv).rgb + texture2D(glow, vUv).rgb * 0.8 + texture2D(glow2, vUv).rgb * 0.7;' +
    ' gl_FragColor = vec4(c, 1.0); }', { tex: { value: null }, glow: { value: null }, glow2: { value: null } });
  const quad = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), bright);
  const quadScene = new THREE.Scene();
  quadScene.add(quad);
  const quadCam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
  const rtOpt = { type: THREE.HalfFloatType, depthBuffer: false };
  const rtScene = new THREE.WebGLRenderTarget(4, 4, { type: THREE.HalfFloatType, samples: 4 });
  const rtA = new THREE.WebGLRenderTarget(4, 4, rtOpt);
  const rtB = new THREE.WebGLRenderTarget(4, 4, rtOpt);
  const rtC = new THREE.WebGLRenderTarget(4, 4, rtOpt);
  const rtD = new THREE.WebGLRenderTarget(4, 4, rtOpt);
  function pass(mat, target) {
    quad.material = mat;
    renderer.setRenderTarget(target);
    renderer.render(quadScene, quadCam);
  }
  function blurInto(src, tmp, w, h, spread) {
    blur.uniforms.tex.value = src.texture;
    blur.uniforms.dir.value.set(spread / w, 0);
    pass(blur, tmp);
    blur.uniforms.tex.value = tmp.texture;
    blur.uniforms.dir.value.set(0, spread / h);
    pass(blur, src);
  }

  let W = 0;
  let H = 0;
  function fit() {
    W = Math.max(200, box.clientWidth);
    if (full()) {                                      // the full screen: all the height under the tabs and over the bar
      // down to the bottom of the window's body, less its padding, the bar and the hint under the picture
      const body = box.closest('.winbody');
      const bottom = body ? body.getBoundingClientRect().bottom - 14 : window.innerHeight - 12;
      H = Math.round(Math.max(240, bottom - box.getBoundingClientRect().top - bar.offsetHeight - hint.offsetHeight - 22));
    } else {
      H = Math.round(W < 520 ? clamp(W * 1.25, 380, 620) : clamp(W * 0.95, 320, 680));   // a phone: taller, the words on top need it
    }
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    renderer.setPixelRatio(dpr);
    renderer.setSize(W, H, false);
    canvas.style.width = `${W}px`;
    canvas.style.height = `${H}px`;
    camera.aspect = W / H;
    camera.updateProjectionMatrix();
    const pw = Math.round(W * dpr);
    const ph = Math.round(H * dpr);
    rtScene.setSize(pw, ph);
    rtA.setSize(pw >> 1, ph >> 1);
    rtB.setSize(pw >> 1, ph >> 1);
    rtC.setSize(pw >> 2, ph >> 2);
    rtD.setSize(pw >> 2, ph >> 2);
    hint.textContent = full()
      ? 'Drag to turn it, shift or the right button to go up and down, the wheel to come closer. Double click: back.'
      : 'Drag to turn it, ctrl and the wheel to come closer, or open the window to the whole screen. A floor\'s number or a part\'s name lights it alone.';
  }

  /* ---- time, and what is lit ---- */
  let now = 0;                                         // seconds of playing
  let done = -1;                                       // the last commit come
  let yNow = 0;                                        // the top, eased up to it
  let verShown = -2;
  let playing = false;
  let turning = 0;                                     // seconds left of the slow turn after the playing
  const focus = { v: -1, part: -1 };                   // a version picked (-1: the one being played), a part alone
  const litV = () => (focus.v >= 0 ? focus.v : verShown);
  const cam = { az: -2.2, el: 0.42, dist: 430, y: 40, userAt: -1e9 };
  const want = { dist: 430, y: 40 };
  const view = { zoom: 1, lift: 0 };                   // what the person did to the camera
  const m4 = new THREE.Matrix4();
  const q = new THREE.Quaternion();
  const v3 = new THREE.Vector3();
  const s3 = new THREE.Vector3();
  const zero = new THREE.Matrix4().makeScale(0, 0, 0);

  /* what is lit and how the camera stands, told to the menu beside the picture after each change */
  const told = () => {
    if (hooks.onFocus) {
      hooks.onFocus({ v: focus.v, part: focus.part, moved: view.zoom !== 1 || view.lift !== 0 || cam.el !== 0.42 });
    }
  };
  function setPart(i) {
    focus.part = i;
    told();
    wake();
  }
  function showAll() {                                 // everything back: the newest lit, all parts, the first view
    view.zoom = 1;
    view.lift = 0;
    cam.el = 0.42;
    focus.v = -1;
    focus.part = -1;
    if (done >= 0) {
      say(commits[done]);
    }
    if (verShown >= 0) {
      hooks.onVersion(verShown);
    }
    told();
    wake();
  }
  let painted = '';
  function paint() {                                   // the colours for what is lit, only when that changes
    const v = litV();
    const key = `${v}:${focus.part}`;
    if (key === painted) {
      return;
    }
    painted = key;
    segs.forEach((s, i) => {
      const lit = commits[s.from].vi === v && s.f.vers.has(v);
      threads.setColorAt(i, col(PARTS[s.f.part].colour, lit ? LIT : DIM));
    });
    threads.instanceColor.needsUpdate = true;
    commits.forEach((c, i) => {
      hubs.setColorAt(i, col('#bff3ff', c.vi === v ? 1 : 0.4));
    });
    hubs.instanceColor.needsUpdate = true;
    glass.forEach((g, i) => {
      if (g) {
        g.material.opacity = focus.part < 0 || focus.part === i ? 0.045 : 0.012;
      }
    });
    for (const l of partLabels) {
      if (l) {
        l.d.classList.toggle('off', focus.part >= 0 && l !== partLabels[focus.part]);
      }
    }
  }

  function arrive(i, quiet) {                          // a commit comes: its version, its caption
    done = i;
    range.value = String(i + 1);
    const c = commits[i];
    if (c.vi !== verShown) {
      verShown = c.vi;
      if (c.vi >= 0 && !quiet) {
        hooks.onVersion(c.vi);
      }
      for (const f of floors) {
        if (f.ci === i && !quiet) {
          f.flash = now;
        }
      }
    }
    if (!quiet) {
      say(c);
    }
  }
  function say(c) {
    const when = new Date(c.t * 1000);
    const date = when.toLocaleDateString('en-GB', { weekday: 'short', day: 'numeric', month: 'short' });
    const time = when.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });
    const byPart = PARTS.map(() => ({ add: 0, del: 0, n: 0 }));
    for (const t of c.touch) {
      const b = byPart[t.file.part];
      b.add += t.add;
      b.del += t.del;
      b.n++;
    }
    const parts = byPart.map((b, i) => (b.n ? `<span><i style="background:${PARTS[i].colour}"></i>${esc(PARTS[i].name)} ` +
      `${b.add || b.del ? `+${b.add} &minus;${b.del}` : `${b.n} ${b.n === 1 ? 'file' : 'files'}`}</span>` : '')).join('');
    const words = c.subject.length > 150 ? `${c.subject.slice(0, 147)}...` : c.subject;
    caption.innerHTML = `<div class="tcwhen">${esc(date)}, ${esc(time)} &middot; ${esc(c.who)}</div><div class="tcwords">${esc(words)}</div>` +
      `<div class="tcparts">${parts}</div>`;
  }
  /* a version picked: lit alone, and the camera to its floors, a little closer */
  function pickVersion(vi) {
    focus.v = vi;
    const s = spans.get(vi);
    if (s) {
      const yMid = (commits[s.from].y + commits[s.to].y) / 2;
      view.lift = yMid - Math.max(yNow, 30) * 0.44;
      view.zoom = Math.min(view.zoom, 0.72);
      say(commits[s.to]);
    }
    cam.userAt = performance.now();
    told();
    wake();
  }

  function update(dt) {
    if (playing) {
      now += dt;
      while (done + 1 < commits.length && commits[done + 1].at <= now) {
        arrive(done + 1, false);
      }
      if (now >= END) {
        playing = false;
        playBtn.textContent = 'PLAY AGAIN';
        turning = 20;
      }
    }
    paint();
    const v = litV();
    const top = done < 0 ? 0 : commits[done].y;
    yNow += (top - yNow) * (1 - Math.exp(-dt * 4));
    // the threads, up to the top as it is now
    segs.forEach((s, i) => {
      if (s.from > done || (focus.part >= 0 && s.f.part !== focus.part)) {   // a part alone: the others gone, not
        threads.setMatrixAt(i, zero);                                       // dark bars standing in front of it
        return;
      }
      const y0 = commits[s.from].y;
      const y1 = s.to !== null && s.to <= done ? commits[s.to].y : yNow;
      if (y1 <= y0 + 0.01) {
        threads.setMatrixAt(i, zero);
        return;
      }
      m4.compose(v3.set(s.f.x, y0, s.f.z), q.identity(), s3.set(s.r, y1 - y0, s.r));
      threads.setMatrixAt(i, m4);
    });
    threads.instanceMatrix.needsUpdate = true;
    touches.forEach((t, i) => {
      const c = commits[t.ci];
      if (t.ci > done || c.vi !== v || (focus.part >= 0 && t.file.part !== focus.part)) {
        beads.setMatrixAt(i, zero);
        return;
      }
      const age = now - c.at;
      const pop = playing && age < 1.2 ? 1 + 1.6 * Math.exp(-age * 3) : 1;
      const k = t.size * pop;
      m4.compose(v3.set(t.file.x, c.y, t.file.z), q.identity(), s3.set(k, k, k));
      beads.setMatrixAt(i, m4);
    });
    beads.instanceMatrix.needsUpdate = true;
    commits.forEach((c, i) => {
      if (i > done) {
        hubs.setMatrixAt(i, zero);
        return;
      }
      const k = i === done && playing ? 1.6 : c.vi === v ? 1 : 0.45;
      m4.compose(v3.set(0, c.y, 0), q.identity(), s3.set(k, k, k));
      hubs.setMatrixAt(i, m4);
    });
    hubs.instanceMatrix.needsUpdate = true;
    // the light: the lit version's commits, the newest brightest; a commit under the pointer whatever its version
    for (const a of arcs) {
      const c = commits[a.ci];
      const shown = a.ci <= done && (focus.part < 0 || a.part === focus.part) && (c.vi === v || a.ci === hoverCommit);
      a.line.visible = shown;
      if (shown) {
        a.line.material.opacity = a.ci === hoverCommit ? 1 : a.ci === done && playing ? 0.95 : 0.38;
      }
    }
    // sparks out along the newest commit's light, the first second after it came
    const sp = sparkGeo.attributes.position.array;
    const sc = sparkGeo.attributes.color.array;
    sp.fill(0);
    sc.fill(0);
    if (done >= 0 && playing) {
      const c = commits[done];
      const run = clamp((now - c.at) / 0.9, 0, 1);
      if (run < 1) {
        c.touch.slice(0, SPARKS).forEach((t, k) => {
          if (focus.part >= 0 && t.file.part !== focus.part) {
            return;
          }
          const p = t.curve.getPoint(run);
          sp[k * 3] = p.x;
          sp[k * 3 + 1] = p.y;
          sp[k * 3 + 2] = p.z;
          const cc = col(t.op === 'D' ? GONE : PARTS[t.file.part].colour, 1 - run * 0.6);
          sc[k * 3] = cc.r;
          sc[k * 3 + 1] = cc.g;
          sc[k * 3 + 2] = cc.b;
        });
      }
    }
    sparkGeo.attributes.position.needsUpdate = true;
    sparkGeo.attributes.color.needsUpdate = true;
    for (const f of floors) {
      const shown = f.ci <= done;
      const fl = f.flash !== undefined ? Math.exp(-(now - f.flash) * 1.5) : 0;
      const lit = f.vi === v;
      f.disc.material.opacity = shown ? (lit ? 0.05 : 0.01) + 0.12 * fl : 0;
      f.rim.material.opacity = shown ? (lit ? 0.9 : 0.28) + 0.5 * fl : 0;
      f.label.style.opacity = shown ? String(lit ? 1 : 0.7) : '0';
      f.label.classList.toggle('now', shown && lit);
    }
    nowRim.position.y = yNow;
    beam.position.y = yNow + 0.05;
    beam.rotation.y -= dt * 1.05;
    // the camera: the whole tower so far in view, turning slowly while it plays, and as the person moved it
    const height = Math.max(yNow, 30);
    want.y = height * 0.44 + view.lift;
    want.dist = Math.max(R * 4.3, height * 2.3 + R * 2.4) * (W < 520 ? 1.2 : 1) * view.zoom;
    const k = 1 - Math.exp(-dt * (dragging ? 8 : 2.2));
    cam.y += (want.y - cam.y) * k;
    cam.dist += (want.dist - cam.dist) * k;
    if ((playing || turning > 0) && !still && performance.now() - cam.userAt > 6000) {
      cam.az += TURN * dt;
    }
    turning = Math.max(0, turning - dt);
    camera.position.set(Math.cos(cam.az) * Math.cos(cam.el) * cam.dist, cam.y + Math.sin(cam.el) * cam.dist, Math.sin(cam.az) * Math.cos(cam.el) * cam.dist);
    camera.lookAt(0, cam.y, 0);
    when.textContent = `${done + 1} of ${commits.length} commits`;
  }

  /* the words over the picture: each where its point of the scene is on the screen */
  const toScreen = (p) => {
    v3.copy(p).project(camera);
    return { x: (v3.x * 0.5 + 0.5) * W, y: (-v3.y * 0.5 + 0.5) * H, behind: v3.z > 1 };
  };
  function place() {
    for (const l of partLabels) {
      if (!l) {
        continue;
      }
      const s = toScreen(l.p);
      l.d.style.transform = `translate(${s.x.toFixed(1)}px, ${s.y.toFixed(1)}px) translate(-50%, -50%)`;
      l.d.style.visibility = s.behind || s.x < 0 || s.x > W || s.y < 0 || s.y > H ? 'hidden' : '';
    }
    const side = cam.az + Math.PI / 2;                 // the floors' numbers stand at the tower's left edge
    let lastY = Infinity;
    for (const f of [...floors].reverse()) {           // from the top down, a number where it has room
      const p = new THREE.Vector3(Math.cos(side) * (R + 8), commits[f.ci].y, Math.sin(side) * (R + 8));
      const s = toScreen(p);
      f.label.style.transform = `translate(${s.x.toFixed(1)}px, ${s.y.toFixed(1)}px) translate(-100%, -50%)`;
      const room = (Math.abs(s.y - lastY) > 15 || f.vi === litV()) && s.y > 0 && s.y < H && !s.behind;
      f.label.style.visibility = room && f.ci <= done ? '' : 'hidden';
      if (room && f.ci <= done) {
        lastY = s.y;
      }
    }
  }

  function render() {
    const pw = rtScene.width;
    const ph = rtScene.height;
    renderer.setRenderTarget(rtScene);
    renderer.render(scene, camera);
    bright.uniforms.tex.value = rtScene.texture;
    pass(bright, rtA);
    blurInto(rtA, rtB, pw >> 1, ph >> 1, 1.2);
    bright.uniforms.tex.value = rtA.texture;
    pass(bright, rtC);
    blurInto(rtC, rtD, pw >> 2, ph >> 2, 1.6);
    blurInto(rtC, rtD, pw >> 2, ph >> 2, 2.4);
    mix.uniforms.tex.value = rtScene.texture;
    mix.uniforms.glow.value = rtA.texture;
    mix.uniforms.glow2.value = rtC.texture;
    pass(mix, null);
    place();
  }

  /* ---- the loop: only while seen, and while something moves ---- */
  let active = false;
  let raf = 0;
  let last = 0;
  let dirty = true;
  function frame(ms) {
    const dt = Math.min(0.05, (ms - last) / 1000 || 0.016);
    last = ms;
    update(dt);
    render();
    dirty = false;
    const moving = playing || turning > 0 || Math.abs(yNow - (done < 0 ? 0 : commits[done].y)) > 0.01 ||
      Math.abs(want.dist - cam.dist) > 0.5 || Math.abs(want.y - cam.y) > 0.2 || dragging;
    raf = active && (moving || dirty) ? requestAnimationFrame(frame) : 0;
  }
  function wake() {
    dirty = true;
    if (active && !raf) {
      last = performance.now();
      raf = requestAnimationFrame(frame);
    }
  }
  function seek(k) {                                   // to commit k at once: those before it come quietly
    k = clamp(k, 0, commits.length);
    done = -1;
    verShown = -2;
    focus.v = -1;
    for (let i = 0; i < k; i++) {
      arrive(i, true);
    }
    if (k === 0) {
      caption.innerHTML = '';
      range.value = '0';
    } else {
      say(commits[k - 1]);
      if (verShown >= 0) {
        hooks.onVersion(verShown);
      }
    }
    now = k ? commits[k - 1].at + 0.001 : 0;
    yNow = done < 0 ? 0 : commits[done].y;
  }
  function play(fromStart) {
    if (fromStart || now >= END) {
      seek(0);
    }
    focus.v = -1;
    view.lift = 0;
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
    wake();
  });

  /* turning it by hand. a drag turns and tilts it; shift or the right button takes the eye up and down the tower;
   * the wheel comes closer, in the full screen or with ctrl (on the page the wheel scrolls the page); two fingers
   * pinch closer and move up and down; one finger on the page only turns, the page scrolls under an up and down
   * drag. a double click brings back the view it had, all of it lit */
  let dragging = false;
  let dragFrom = null;
  let pinch = null;
  const fingers = new Map();
  canvas.addEventListener('contextmenu', (ev) => ev.preventDefault());
  canvas.addEventListener('pointerdown', (ev) => {
    fingers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
    canvas.setPointerCapture(ev.pointerId);
    if (fingers.size === 2) {
      const [a, b] = [...fingers.values()];
      pinch = { d: Math.hypot(a.x - b.x, a.y - b.y) || 1, my: (a.y + b.y) / 2, zoom: view.zoom, lift: view.lift };
      dragFrom = null;
    } else if (fingers.size === 1) {
      dragFrom = { x: ev.clientX, y: ev.clientY, az: cam.az, el: cam.el, lift: view.lift,
        up: ev.shiftKey || ev.button === 2, touch: ev.pointerType === 'touch' };
    }
    dragging = true;
    tip.hidden = true;
    wake();
  });
  const lift = () => cam.dist * 0.0016;
  canvas.addEventListener('pointermove', (ev) => {
    if (fingers.has(ev.pointerId)) {
      fingers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
    }
    if (pinch && fingers.size === 2) {
      const [a, b] = [...fingers.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y) || 1;
      view.zoom = clamp((pinch.zoom * pinch.d) / d, 0.12, 2.5);
      view.lift = pinch.lift + ((a.y + b.y) / 2 - pinch.my) * lift();
      cam.userAt = performance.now();
      wake();
      return;
    }
    if (dragging && dragFrom) {
      if (dragFrom.up) {
        view.lift = dragFrom.lift + (ev.clientY - dragFrom.y) * lift();
      } else {
        cam.az = dragFrom.az + (ev.clientX - dragFrom.x) * 0.008;
        if (!dragFrom.touch || full()) {
          cam.el = clamp(dragFrom.el + (ev.clientY - dragFrom.y) * 0.006, -0.2, 1.4);
        }
      }
      cam.userAt = performance.now();
      wake();
      return;
    }
    pick(ev);
  });
  const lifted = (ev) => {
    fingers.delete(ev.pointerId);
    if (fingers.size < 2) {
      pinch = null;
    }
    if (!fingers.size) {
      dragging = false;
      dragFrom = null;
      told();
    }
  };
  canvas.addEventListener('pointerup', lifted);
  canvas.addEventListener('pointercancel', lifted);
  canvas.addEventListener('wheel', (ev) => {
    if (!full() && !ev.ctrlKey) {
      return;                                          // the page's own scroll
    }
    ev.preventDefault();
    view.zoom = clamp(view.zoom * Math.exp(ev.deltaY * 0.0015), 0.12, 2.5);
    cam.userAt = performance.now();
    told();
    wake();
  }, { passive: false });
  canvas.addEventListener('dblclick', showAll);
  let hoverCommit = -1;
  canvas.addEventListener('pointerleave', () => {
    tip.hidden = true;
    if (hoverCommit !== -1) {
      hoverCommit = -1;
      wake();
    }
  });
  /* the pointer on a bead: the nearest one on the screen within a few pixels, a lit file's or any commit's */
  function pick(ev) {
    const r = canvas.getBoundingClientRect();
    const mx = ev.clientX - r.left;
    const my = ev.clientY - r.top;
    let best = null;
    let bestD = 10 * 10;
    const look = (p, item) => {
      const s = toScreen(p);
      const d2 = (s.x - mx) ** 2 + (s.y - my) ** 2;
      if (!s.behind && d2 < bestD) {
        bestD = d2;
        best = item;
      }
    };
    const v = litV();
    const p = new THREE.Vector3();
    for (const t of touches) {
      if (t.ci <= done && commits[t.ci].vi === v && (focus.part < 0 || t.file.part === focus.part)) {
        look(p.set(t.file.x, commits[t.ci].y, t.file.z), { t });
      }
    }
    commits.forEach((c, i) => {
      if (i <= done) {
        look(p.set(0, c.y, 0), { c, i });
      }
    });
    let hc = -1;
    if (best) {
      const c = best.t ? commits[best.t.ci] : best.c;
      hc = best.t ? best.t.ci : best.i;
      const date = new Date(c.t * 1000).toLocaleString('en-GB', { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' });
      const head = best.t
        ? `<b>${esc(best.t.file.path)}</b> ${best.t.op === 'D' ? 'taken out' : best.t.op === 'A' ? 'new' : 'changed'}` +
          `${best.t.add || best.t.del ? `, +${best.t.add} &minus;${best.t.del}` : ''}<br>`
        : `<b>${c.touch.length} ${c.touch.length === 1 ? 'file' : 'files'}</b><br>`;
      const words = c.subject.length > 110 ? `${c.subject.slice(0, 107)}...` : c.subject;
      tip.innerHTML = `${head}${esc(words)}<br><small>${esc(date)}, ${esc(c.who)}</small>`;
      tip.style.left = `${clamp(mx + 14, 0, W - 260)}px`;
      tip.style.top = `${clamp(my + 14, 0, H - 70)}px`;
      tip.hidden = false;
    } else {
      tip.hidden = true;
    }
    if (hc !== hoverCommit) {
      hoverCommit = hc;
      wake();
    }
  }

  let lastSize = '';
  const refit = () => {
    const size = `${box.clientWidth}x${window.innerHeight}:${full()}`;
    if (box.clientWidth && size !== lastSize) {
      lastSize = size;
      fit();
      wake();
    }
  };
  new ResizeObserver(refit).observe(box);
  window.addEventListener('resize', refit);
  fit();
  seek(commits.length);
  update(0);

  let started = false;
  return {
    setActive(on) {
      active = on;
      if (!on) {
        if (raf) {
          cancelAnimationFrame(raf);
          raf = 0;
        }
        return;
      }
      refit();
      if (!started) {
        started = true;
        if (still) {
          seek(commits.length);
          wake();
          return;
        }
        play(true);
        return;
      }
      wake();
    },
    parts: PARTS.map((p, i) => ({ name: p.name, colour: p.colour, here: count[i] > 0 })),
    setPart,
    showAll,
    /* the menu's version buttons: while it plays, from that version on; stopped, that version lit, and the one lit
     * picked again lets it go, back to the newest. gives the version the menu should show */
    seekVersion(vi) {
      if (!playing && started) {
        if (focus.v === vi) {
          focus.v = -1;
          view.lift = 0;
          view.zoom = 1;
          told();
          wake();
          return verShown;
        }
        pickVersion(vi);
        return vi;
      }
      const k = commits.findIndex((c) => c.vi === vi);
      if (k < 0) {
        return;
      }
      started = true;
      seek(k);
      playing = !still;
      playBtn.textContent = playing ? 'PAUSE' : 'PLAY';
      wake();
      return vi;
    },
  };
}
