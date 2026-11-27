// The chain from a plane to the board, in isometric wire. A mockup: one canvas, no libraries.
// What stands still is drawn once into `back`, thin and dim. What moves is bright.
// Radio is amber, data on the ground is cyan, WiFi is green. The rings in `world` are not drawn:
// they only time when an antenna hears a plane and when the board gets its planes.

const TAU = Math.PI * 2;
const C30 = Math.cos(Math.PI / 6);
const WIRE = '92,225,255';
const RADIO = '255,184,72';
const WIFI = '125,255,168';
const HIGH = '234,247,255';
const NIGHT = '5,14,34';
const MONO = '"Cascadia Mono", Consolas, monospace';
const rgba = (c, a) => `rgba(${c},${a})`;
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const smooth = (v) => {
  const t = clamp(v, 0, 1);
  return t * t * (3 - 2 * t);
};

const xy = (p) => [p.x, p.y];

/* a line with its corners rounded off: each corner becomes a short curve of small steps */
function rounded(points, radius) {
  const out = [{ x: points[0].x, y: points[0].y }];
  for (let i = 1; i < points.length - 1; i++) {
    const p = points[i - 1];
    const q = points[i];
    const r = points[i + 1];
    const a = Math.hypot(q.x - p.x, q.y - p.y) || 1;
    const b = Math.hypot(r.x - q.x, r.y - q.y) || 1;
    const k = Math.min(radius, a / 2, b / 2);
    const from = { x: q.x + ((p.x - q.x) * k) / a, y: q.y + ((p.y - q.y) * k) / a };
    const to = { x: q.x + ((r.x - q.x) * k) / b, y: q.y + ((r.y - q.y) * k) / b };
    for (let t = 0; t <= 12; t++) {
      const u = t / 12;
      const v = 1 - u;
      out.push({ x: v * v * from.x + 2 * v * u * q.x + u * u * to.x, y: v * v * from.y + 2 * v * u * q.y + u * u * to.y });
    }
  }
  const last = points[points.length - 1];
  out.push({ x: last.x, y: last.y });
  return out;
}

const PLANE_TOP = [[25, 0], [19, 2.6], [6, 3.2], [-3, 23], [-8, 23], [-4, 3.2], [-17, 2.4], [-23, 9.5], [-27, 9.5], [-24, 0]];

export function startScene(canvas, getRadar, onPick) {
  const pen = canvas.getContext('2d');
  const back = document.createElement('canvas');
  const bp = back.getContext('2d');
  const calm = matchMedia('(prefers-reduced-motion: reduce)').matches;

  let W = 0;
  let H = 0;
  let dpr = 1;
  let s = 1;
  let cx = 0;
  let cy = 0;
  let spread = 1;
  let small = false;
  let world = null;
  let clock = 0;
  let spots = [];
  let hover = null;
  let picked = null;
  let marked = null;     // what the page has in hand: the board of the window on top
  let relief = null;     // the real ground, when it has come
  let sky = null;        // the last real planes

  const iso = (x, y, z = 0) => [cx + (x - y) * C30 * s, cy + (x + y) * 0.5 * s - z * s];
  const place = (a, b) => ({ x: (a * spread + b) / 2, y: (b - a * spread) / 2 });

  function height(x, y) {
    if (relief) {
      return relief(x, y);
    }
    let h = 3 * Math.sin(x * 0.021 + 1) * Math.cos(y * 0.017);
    for (const m of world.hills) {
      const d = ((x - m.x) ** 2 + (y - m.y) ** 2) / (m.r * m.r);
      h += m.h * Math.exp(-d) * (1 + 0.16 * Math.sin(x * 0.05) * Math.cos(y * 0.043));
    }
    return h;
  }

  /* a cable lies on the ground, but not on every bump: the ground's height, averaged along it */
  function settle(path) {
    const raw = path.map((p) => height(p.x, p.y));
    path.forEach((p, i) => {
      let sum = 0;
      let n = 0;
      for (let k = Math.max(0, i - 16); k <= Math.min(path.length - 1, i + 16); k++) {
        sum += raw[k];
        n++;
      }
      p.z = sum / n;
    });
  }

  function poly(p, points, stroke, fill, width = 1) {
    p.beginPath();
    points.forEach(([x, y], i) => {
      if (i === 0) {
        p.moveTo(x, y);
      } else {
        p.lineTo(x, y);
      }
    });
    p.closePath();
    if (fill) {
      p.fillStyle = fill;
      p.fill();
    }
    if (stroke) {
      p.strokeStyle = stroke;
      p.lineWidth = width;
      p.stroke();
    }
  }

  function line(p, from, to, stroke, width = 1) {
    p.beginPath();
    p.moveTo(from[0], from[1]);
    p.lineTo(to[0], to[1]);
    p.strokeStyle = stroke;
    p.lineWidth = width;
    p.stroke();
  }

  function box(p, x, y, z, w, d, h, stroke, fill) {
    const t = z + h;
    poly(p, [iso(x, y, t), iso(x + w, y, t), iso(x + w, y + d, t), iso(x, y + d, t)], stroke, fill);
    poly(p, [iso(x + w, y, t), iso(x + w, y + d, t), iso(x + w, y + d, z), iso(x + w, y, z)], stroke, fill);
    poly(p, [iso(x, y + d, t), iso(x + w, y + d, t), iso(x + w, y + d, z), iso(x, y + d, z)], stroke, fill);
  }

  /* windows on a face: o its corner, u along it, v up it. some of them are lit */
  function panes(p, o, u, v, cols, rows, bright) {
    const at = (a, b) => iso(o[0] + u[0] * a + v[0] * b, o[1] + u[1] * a + v[1] * b, o[2] + u[2] * a + v[2] * b);
    for (let r = 0; r < rows; r++) {
      for (let c = 0; c < cols; c++) {
        const a0 = (c + 0.14) / cols;
        const a1 = (c + 0.86) / cols;
        const b0 = (r + 0.2) / rows;
        const b1 = (r + 0.8) / rows;
        const lit = (c * 7 + r * 13 + cols) % 5 < 2;
        poly(p, [at(a0, b0), at(a1, b0), at(a1, b1), at(a0, b1)], rgba(WIRE, bright * 0.55), lit ? rgba(WIRE, bright * 0.5) : null);
      }
    }
  }

  function roofEdge(p, x, y, z, w, d, bright) {
    const k = 3;
    poly(p, [iso(x + k, y + k, z), iso(x + w - k, y + k, z), iso(x + w - k, y + d - k, z), iso(x + k, y + d - k, z)], rgba(WIRE, bright * 0.5), null);
  }

  /* a flat-roofed house: a glass ground floor, a smaller floor over it, a terrace beside that */
  function villa(p, x, y, z, w, d, h, bright) {
    const edge = rgba(WIRE, bright);
    const fill = rgba(NIGHT, 0.95);
    const low = h * 0.56;
    const up = h - low;
    box(p, x, y, z, w, d, low, edge, fill);
    panes(p, [x + w * 0.05, y + d, z + low * 0.1], [w * 0.9, 0, 0], [0, 0, low * 0.8], 6, 1, bright);
    panes(p, [x + w, y + d * 0.92, z + low * 0.2], [0, -d * 0.84, 0], [0, 0, low * 0.6], 3, 1, bright);
    roofEdge(p, x, y, z + low, w, d, bright);
    const ux = x + w * 0.05;
    const uy = y + d * 0.06;
    const uw = w * 0.6;
    const ud = d * 0.66;
    box(p, ux, uy, z + low, uw, ud, up, edge, fill);
    panes(p, [ux + uw * 0.06, uy + ud, z + low + up * 0.14], [uw * 0.88, 0, 0], [0, 0, up * 0.72], 4, 1, bright);
    panes(p, [ux + uw, uy + ud * 0.9, z + low + up * 0.2], [0, -ud * 0.8, 0], [0, 0, up * 0.6], 2, 1, bright);
    roofEdge(p, ux, uy, z + h, uw, ud, bright);
  }

  /* an apartment block, or an office with wide windows */
  function block(p, x, y, z, w, d, h, bright, wide) {
    box(p, x, y, z, w, d, h, rgba(WIRE, bright), rgba(NIGHT, 0.95));
    const rows = Math.max(2, Math.round(h / (wide ? 13 : 11)));
    panes(p, [x + w * 0.05, y + d, z + h * 0.05], [w * 0.9, 0, 0], [0, 0, h * 0.9], wide ? 3 : 5, rows, bright);
    panes(p, [x + w, y + d * 0.92, z + h * 0.05], [0, -d * 0.84, 0], [0, 0, h * 0.9], wide ? 2 : 4, rows, bright);
    roofEdge(p, x, y, z + h, w, d, bright);
  }

  function building(p, b, bright) {
    if (b.kind === 'villa') {
      villa(p, b.x, b.y, b.z, b.w, b.d, b.h, bright);
    } else {
      block(p, b.x, b.y, b.z, b.w, b.d, b.h, bright, b.kind === 'office');
    }
  }

  /* a modern ADS-B antenna: one thin rod with a small box at its foot */
  function antenna(p, r) {
    const base = r.z + r.h;
    const top = r.z + r.mast;
    box(p, r.ax - 4, r.ay - 3, base, 8, 6, 5, rgba(WIRE, 0.7), rgba(NIGHT, 1));
    line(p, iso(r.ax, r.ay, base + 5), iso(r.ax, r.ay, top), rgba(HIGH, 0.9), 1.5);
    for (const k of [0.42, 0.7]) {
      const z = base + 5 + (top - base - 5) * k;
      line(p, iso(r.ax, r.ay, z), iso(r.ax, r.ay, z + 4), rgba(HIGH, 0.9), 3);
    }
  }

  /* --- what is where: built again when the page changes size --- */

  function build() {
    const at = (a, b) => place(a, b);
    world = { hills: [] };
    world.hills = [
      { ...at(-600, -360), h: 104, r: 170 }, { ...at(-210, -470), h: 128, r: 210 }, { ...at(250, -500), h: 96, r: 180 },
      { ...at(690, -340), h: 118, r: 180 }, { ...at(-920, -60), h: 74, r: 150 }, { ...at(1010, 40), h: 60, r: 150 },
    ];
    const stand = (a, b) => {
      const p = at(a, b);
      return { ...p, z: height(p.x, p.y) };
    };

    const roofed = (b, ax, ay) => ({ ...b, ax: b.x + b.w * ax, ay: b.y + b.d * ay, mast: b.h + 42, glow: 0, sent: 0, heard: 0 });
    world.receivers = [
      roofed({ ...stand(-640, 120), kind: 'block', w: 44, d: 38, h: 66 }, 0.5, 0.45),
      roofed({ ...stand(-430, -90), kind: 'villa', w: 58, d: 46, h: 38 }, 0.35, 0.4),
      roofed({ ...stand(-330, 205), kind: 'office', w: 58, d: 40, h: 46 }, 0.5, 0.45),
    ];
    world.server = stand(-20, -40);
    world.router = stand(330, 150);
    world.home = { ...stand(230, 40), kind: 'villa', w: 94, d: 72, h: 62 };
    world.board = { ...stand(small ? 395 : 505, -20), L: small ? 165 : 258 };
    world.others = [
      { ...stand(95, 255), kind: 'villa', w: 52, d: 40, h: 32, glow: 0 },
      { ...stand(-135, 290), kind: 'block', w: 38, d: 34, h: 58, glow: 0 },
    ];

    const v = world.server;
    world.portIn = { x: v.x - 24, y: v.y + 44 };
    world.portOut = { x: v.x + 82, y: v.y + 37 };
    // a cable bends softly: an S on its way, and it reaches a port straight on
    const trace = (from, to, along, across) => {
      const mx = from.x + (to.x - from.x) * along;
      const my = from.y + (to.y - from.y) * across;
      return rounded([from, { x: mx, y: from.y }, { x: mx, y: my }, { x: to.x, y: my }, to], 58);
    };
    const bends = [[0.62, 0.5], [0.4, 0.62], [0.5, 0.72]];
    world.up = world.receivers.map((r, i) => trace({ x: r.x + r.w / 2, y: r.y + r.d }, world.portIn, ...bends[i]));
    world.down = trace(world.portOut, { x: world.router.x + 22, y: world.router.y + 34 }, 0.5, 0.5);
    world.side = world.others.map((o, i) => trace(world.portOut, { x: o.x + o.w / 2, y: o.y }, [0.55, 0.6][i], [0.5, 0.45][i]));

    for (const t of [...world.up, world.down, ...world.side]) {
      settle(t);
    }

    world.city = [];
    let seed = 41;
    const next = () => {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      return seed / 0x7fffffff;
    };
    for (let i = 0; i < 26; i++) {
      const a = -200 + next() * 520;
      const b = -190 + next() * 150;
      const p = at(a, b);
      const clear = Math.hypot(p.x - v.x, p.y - v.y) > 100;
      if (clear) {
        world.city.push({ x: p.x, y: p.y, w: 16 + next() * 22, d: 16 + next() * 22, h: 16 + next() * 54 });
      }
    }
    world.city.sort((p, q) => (p.x + p.y) - (q.x + q.y));

    const route = (a0, b0, a1, b1, z, speed, at0, facts) => {
      const from = at(a0, b0);
      const to = at(a1, b1);
      return { ...facts, id: facts.name, from, to, z, speed, p: at0, heading: Math.atan2(to.y - from.y, to.x - from.x), length: Math.hypot(to.x - from.x, to.y - from.y), ring: 0.5 + at0 };
    };
    world.planes = [
      route(-980, -130, 980, -130, 172, 92, 0.34, { name: 'OWL204', level: 'FL340', type: 'Airbus A320', knots: 452, feet: '34,000 ft', course: '090', squawk: '2301' }),
      route(-860, -560, 340, 640, 132, 76, 0.42, { name: 'OWL518', level: 'FL210', type: 'Boeing 737', knots: 388, feet: '21,000 ft', course: '135', squawk: '4417' }),
      route(-330, 620, 960, -670, 188, 84, 0.2, { name: 'OWL77', level: 'FL380', type: 'Airbus A350', knots: 486, feet: '38,000 ft', course: '045', squawk: '6132' }),
      route(980, 70, -980, 70, 148, 70, 0.72, { name: 'OWL310', level: 'FL120', type: 'ATR 72', knots: 266, feet: '12,000 ft', course: '270', squawk: '3055' }),
    ];
    world.heli = { id: 'heli', rotor: true, c: at(-170, 110), r: 86, z: 62, angle: 1.2, ring: 0.9, name: 'OWL H7', type: 'Airbus H135', knots: 112, feet: '1,800 ft', course: 'circling', squawk: '0020' };
    world.rings = [];
    world.packets = [];
    world.beams = [];
    world.wifi = [];
    world.wifiNext = 0;
    world.link = -1;
    world.sweep = 0;
    world.serverGlow = 0;
    world.routerGlow = 0;
    world.boardGlow = 0;
    world.nextDown = 0;
    world.count = { heard: 0, into: 0, out: 0 };
  }

  function paintBack() {
    back.width = Math.round(W * dpr);
    back.height = Math.round(H * dpr);
    bp.setTransform(dpr, 0, 0, dpr, 0, 0);
    bp.clearRect(0, 0, W, H);

    const levels = [0.05, 0.085, 0.125, 0.17, 0.23, 0.3];
    const paths = levels.map(() => new Path2D());
    const step = small ? 46 : relief ? 18 : 36;
    const span = 1500;
    const add = (x0, y0, x1, y1) => {
      const h0 = height(x0, y0);
      const h1 = height(x1, y1);
      const p = iso(x0, y0, h0);
      const q = iso(x1, y1, h1);
      if (Math.max(p[0], q[0]) < -8 || Math.min(p[0], q[0]) > W + 8 || Math.max(p[1], q[1]) < -8 || Math.min(p[1], q[1]) > H + 8) {
        return;
      }
      const far = Math.hypot((p[0] - W / 2) / (W * 0.68), (p[1] - H * 0.52) / (H * 0.86));
      const lift = clamp(h0 / 90, 0, 1);
      const level = clamp(Math.round((1 - far) * 3.4 + lift * 3), 0, levels.length - 1);
      paths[level].moveTo(p[0], p[1]);
      paths[level].lineTo(q[0], q[1]);
    };
    for (let u = -span; u <= span; u += step) {
      for (let v = -span; v < span; v += step) {
        add(u, v, u, v + step);
        add(v, u, v + step, u);
      }
    }
    bp.lineWidth = 1;
    paths.forEach((path, i) => {
      bp.strokeStyle = rgba(WIRE, levels[i]);
      bp.stroke(path);
    });

    for (const c of world.city) {
      block(bp, c.x, c.y, height(c.x, c.y), c.w, c.d, c.h, 0.24, c.h < 34);
    }

    const traces = [...world.up, world.down, ...world.side];
    bp.lineJoin = 'round';
    bp.lineCap = 'round';
    for (const [width, alpha] of [[5 * s, 0.1], [1.3, 0.55]]) {
      for (const t of traces) {
        bp.beginPath();
        t.forEach((p, i) => {
          const q = iso(p.x, p.y, p.z + 1);
          if (i === 0) {
            bp.moveTo(q[0], q[1]);
          } else {
            bp.lineTo(q[0], q[1]);
          }
        });
        bp.strokeStyle = rgba(WIRE, alpha);
        bp.lineWidth = width;
        bp.stroke();
      }
    }
    bp.lineCap = 'butt';
    for (const t of traces) {
      const e = iso(t[0].x, t[0].y, t[0].z + 1);
      bp.fillStyle = rgba(WIRE, 0.85);
      bp.beginPath();
      bp.arc(e[0], e[1], 2.2, 0, TAU);
      bp.fill();
    }

    for (const r of world.receivers) {
      building(bp, r, 0.55);
      antenna(bp, r);
    }

    const v = world.server;
    for (let i = 0; i < 3; i++) {
      const x = v.x + i * 40 - 30;
      box(bp, x, v.y, v.z, 32, 30, 92, rgba(WIRE, 0.6), rgba(NIGHT, 0.95));
      for (let row = 0; row < 8; row++) {
        const z = v.z + 14 + row * 10;
        line(bp, iso(x + 5, v.y + 30, z), iso(x + 27, v.y + 30, z), rgba(WIRE, 0.28));
      }
    }
    // the patch panel: everything goes into the racks through IN and leaves through OUT
    box(bp, v.x - 30, v.y + 30, v.z, 112, 14, 10, rgba(WIRE, 0.85), rgba(NIGHT, 1));
    for (const [px, py] of [[world.portIn.x, v.y + 44], [v.x + 82, v.y + 37]]) {
      const on = py === v.y + 44
        ? [iso(px - 5, py, v.z + 2), iso(px + 5, py, v.z + 2), iso(px + 5, py, v.z + 8), iso(px - 5, py, v.z + 8)]
        : [iso(px, py - 5, v.z + 2), iso(px, py + 5, v.z + 2), iso(px, py + 5, v.z + 8), iso(px, py - 5, v.z + 8)];
      poly(bp, on, rgba(HIGH, 0.95), rgba(WIRE, 0.5));
    }
    if (!small) {
      bp.font = `700 11px ${MONO}`;
      bp.fillStyle = rgba(HIGH, 0.9);
      bp.textAlign = 'right';
      const i0 = iso(world.portIn.x, v.y + 44, 0);
      bp.fillText('IN', i0[0] - 8, i0[1] + 16);
      bp.textAlign = 'left';
      const o0 = iso(v.x + 82, v.y + 37, 0);
      bp.fillText('OUT', o0[0] + 10, o0[1] - 2);
    }

    building(bp, world.home, 0.66);

    const r = world.router;
    box(bp, r.x, r.y, r.z, 44, 34, 16, rgba(WIRE, 0.4), rgba(NIGHT, 0.95));
    box(bp, r.x - 4, r.y - 4, r.z + 16, 54, 40, 9, rgba(WIRE, 0.9), rgba(NIGHT, 0.98));
    for (const [dx, dy] of [[4, 4], [44, 4]]) {
      line(bp, iso(r.x + dx, r.y + dy, r.z + 25), iso(r.x + dx, r.y + dy, r.z + 56), rgba(WIRE, 0.9), 1.6);
    }

    for (const o of world.others) {
      building(bp, o, 0.46);
    }
  }

  /* --- what moves --- */

  function ring(x, y, z, reach, source) {
    world.rings.push({ x, y, z, r: 8, reach, source, heard: new Set() });
  }

  function send(path, then) {
    let length = 0;
    const cuts = [0];
    for (let i = 1; i < path.length; i++) {
      length += Math.hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
      cuts.push(length);
    }
    world.packets.push({ path, cuts, length, at: 0, then });
  }

  function pointOn(k, at) {
    const d = clamp(at, 0, k.length);
    for (let i = 1; i < k.path.length; i++) {
      if (d <= k.cuts[i]) {
        const t = (d - k.cuts[i - 1]) / Math.max(1, k.cuts[i] - k.cuts[i - 1]);
        const p = k.path[i - 1];
        const q = k.path[i];
        return { x: p.x + (q.x - p.x) * t, y: p.y + (q.y - p.y) * t, z: p.z + (q.z - p.z) * t };
      }
    }
    return k.path[k.path.length - 1];
  }

  /* into the racks: the rows light from the first to the last, then the answer goes out */
  function reachServer() {
    world.count.into++;
    world.serverGlow = 1;
    if (world.sweep === 0 && clock >= world.nextDown) {
      world.sweep = 0.001;
    }
  }

  function leaveServer() {
    world.nextDown = clock + 0.9;
    world.count.out++;
    send(world.down, () => {
      world.routerGlow = 1;
      world.wifi.push({ r: 0, data: true }, { r: -46, data: true });
      world.link = 0;
    });
    world.side.forEach((path, i) => {
      if (Math.sin(clock * 3 + i * 2) > -0.2) {
        send(path, () => {
          world.others[i].glow = 1;
        });
      }
    });
  }

  function wifiEnds() {
    const r = world.router;
    const b = boardCorners(world.board);
    return { from: { x: r.x + 24, y: r.y + 4, z: r.z + 56 }, to: { x: b.x + 4, y: b.y, z: b.z + b.h + 34 } };
  }

  function update(dt) {
    clock += dt;
    world.planes = world.planes.filter((p) => !p.gone || p.seen > 0.02);
    for (const p of world.planes) {
      if (p.live) {
        // straight on between two answers. a new answer is pulled to softly, but never backwards:
        // a plane ahead of where it really is goes on at half speed till the real one catches up
        p.tx += p.vx * dt;
        p.ty += p.vy * dt;
        const ex = p.tx - p.x;
        const ey = p.ty - p.y;
        if (ex * p.vx + ey * p.vy > 0) {
          p.x += ex * Math.min(1, dt * 1.2);
          p.y += ey * Math.min(1, dt * 1.2);
        } else {
          p.x += p.vx * dt * 0.5;
          p.y += p.vy * dt * 0.5;
        }
        p.seen += ((p.gone ? 0 : 1) - p.seen) * Math.min(1, dt * 1.5);
      } else {
        p.p = (p.p + (p.speed * dt) / p.length) % 1;
        p.x = p.from.x + (p.to.x - p.from.x) * p.p;
        p.y = p.from.y + (p.to.y - p.from.y) * p.p;
        p.seen = smooth(p.p / 0.06) * smooth((1 - p.p) / 0.06);
      }
      p.ring -= dt;
      if (p.ring <= 0 && p.seen > 0.9) {
        p.ring = p.live ? 7 : 1.9;
        ring(p.x, p.y, p.z, p.live ? 320 : small ? 250 : 350, p);
      }
    }
    const h = world.heli;
    if (h) {
      h.angle += dt * 0.34;
      h.x = h.c.x + Math.cos(h.angle) * h.r;
      h.y = h.c.y + Math.sin(h.angle) * h.r;
      h.seen = 1;
      h.ring -= dt;
      if (h.ring <= 0) {
        h.ring = 2.6;
        ring(h.x, h.y, h.z, 210, h);
      }
    }

    world.rings = world.rings.filter((r) => r.r < r.reach);
    for (const r of world.rings) {
      r.r += dt * 165;
      world.receivers.forEach((v, i) => {
        if (!r.heard.has(i) && Math.hypot(v.ax - r.x, v.ay - r.y) < r.r) {
          r.heard.add(i);
          v.glow = 1;
          v.heard++;
          world.count.heard++;
          if (world.beams.length < 3) {
            world.beams.push({ from: r.source, to: v, life: 1 });
          }
          if (clock - v.sent > 1.3) {
            v.sent = clock;
            send(world.up[i], reachServer);
          }
        }
      });
    }
    world.beams = world.beams.filter((b) => b.life > 0);
    for (const b of world.beams) {
      b.life -= dt * 1.7;
    }
    world.packets = world.packets.filter((k) => {
      k.at += dt * 250;
      if (k.at >= k.length) {
        k.then?.();
        return false;
      }
      return true;
    });
    if (world.sweep > 0) {
      world.sweep += dt * 1.5;
      if (world.sweep >= 1) {
        world.sweep = 0;
        leaveServer();
      }
    }

    const { from, to } = wifiEnds();
    const reach = Math.hypot(to.x - from.x, to.y - from.y);
    world.wifiReach = reach;
    world.wifiNext -= dt;
    if (world.wifiNext <= 0) {
      world.wifiNext = 1.3;
      world.wifi.push({ r: 0, data: false });
    }
    world.wifi = world.wifi.filter((w) => {
      w.r += dt * 150;
      if (w.r >= reach) {
        if (w.data) {
          world.boardGlow = 1;
        }
        return false;
      }
      return true;
    });
    if (world.link >= 0) {
      world.link += dt * 1.1;
      if (world.link >= 1) {
        world.link = -1;
      }
    }
    for (const v of world.receivers) {
      v.glow = Math.max(0, v.glow - dt * 1.4);
    }
    for (const o of world.others) {
      o.glow = Math.max(0, o.glow - dt * 1.2);
    }
    world.serverGlow = Math.max(0, world.serverGlow - dt * 1.5);
    world.routerGlow = Math.max(0, world.routerGlow - dt * 1.3);
    world.boardGlow = Math.max(0, world.boardGlow - dt * 1.5);
  }

  function drawPlane(p) {
    const a = p.seen;
    const g = height(p.x, p.y);
    const under0 = iso(p.x, p.y, g);
    const self = iso(p.x, p.y, p.z);
    pen.setLineDash([3, 5]);
    line(pen, self, under0, rgba(WIRE, 0.3 * a));
    pen.setLineDash([]);
    pen.beginPath();
    pen.ellipse(under0[0], under0[1], 7 * s, 4 * s, 0, 0, TAU);
    pen.strokeStyle = rgba(WIRE, 0.34 * a);
    pen.lineWidth = 1;
    pen.stroke();

    const c = Math.cos(p.heading);
    const n = Math.sin(p.heading);
    const size = small ? 0.8 : 1.16;
    const flat = (u, v, z = 0) => iso(p.x + (u * c - v * n) * size, p.y + (u * n + v * c) * size, p.z + z * size);
    const outline = [...PLANE_TOP, ...PLANE_TOP.slice(1, -1).reverse().map(([u, v]) => [u, -v])];
    poly(pen, outline.map(([u, v]) => flat(u, v)), rgba(HIGH, a), rgba(WIRE, 0.3 * a), 1.5);
    poly(pen, [flat(-15, 0), flat(-25, 0, 11), flat(-27, 0, 11), flat(-24, 0)], rgba(HIGH, a), rgba(WIRE, 0.3 * a), 1.2);
  }

  function drawHeli(h, t) {
    const g = height(h.x, h.y);
    const under0 = iso(h.x, h.y, g);
    const self = iso(h.x, h.y, h.z);
    pen.setLineDash([3, 5]);
    line(pen, self, under0, rgba(WIRE, 0.3));
    pen.setLineDash([]);
    const way = h.live ? h.heading : h.angle + Math.PI / 2;
    const c = Math.cos(way);
    const n = Math.sin(way);
    const size = small ? 0.8 : 1.1;
    const at = (u, v, z = 0) => iso(h.x + (u * c - v * n) * size, h.y + (u * n + v * c) * size, h.z + z * size);
    poly(pen, [at(11, 0, 1), at(7, 4, 3), at(-6, 4, 3), at(-9, 0, 4), at(-6, -4, 3), at(7, -4, 3)], rgba(HIGH, 1), rgba(WIRE, 0.3), 1.4);
    poly(pen, [at(9, 0, 7), at(4, 0, 11), at(-6, 0, 10), at(-9, 0, 4), at(9, 0, 1)], rgba(HIGH, 1), rgba(WIRE, 0.3), 1.2);
    line(pen, at(-8, 0, 7), at(-29, 0, 10), rgba(HIGH, 1), 1.4);
    line(pen, at(-29, -3, 8), at(-29, 3, 14), rgba(HIGH, 1), 1.2);
    line(pen, at(4, 5, -4), at(-6, 5, -4), rgba(HIGH, 0.8));
    line(pen, at(4, -5, -4), at(-6, -5, -4), rgba(HIGH, 0.8));
    const spin = t * 22;
    for (let i = 0; i < 2; i++) {
      const u = Math.cos(spin + i * Math.PI / 2) * 24;
      const v = Math.sin(spin + i * Math.PI / 2) * 24;
      line(pen, at(u, v, 14), at(-u, -v, 14), rgba(HIGH, 0.62));
    }
  }

  function drawRings() {
    for (const b of world.beams) {
      const from = iso(b.from.x, b.from.y, b.from.z);
      const to = iso(b.to.ax, b.to.ay, b.to.z + b.to.mast);
      pen.setLineDash([6, 6]);
      pen.lineDashOffset = -clock * 60;
      line(pen, from, to, rgba(RADIO, 0.62 * b.life), 1.2);
      pen.setLineDash([]);
    }
  }

  function drawPackets() {
    for (const k of world.packets) {
      // the tail lies along the cable, so it turns as the cable turns
      const steps = 10;
      const length = 56;
      const onGround = (q) => iso(q.x, q.y, q.z + 2);
      let from = onGround(pointOn(k, k.at - length));
      for (let i = 1; i <= steps; i++) {
        const to = onGround(pointOn(k, k.at - length + (length * i) / steps));
        line(pen, from, to, rgba(WIRE, 0.9 * (i / steps) ** 1.6), 1.2 + (1.4 * i) / steps);
        from = to;
      }
      const p = from;
      pen.fillStyle = rgba(HIGH, 1);
      pen.beginPath();
      pen.arc(p[0], p[1], 2.4, 0, TAU);
      pen.fill();
    }
  }

  function drawGlows(t) {
    for (const v of world.receivers) {
      if (v.glow > 0) {
        const top = iso(v.ax, v.ay, v.z + v.mast);
        pen.beginPath();
        pen.arc(top[0], top[1], (3 + (1 - v.glow) * 9) * s, 0, TAU);
        pen.strokeStyle = rgba(RADIO, v.glow);
        pen.lineWidth = 1.4;
        pen.stroke();
        pen.fillStyle = rgba(RADIO, v.glow);
        pen.beginPath();
        pen.arc(top[0], top[1], 2.4, 0, TAU);
        pen.fill();
      }
    }
    const v = world.server;
    for (let i = 0; i < 3; i++) {
      const x = v.x + i * 40 - 30;
      for (let row = 0; row < 8; row++) {
        const q = (i * 8 + row) / 23;
        const swept = world.sweep > 0 && Math.abs(q - world.sweep) < 0.16;
        const z = v.z + 14 + row * 10;
        if (swept) {
          line(pen, iso(x + 5, v.y + 30, z), iso(x + 27, v.y + 30, z), rgba(HIGH, 1), 2);
        } else if (Math.sin(t * 5 + row * 1.7 + i * 2.3) > 0.62) {
          const p = iso(x + 7 + ((row * 5 + i * 3) % 14), v.y + 30, z);
          pen.fillStyle = rgba(WIRE, 0.6);
          pen.fillRect(p[0] - 1, p[1] - 1, 3, 2);
        }
      }
    }
    if (world.serverGlow > 0) {
      const p = iso(world.portIn.x, v.y + 44, v.z + 5);
      pen.beginPath();
      pen.arc(p[0], p[1], (5 + (1 - world.serverGlow) * 10) * s, 0, TAU);
      pen.strokeStyle = rgba(WIRE, world.serverGlow);
      pen.lineWidth = 1.5;
      pen.stroke();
    }
    const r = world.router;
    for (let i = 0; i < 3; i++) {
      const on = world.routerGlow > 0.05 || Math.sin(t * 3 + i * 2) > 0.3;
      if (on) {
        const p = iso(r.x + 12 + i * 11, r.y + 36, r.z + 20);
        pen.fillStyle = rgba(WIFI, 0.6 + world.routerGlow * 0.4);
        pen.fillRect(p[0] - 1.5, p[1] - 1, 3, 2);
      }
    }
  }

  function boardCorners(b) {
    const radar = getRadar();
    const h = b.L * (radar ? radar.height / radar.width : 0.6);   // the shape of the screen it shows
    return { x: b.x, y: b.y, z: b.z + 34, L: b.L, h };
  }

  /* one canvas a step of the halving, resized in place. kept by their sizes, a new one came for every few pixels of a
   * window being resized, up to a megabyte each, and none went (the 2026-11-27 review) */
  const smallOnes = [];

  function step(level, from, w, h) {
    let c = smallOnes[level];
    if (!c) {
      c = document.createElement('canvas');
      smallOnes[level] = c;
    }
    if (c.width !== w || c.height !== h) {
      c.width = w;
      c.height = h;
    }
    const p = c.getContext('2d');
    p.imageSmoothingEnabled = true;
    p.imageSmoothingQuality = 'high';
    p.drawImage(from, 0, 0, w, h);
    return c;
  }

  /* by halves: only Chrome filters a big step well, a half at a time comes out the same everywhere */
  function shrunk(radar, w, h) {
    let from = radar;
    let level = 0;
    while (from.width / 2 >= w) {
      from = step(level++, from, Math.round(from.width / 2), Math.round(from.height / 2));
    }
    return step(level, from, w, h);
  }

  function screenOn(x, y, z, L, h) {
    const radar = getRadar();
    const tl = iso(x, y, z + h);
    const tr = iso(x + L, y, z + h);
    const bl = iso(x, y, z);
    if (radar) {
      // made small first, straight on, where the browser filters it well. shrunk and skewed in
      // one go the picture falls apart: lines and letters lose pixels at random
      const rw = Math.max(8, Math.round(Math.hypot(tr[0] - tl[0], tr[1] - tl[1]) * dpr * 1.25));
      const rh = Math.max(8, Math.round(Math.hypot(bl[0] - tl[0], bl[1] - tl[1]) * dpr * 1.25));
      const small0 = shrunk(radar, rw, rh);
      pen.save();
      pen.setTransform(dpr * (tr[0] - tl[0]) / rw, dpr * (tr[1] - tl[1]) / rw, dpr * (bl[0] - tl[0]) / rh, dpr * (bl[1] - tl[1]) / rh, dpr * tl[0], dpr * tl[1]);
      pen.imageSmoothingEnabled = true;
      pen.imageSmoothingQuality = 'high';
      pen.drawImage(small0, 0, 0);
      pen.restore();
    } else {
      poly(pen, [tl, tr, iso(x + L, y, z), bl], null, '#000');
    }
  }

  function drawBoard(t) {
    const b = boardCorners(world.board);
    const m = small ? 7 : 11;
    const pcb = { x: b.x - m, z: b.z - m, L: b.L + m * 3.4, h: b.h + m * 2 };
    const glow = world.boardGlow;

    line(pen, iso(b.x + b.L * 0.2, b.y, b.z - m), iso(b.x + b.L * 0.2, b.y - 26, world.board.z), rgba(WIRE, 0.6));
    line(pen, iso(b.x + b.L * 0.8, b.y, b.z - m), iso(b.x + b.L * 0.8, b.y - 26, world.board.z), rgba(WIRE, 0.6));
    line(pen, iso(b.x + b.L * 0.2, b.y, b.z - m), iso(b.x + b.L * 0.2, b.y + 26, world.board.z), rgba(WIRE, 0.6));
    line(pen, iso(b.x + b.L * 0.8, b.y, b.z - m), iso(b.x + b.L * 0.8, b.y + 26, world.board.z), rgba(WIRE, 0.6));

    poly(pen, [iso(pcb.x, b.y - 7, pcb.z + pcb.h), iso(pcb.x + pcb.L, b.y - 7, pcb.z + pcb.h), iso(pcb.x + pcb.L, b.y, pcb.z + pcb.h), iso(pcb.x, b.y, pcb.z + pcb.h)], rgba(WIRE, 0.9), rgba(NIGHT, 1));
    poly(pen, [iso(pcb.x + pcb.L, b.y - 7, pcb.z + pcb.h), iso(pcb.x + pcb.L, b.y, pcb.z + pcb.h), iso(pcb.x + pcb.L, b.y, pcb.z), iso(pcb.x + pcb.L, b.y - 7, pcb.z)], rgba(WIRE, 0.9), rgba(NIGHT, 1));
    poly(pen, [iso(pcb.x, b.y, pcb.z + pcb.h), iso(pcb.x + pcb.L, b.y, pcb.z + pcb.h), iso(pcb.x + pcb.L, b.y, pcb.z), iso(pcb.x, b.y, pcb.z)], rgba(HIGH, 0.86 + glow * 0.14), 'rgba(9,24,52,1)', 1.4 + glow * 1.4);

    const edge = b.x + b.L + m * 0.5;
    for (let i = 0; i < 5; i++) {
      const z = b.z + b.h * (0.16 + i * 0.17);
      line(pen, iso(edge, b.y, z), iso(edge + m * 1.5, b.y, z), rgba(WIRE, 0.5));
    }
    screenOn(b.x, b.y, b.z, b.L, b.h);
    poly(pen, [iso(b.x, b.y, b.z + b.h), iso(b.x + b.L, b.y, b.z + b.h), iso(b.x + b.L, b.y, b.z), iso(b.x, b.y, b.z)], rgba(WIFI, 0.25 + glow * 0.75), null, 1 + glow);
    const led = iso(edge + m * 0.7, b.y, b.z + b.h + m * 0.2);
    pen.fillStyle = rgba(WIFI, 0.45 + 0.55 * Math.max(glow, 0.5 + 0.5 * Math.sin(t * 2)));
    pen.beginPath();
    pen.arc(led[0], led[1], 2.2, 0, TAU);
    pen.fill();
    line(pen, iso(b.x + 4, b.y, pcb.z + pcb.h), iso(b.x + 4, b.y, b.z + b.h + 34), rgba(WIFI, 0.8 + glow * 0.2), 1.6);
  }

  function otherBoard(o) {
    const L = 54;
    return { x: o.x + o.w + 14, y: o.y + o.d * 0.5, z: o.z + 7, L, h: L * 0.6 };
  }

  function drawOthers() {
    world.others.forEach((o) => {
      const b = otherBoard(o);
      line(pen, iso(b.x + 10, b.y, b.z), iso(b.x + 10, b.y, o.z), rgba(WIRE, 0.5));
      line(pen, iso(b.x + 44, b.y, b.z), iso(b.x + 44, b.y, o.z), rgba(WIRE, 0.5));
      poly(pen, [iso(b.x - 3, b.y, b.z + b.h + 3), iso(b.x + b.L + 3, b.y, b.z + b.h + 3), iso(b.x + b.L + 3, b.y, b.z - 3), iso(b.x - 3, b.y, b.z - 3)], rgba(WIRE, 0.7 + o.glow * 0.3), rgba(NIGHT, 1));
      screenOn(b.x, b.y, b.z, b.L, b.h);
    });
  }

  /* WiFi: a dashed line from the router to the board, and a dot along it when planes come */
  function drawWifi() {
    const { from, to } = wifiEnds();
    const a = iso(from.x, from.y, from.z);
    const b = iso(to.x, to.y, to.z);
    pen.setLineDash([4, 6]);
    pen.lineDashOffset = -clock * 26;
    line(pen, a, b, rgba(WIFI, 0.5), 1.2);
    pen.setLineDash([]);
    if (world.link >= 0) {
      const p = [a[0] + (b[0] - a[0]) * world.link, a[1] + (b[1] - a[1]) * world.link];
      pen.fillStyle = rgba(HIGH, 1);
      pen.beginPath();
      pen.arc(p[0], p[1], 2.8, 0, TAU);
      pen.fill();
    }
  }

  /* --- what can be clicked --- */

  function spot(id, points, pad = 6, label = false) {
    const xs = points.map((p) => p[0]);
    const ys = points.map((p) => p[1]);
    const x = Math.min(...xs) - pad;
    const y = Math.min(...ys) - pad;
    spots.push({ id, x, y, w: Math.max(...xs) + pad - x, h: Math.max(...ys) + pad - y, label });
  }

  function solid(b, top) {
    return [iso(b.x, b.y + b.d, b.z), iso(b.x + b.w, b.y + b.d, b.z), iso(b.x + b.w, b.y, b.z), iso(b.x, b.y, b.z + top), iso(b.x + b.w, b.y, b.z + top), iso(b.x, b.y + b.d, b.z + top)];
  }

  function findSpots() {
    world.receivers.forEach((r, i) => {
      spot(`receiver:${i}`, [...solid(r, r.h), iso(r.ax, r.ay, r.z + r.mast)]);
    });
    const v = world.server;
    spot('server', solid({ x: v.x - 30, y: v.y, z: v.z, w: 112, d: 44 }, 92));
    spot('home', solid(world.home, world.home.h));
    const r = world.router;
    spot('router', [...solid({ x: r.x - 4, y: r.y - 4, z: r.z, w: 54, d: 40 }, 25), iso(r.x + 4, r.y + 4, r.z + 56), iso(r.x + 44, r.y + 4, r.z + 56)]);
    world.others.forEach((o, i) => {
      const b = otherBoard(o);
      spot(`other:${i}`, [...solid(o, o.h), iso(b.x + b.L, b.y, b.z), iso(b.x + b.L, b.y, b.z + b.h)]);
    });
    const b = boardCorners(world.board);
    spot('board', [iso(b.x - 11, b.y, b.z + b.h + 11), iso(b.x + b.L + 24, b.y, b.z + b.h + 11), iso(b.x + b.L + 24, b.y, b.z - 11), iso(b.x - 11, b.y, b.z - 11)], 2);
    for (const p of world.planes) {
      if (p.seen > 0.3) {
        const c = iso(p.x, p.y, p.z);
        spot(`plane:${p.id}`, [[c[0] - 26 * s, c[1] - 18 * s], [c[0] + 26 * s, c[1] + 18 * s]], 8);
      }
    }
    if (world.heli) {
      const h = iso(world.heli.x, world.heli.y, world.heli.z);
      spot('heli', [[h[0] - 22 * s, h[1] - 18 * s], [h[0] + 22 * s, h[1] + 14 * s]], 8);
    }
  }

  function brackets(b, colour) {
    const k = Math.min(12, b.w / 3, b.h / 3);
    const corner = (x, y, dx, dy) => {
      pen.beginPath();
      pen.moveTo(x + dx * k, y);
      pen.lineTo(x, y);
      pen.lineTo(x, y + dy * k);
      pen.strokeStyle = colour;
      pen.lineWidth = 1.6;
      pen.stroke();
    };
    corner(b.x, b.y, 1, 1);
    corner(b.x + b.w, b.y, -1, 1);
    corner(b.x, b.y + b.h, 1, -1);
    corner(b.x + b.w, b.y + b.h, -1, -1);
  }

  function drawBrackets() {
    for (const id of new Set([hover, picked, marked])) {
      const b = spots.find((p) => p.id === id && !p.label);
      if (b) {
        brackets(b, id === marked ? rgba(WIFI, 1) : rgba(HIGH, id === picked ? 1 : 0.7));
      }
    }
  }

  function under(x, y) {
    for (let i = spots.length - 1; i >= 0; i--) {
      const p = spots[i];
      if (x >= p.x && x <= p.x + p.w && y >= p.y && y <= p.y + p.h) {
        return p;
      }
    }
    return null;
  }

  function pointer(e) {
    const b = canvas.getBoundingClientRect();
    return [e.clientX - b.left, e.clientY - b.top];
  }

  canvas.addEventListener('pointermove', (e) => {
    const [x, y] = pointer(e);
    const p = under(x, y);
    hover = p ? p.id : null;
    canvas.style.cursor = p ? 'pointer' : 'default';
  });
  canvas.addEventListener('pointerleave', () => {
    hover = null;
  });
  canvas.addEventListener('click', (e) => {
    const [x, y] = pointer(e);
    const p = under(x, y);
    picked = p ? p.id : null;
    onPick(picked, { x, y });
  });

  /* --- the words --- */

  function plate(x, y, n, title, sub) {
    pen.textBaseline = 'alphabetic';
    pen.font = `700 13px ${MONO}`;
    const titleWidth = pen.measureText(title).width;
    pen.font = `400 12px ${MONO}`;
    const subWidth = pen.measureText(sub).width;
    const w = Math.max(titleWidth + 26, subWidth) + 16;
    const h = 42;
    const left = clamp(x, 6, W - w - 6);
    const top = clamp(y, 6, H - h - 6);
    pen.fillStyle = 'rgba(4,11,30,.84)';
    pen.strokeStyle = rgba(WIRE, 0.42);
    pen.lineWidth = 1;
    pen.beginPath();
    pen.roundRect(left, top, w, h, 5);
    pen.fill();
    pen.stroke();
    pen.fillStyle = rgba(WIRE, 1);
    pen.beginPath();
    pen.arc(left + 16, top + 13, 8.5, 0, TAU);
    pen.fill();
    pen.textAlign = 'center';
    pen.font = `700 12px ${MONO}`;
    pen.fillStyle = '#03081a';
    pen.fillText(String(n), left + 16, top + 17);
    pen.textAlign = 'left';
    pen.font = `700 13px ${MONO}`;
    pen.fillStyle = rgba(HIGH, 1);
    pen.fillText(title, left + 31, top + 17);
    pen.font = `400 12px ${MONO}`;
    pen.fillStyle = 'rgba(165,198,226,1)';
    pen.fillText(sub, left + 8, top + 34);
    return { left, top, w, h };
  }

  function badge(anchor, n) {
    pen.fillStyle = rgba(WIRE, 1);
    pen.beginPath();
    pen.arc(anchor[0], anchor[1], 9, 0, TAU);
    pen.fill();
    pen.textAlign = 'center';
    pen.font = `700 12px ${MONO}`;
    pen.fillStyle = '#03081a';
    pen.fillText(String(n), anchor[0], anchor[1] + 4);
    pen.textAlign = 'left';
  }

  function callout(id, anchor, dx, dy, n, title, sub) {
    if (small) {
      badge(anchor, n);
      spot(id, [[anchor[0] - 11, anchor[1] - 11], [anchor[0] + 11, anchor[1] + 11]], 5, true);
      return;
    }
    const b = plate(anchor[0] + dx, anchor[1] + dy, n, title, sub);
    spot(id, [[b.left, b.top], [b.left + b.w, b.top + b.h]], 0, true);
    const ex = clamp(anchor[0], b.left, b.left + b.w);
    const ey = anchor[1] < b.top ? b.top : b.top + b.h;
    line(pen, [ex, ey], anchor, rgba(WIRE, 0.62));
    pen.fillStyle = rgba(HIGH, 1);
    pen.beginPath();
    pen.arc(anchor[0], anchor[1], 2.2, 0, TAU);
    pen.fill();
  }

  function tag(p, text) {
    const self = iso(p.x, p.y, p.z);
    pen.font = `400 11px ${MONO}`;
    pen.textAlign = 'left';
    pen.fillStyle = rgba(HIGH, 0.86 * (p.seen ?? 1));
    pen.fillText(text, self[0] + 20 * s, self[1] - 11 * s);
  }

  /* the plane that wears the first label: one on the screen, the same one for as long as it stays */
  function lead() {
    const on = (p) => {
      const c = iso(p.x, p.y, p.z);
      return !p.gone && c[0] > 30 && c[0] < W * 0.42 && c[1] > 110 && c[1] < H * 0.62;
    };
    if (!world.lead || !world.planes.includes(world.lead) || !on(world.lead)) {
      world.lead = world.planes.find(on) ?? null;
    }
    return world.lead;
  }

  /* real skies are full: only the few nearest the middle say their names, and the one under the mouse */
  function named(first) {
    const away = (p) => {
      const c = iso(p.x, p.y, p.z);
      return Math.hypot(c[0] - W / 2, c[1] - H / 2);
    };
    const list = world.planes.filter((p) => p !== first && p.seen > 0.5).sort((p, q) => away(p) - away(q)).slice(0, 4);
    for (const p of world.planes) {
      const here = hover === `plane:${p.id}` || picked === `plane:${p.id}`;
      if (here && p !== first && !list.includes(p)) {
        list.push(p);
      }
    }
    return list;
  }

  function drawWords() {
    const first = lead();
    if (!small) {
      for (const p of named(first)) {
        tag(p, `${p.name} ${p.level}`);
      }
      if (world.heli) {
        tag(world.heli, world.heli.name);
      }
    }
    if (first && first.seen > 0.3) {
      const self = iso(first.x, first.y, first.z);
      pen.globalAlpha = first.seen;
      callout(`plane:${first.id}`, self, 16 - self[0], 46 - self[1], 1, `${first.name} ${first.level}`, 'says where it is, twice a second: ADS-B, 1090 MHz');
      pen.globalAlpha = 1;
    }
    const v = world.receivers[0];
    callout('receiver:0', iso(v.ax, v.ay, v.z + v.mast), -86, 150, 2, 'A VOLUNTEER\'S ANTENNA', 'people all over the world catch the signal');
    const srv = world.server;
    callout('server', iso(srv.x + 24, srv.y + 14, srv.z + 92), -122, -66, 3, 'THE NETWORK', 'adsb.lol, adsb.fi or Flightradar24');
    const r = world.router;
    callout('router', iso(r.x + 20, r.y + 26, r.z), -64, 44, 4, 'YOUR ROUTER', 'the planes come home over WiFi');
    const b = boardCorners(world.board);
    // with a board on the cable its screen here is the board's own, now: the callout says so
    callout('board', iso(b.x + b.L * 0.5, b.y, b.z + b.h + 12), -22, -74, 5, sky ? 'YOUR BOARD, LIVE' : 'YOUR BOARD', sky ? 'its own screen, right now' : 'draws them on its own screen');
  }

  function draw(t) {
    spots = [];
    findSpots();
    pen.setTransform(dpr, 0, 0, dpr, 0, 0);
    pen.clearRect(0, 0, W, H);
    pen.drawImage(back, 0, 0, W, H);
    drawPackets();
    drawGlows(t);
    drawRings();
    const flying = [...world.planes, ...(world.heli ? [world.heli] : [])].sort((p, q) => (p.x + p.y) - (q.x + q.y));
    for (const p of flying) {
      if (p.rotor) {
        drawHeli(p, t);
      } else if (p.seen > 0.01) {
        drawPlane(p);
      }
    }
    drawOthers();
    drawWifi();
    drawBoard(t);
    drawWords();
    drawBrackets();
  }

  /* the real planes take the demo's place; those no longer answered fade away */
  function applySky() {
    if (!sky) {
      return;
    }
    if (!world.live) {
      world.live = true;
      world.planes = [];
      world.heli = null;
      world.lead = null;
    }
    const now = new Set();
    for (const a of sky) {
      now.add(a.id);
      const p = world.planes.find((q) => q.id === a.id);
      if (p) {
        Object.assign(p, a, { x: p.x, y: p.y, tx: a.x, ty: a.y, gone: false });
      } else {
        world.planes.push({ ...a, live: true, tx: a.x, ty: a.y, seen: 0, ring: Math.random() * 2, gone: false });
      }
    }
    for (const p of world.planes) {
      p.gone = !now.has(p.id);
    }
  }

  function resize() {
    W = canvas.clientWidth;
    H = canvas.clientHeight;
    dpr = Math.min(devicePixelRatio || 1, 1.75);
    canvas.width = Math.round(W * dpr);
    canvas.height = Math.round(H * dpr);
    small = W < 720;
    s = small ? clamp(W / 760, 0.46, 0.7) : clamp(Math.min(W / 1400, H / 480), 0.62, 1.3);
    spread = small ? clamp(W / s / 1500, 0.45, 1) : clamp(W / s / 1420, 0.8, 1.25);
    cx = W / 2;
    cy = H * 0.57;
    build();
    applySky();
    paintBack();
    if (!sky) {                                        // a demo sky starts in full swing; real planes stay where they are
      for (let i = 0; i < 480; i++) {
        update(1 / 30);
      }
    }
  }

  let shown = true;
  let then = performance.now();
  function run(now) {
    const dt = Math.min(0.25, (now - then) / 1000);
    then = now;
    if (shown) {
      update(dt);
      draw(now / 1000);
    }
    requestAnimationFrame(run);
  }

  new ResizeObserver(() => {
    if (canvas.clientWidth !== W || canvas.clientHeight !== H) {
      resize();
      if (calm) {
        draw(0);
      }
    }
  }).observe(canvas);
  new IntersectionObserver(([e]) => {
    shown = e.isIntersecting;
  }).observe(canvas);

  resize();
  if (calm) {
    draw(0);
    setInterval(() => draw(0), 500);
  } else {
    requestAnimationFrame(run);
  }

  return {
    isShown: () => shown,
    clear() {
      picked = null;
    },
    mark(id) {
      marked = id;
    },
    setRelief(fn) {
      relief = fn;
      resize();
    },
    setSky(list) {
      sky = list;
      applySky();
    },
    /* what a panel says: the thing itself and how much has gone by so far */
    about(id) {
      const [kind, n] = id.split(':');
      const thing = { plane: world.planes.find((p) => p.id === n), heli: world.heli, receiver: world.receivers[n], other: world.others[n] }[kind] ?? null;
      return { kind, thing, count: world.count };
    },
  };
}
