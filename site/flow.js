// The page: your boards over USB. Connect, flash, and each board's own screen beside its settings.
// A Screen is the radar's own code built for that display; with a board on the cable it shows that
// board's planes, and a touch on either side reaches the other.

import { startScene } from './scene.js';
import { loadRelief } from './relief.js';
import { reliefAround } from './relief-live.js';
import { Link } from './link.js';
import { detect, flash as writeFirmware } from './flasher.js';
import { Shell } from './console/shell.js';
import { CORE_COMMANDS, CORE_GROUPS } from './console/commands.js';
import { Terminal } from './term.js';
import { Desk } from './win.js';
import { BoardPaths, BOARD_GROUPS, boardCommands } from './board-console.js';
import { startStats, askHelp, helpAnswer, onHelp, reportFlash, reportCable } from './stats.js';

const $ = (id) => document.getElementById(id);
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const fmt = (n) => n.toLocaleString('en-US');

const BOARDS = {
  ili9341: { name: '2.8" CYD', spec: 'ESP32, 320x240, resistive touch', chip: 'esp32', w: 320, h: 240, inch: 2.8, touch: 'resistive', twin: 'Twin1', shot: '02-live.png', yellow: true, screens: 'three' },
  st7796: { name: '3.5" CYD', spec: 'ESP32, 480x320, touch', chip: 'esp32', w: 480, h: 320, inch: 3.5, touch: 'resistive or capacitive', twin: 'Twin2', shot: 'st7796/02-live.png', yellow: true },
  ili9486: { name: '3.5" RPi LCD', spec: 'on OWLOSAirQuality, no touch', chip: 'esp32', w: 480, h: 320, inch: 3.5, touch: 'none', twin: 'Twin2', shot: 'ili9486/03-picked-heavy.png' },
  gc9a01: { name: '1.28" round', spec: 'Elecrow CrowPanel, ESP32-C3', chip: 'c3', w: 240, h: 240, inch: 1.28, touch: 'capacitive', round: true, twin: 'Twin4', shot: 'gc9a01/03-picked-heavy.png' },
  nmcydc5: { name: '2.8" NM-CYD-C5', spec: 'ESP32-C5, 320x240, resistive touch', chip: 'c5', w: 320, h: 240, inch: 2.8, touch: 'resistive', twin: 'Twin1', shot: '02-live.png', screens: 'two' },
  es3c28p: { name: '2.8" LCDWIKI ES3C28P', spec: 'ESP32-S3, 320x240, capacitive touch', chip: 's3', w: 320, h: 240, inch: 2.8, touch: 'capacitive', twin: 'Twin1', shot: '02-live.png', screens: 'three' },
  crowadv28: { name: '2.8" CrowPanel Advance', spec: 'ESP32-S3, 320x240 IPS, capacitive touch', chip: 's3', w: 320, h: 240, inch: 2.8, touch: 'capacitive', twin: 'Twin1', shot: '02-live.png', screens: 'two' },
  crowadv50: { name: '5" CrowPanel Advance', spec: 'ESP32-S3, 800x480 IPS, capacitive touch', chip: 's3', w: 800, h: 480, inch: 5, touch: 'capacitive', twin: 'Twin', shot: 'crowadv50/02-live.png', screens: 'two' },
  jc8048w550: { name: '5" Guition JC8048W550', spec: 'ESP32-S3, 800x480, capacitive touch', chip: 's3', w: 800, h: 480, inch: 5, touch: 'capacitive', twin: 'Twin', shot: 'crowadv50/02-live.png', screens: 'two' },
  tab5: { name: '5" M5Stack Tab5', spec: 'ESP32-P4 and C6, 1280x720 IPS, capacitive touch, experimental', chip: 'p4', w: 1280, h: 720, inch: 5, touch: 'capacitive', twin: 'Twin', shot: 'crowadv50/02-live.png' },
  esp4827s043: { name: '4.3" ESP32-4827S043C', spec: 'ESP32-S3, 480x272, capacitive touch, experimental', chip: 's3', w: 480, h: 272, inch: 4.3, touch: 'capacitive', twin: 'Twin5', shot: 'esp4827s043/02-live.png', screens: 'two' },
  // its BOOT is the screen's clock line: nothing to press while it runs
  crow70: { name: '7" CrowPanel 7.0', spec: 'ESP32-S3, 800x480, capacitive touch, experimental', chip: 's3', w: 800, h: 480, inch: 7, touch: 'capacitive', twin: 'Twin', shot: 'crowadv50/02-live.png', screens: 'two', noboot: true },
};
const CHIP_NAME = { esp32: 'ESP32', s3: 'ESP32-S3', c3: 'ESP32-C3', c5: 'ESP32-C5', p4: 'ESP32-P4' };
const THEMES = ['arctic', 'phosphor', 'amber', 'crimson', 'synth'];
const LOOKS = ['radar', 'map', 'classic'];
const RANGES = [2, 5, 10, 25, 50, 100, 250];
const SWITCHES = { vectors: 'Vectors', clock: 'UTC clock', block: 'Data block', conflict: 'Conflict' };
const KEPT = 'owlos.boards';                           // the boards this browser knows, by MAC. it stays here

let newest = '0';

const older = (a, b) => a.localeCompare(b, 'en', { numeric: true }) < 0;

/* --- a screen: one running radar --- */

class Screen {
  constructor(factory, first = {}) {
    this.factory = factory;
    this.first = first;
    this.raw = document.createElement('canvas');       // the radar's own pixels
    this.k = 1;                                        // screen pixels for one of them
    this.zoom = 1;                                     // what + and - set: 1 is one to one
    this.canvas = document.createElement('canvas');    // what is seen: the same, drawn up with no smoothing
    this.canvas.className = 'screen';
    this.canvas.setAttribute('aria-label', "the radar's screen");
    this.finger = { down: 0, pressed: 0, x: 0, y: 0 };
    this.opts = { vectors: 0, beam_step: 0, clock: 0, block: 0, autopilot: 0, lost: 0, conflict: 0, fl_lo: 0, fl_hi: 0 };
    this.m = null;
    this.drawn = false;
    this.last = 0;
    this.told = null;
    this.home = null;                                  // a board's: lat, lon, place, country
    this.onChange = null;                              // a touch changed something: (settings changed, plane picked)
    this.listen();
  }

  listen() {
    const c = this.canvas;
    const follow = (e) => {
      const box = c.getBoundingClientRect();
      this.finger.x = Math.round((e.clientX - box.left) * this.w / box.width);
      this.finger.y = Math.round((e.clientY - box.top) * this.h / box.height);
    };
    c.addEventListener('pointerdown', (e) => {
      follow(e);
      this.finger.down = 1;
      this.finger.pressed = 1;
      c.setPointerCapture(e.pointerId);
    });
    c.addEventListener('pointermove', (e) => {
      if (this.finger.down) {
        follow(e);
      }
    });
    for (const name of ['pointerup', 'pointercancel']) {
      c.addEventListener(name, () => {
        this.finger.down = 0;
      });
    }
  }

  async start() {
    const m = await window[this.factory]();
    let started = performance.now();
    this.now = () => Math.round(performance.now() - started) + 1;
    if (!m._twin_init(this.now())) {
      return;
    }
    this.m = m;
    this.w = m._twin_width();
    this.h = m._twin_height();
    this.raw.width = this.w;
    this.raw.height = this.h;
    this.rawPen = this.raw.getContext('2d');
    this.canvas.width = this.w;
    this.canvas.height = this.h;
    this.pen = this.canvas.getContext('2d');
    this.apply({ range: 50, ...this.first });
    started -= 60000;                                  // a new sky is empty till its next answer: that is now
  }

  cfg() {
    return JSON.parse(this.m.UTF8ToString(this.m._twin_set_json()));
  }

  apply(change) {
    const m = this.m;
    const c = { ...this.cfg(), ...change };
    const at = this.home ?? { lat: 50.9, lon: 4.48, place: 'DEMO SKY', country: '' };
    const place = m.stringToNewUTF8(at.place || 'HOME');
    const country = m.stringToNewUTF8(at.country || '');
    m._twin_cfg(at.lat, at.lon, place, country, c.range, c.up, c.theme, c.look, c.map, c.ground, c.dist_unit, c.alt_unit, c.labels);
    m._free(place);
    m._free(country);
    this.told = this.cfg();
  }

  setOpts() {
    const o = this.opts;
    this.m._twin_opt(o.vectors, o.beam_step, o.clock, o.block, o.autopilot, o.lost, o.conflict, o.fl_lo, o.fl_hi);
  }

  /* one setting by its console word. false: no such thing */
  set(name, value) {
    if (name === 'theme' && THEMES.includes(value)) {
      this.apply({ theme: THEMES.indexOf(value) });
    } else if (name === 'look' && LOOKS.includes(value)) {
      this.apply({ look: LOOKS.indexOf(value) });
    } else if (name === 'range' && Number(value) >= 2 && Number(value) <= 250) {
      this.apply({ range: Number(value) });
    } else if (name in SWITCHES && ['on', 'off'].includes(value)) {
      this.opts[name] = value === 'on' ? 1 : 0;
      this.setOpts();
    } else {
      return false;
    }
    return true;
  }

  setJson() {
    return this.m.UTF8ToString(this.m._twin_set_json());
  }

  /* what a board says of itself: how this screen shows it from now on */
  fromBoard(c) {
    this.home = { lat: c.lat, lon: c.lon, place: c.place, country: c.country };
    this.m._twin_board(1);
    this.apply({ range: c.range, up: c.up, theme: c.theme, look: c.look, map: c.map, ground: c.ground, dist_unit: c.dist_unit, alt_unit: c.alt_unit, labels: c.labels });
    Object.assign(this.opts, { vectors: c.vectors, beam_step: c.beam_step, clock: c.clock, block: c.block, autopilot: c.autopilot, lost: c.lost, conflict: c.conflict, fl_lo: c.fl_lo, fl_hi: c.fl_hi });
    this.setOpts();
  }

  /* a board's planes, line by line as it sends them */
  take(line) {
    const m = this.m;
    const held = (text, fn) => {
      const at = m.stringToNewUTF8(text);
      fn(at);
      m._free(at);
    };
    if (line.startsWith('OWLOS SKY BEGIN ')) {
      held(line.slice(16), (at) => m._twin_sky_begin(this.now(), at));
      this.inAnswer = true;
    } else if (line.startsWith('OWLOS PLANE ') && this.inAnswer) {
      held(line.slice(12), (at) => m._twin_sky_plane(at));
    } else if (line.startsWith('OWLOS SKY END ') && this.inAnswer) {
      m._twin_sky_end(this.now(), parseInt(line.slice(14), 10) || 0);
      this.inAnswer = false;
      return true;
    } else if (line.startsWith('OWLOS SEL ')) {
      m._twin_pick(parseInt(line.slice(10), 16) || 0);
    }
    return false;
  }

  async tile(k) {
    const m = this.m;
    const url = m.UTF8ToString(m._twin_map_url(k));
    if (!url) {
      return;
    }
    const answer = await fetch(url);
    if (!answer.ok) {
      return;
    }
    const bytes = new Uint8Array(await answer.arrayBuffer());
    const at = m._malloc(bytes.length);
    m.HEAPU8.set(bytes, at);
    m._twin_map_tile(k, at, bytes.length);
    m._free(at);
  }

  async map(tiles) {
    let next = 0;
    const worker = async () => {
      while (next < tiles) {
        const k = next++;
        try {
          await this.tile(k);
        } catch (e) {
          console.warn('tile', k, e);
        }
      }
    };
    await Promise.all([worker(), worker(), worker(), worker()]);
    this.m._twin_map_end();
  }

  /* a finger on the screen changed something: the console says which command that was */
  noticeFinger() {
    const what = this.m._twin_changed();
    if (!what) {
      return;
    }
    if (what & 1) {
      const now = this.cfg();
      for (const [key, word] of [['theme', THEMES], ['look', LOOKS]]) {
        if (this.told && now[key] !== this.told[key]) {
          say(`set ${key}=${word[now[key]]}`, 'cmd');
        }
      }
      if (this.told && now.range !== this.told.range) {
        say(`set range=${Math.round(now.range)}`, 'cmd');
      }
      this.told = now;
      drawPane();
    }
    this.onChange?.(what);
  }

  /* the browser gets a picture already at its size, so it has nothing left to scale or to smooth */
  show() {
    if (this.canvas.isConnected) {
      this.pen.imageSmoothingEnabled = this.k < 1;
      this.pen.drawImage(this.raw, 0, 0, this.canvas.width, this.canvas.height);
    }
  }

  frame(time) {
    if (time - this.last < 66) {
      return;
    }
    this.last = time;
    const m = this.m;
    const f = this.finger;
    const at = m._twin_frame(this.now(), Math.floor(Date.now() / 1000), f.down || f.pressed, f.x, f.y);
    f.pressed = 0;
    this.rawPen.putImageData(new ImageData(new Uint8ClampedArray(m.HEAPU8.buffer, at, this.w * this.h * 4), this.w, this.h), 0, 0);
    this.drawn = true;
    this.show();
    this.noticeFinger();
    const tiles = m._twin_map_wanted();
    if (tiles) {
      this.map(tiles);
    }
  }
}

/* --- what the page knows --- */

const hero = new Screen('Twin');
const desk = new Desk($('desk'));
const app = {
  units: [],             // your boards
  cur: null,             // the one on show
  view: null,            // 'all', 'unit', 'demo' or null
  tab: 'view',
  port: null,            // the port of the board being connected
  chip: null,            // what its bootloader said
  mac: null,             // and its MAC
  troubleFor: null,      // the board the connect window's word of trouble is about
  kind: null,            // the board picked for it
  ours: false,           // it runs an older Flight Radar: its settings stay
  erase: false,          // ticked by the person, never by the page
  busy: null,            // { name, pct } while one is being flashed
};
let scene = null;

function remember() {
  try {
    localStorage.setItem(KEPT, JSON.stringify(app.units.map((u) => ({ mac: u.mac, name: u.name, build: u.build, zoom: u.screen.zoom, usb: u.usb }))));
  } catch (e) {
    console.warn(e);
  }
}

function addUnit({ mac, name, build, online = false, zoom = 1, usb = null }) {
  const screen = new Screen(BOARDS[build].twin);
  screen.zoom = zoom;
  const u = { mac, name, build, online, usb, version: '', wifi: null, nets: [], port: null, link: null, planes: 0, sky: [], screen, ready: screen.start() };
  app.units.push(u);
  return u;
}

const screenNow = () => (app.cur ? app.cur.screen : hero);
const here = () => app.units.filter((u) => u.online);
const byMac = (mac) => app.units.find((u) => u.mac === mac);
const short = (u) => u.name.toLowerCase().replace(/\s+/g, '-').replace(/[^\p{L}\p{N}.-]/gu, '');

const usbName = (port) => ({ 0x1a86: 'CH340', 0x10c4: 'CP210X', 0x303a: 'USB' }[port?.getInfo?.().usbVendorId] ?? 'USB');

/* --- the amber bar: what is going on now, and which board --- */

function barState() {
  if (app.busy) {
    return `FLASHING ${esc(app.busy.name.toUpperCase())} ${app.busy.pct}%`;
  }
  const old = here().find((u) => older(u.version, newest));
  let text = '';
  if (!app.units.length) {
    text = 'NO BOARD: A DEMO SKY';
  } else if (app.view === 'unit' && app.cur?.online) {
    const u = app.cur;
    text = browned(u) ? `${usbName(u.port)} / ${esc(BOARDS[u.build].name)} / TOO LITTLE POWER`
      : `${usbName(u.port)} / ${esc(BOARDS[u.build].name)} / ${u.version} / ${u.wifi ? 'WIFI' : 'NO WIFI'} / ${u.planes} PLANES`;
  } else if (app.view === 'unit' && app.cur) {
    text = 'NOT CONNECTED';
  } else {
    text = `${here().length} OF ${app.units.length} CONNECTED`;
  }
  if (old) {
    text += ` <button class="baract" data-update="${old.mac}">${esc(old.name.toUpperCase())} RUNS ${old.version}: UPDATE</button>`;
  }
  const waits = here().find((u) => u.aligning);
  if (waits) {
    text += ` <button class="baract" data-skip="${waits.mac}">${esc(waits.name.toUpperCase())}: SKIP THE CROSSES</button>`;
  }
  return text;
}

/* --- the picture on top: with a board on a cable its planes fly over its own ground --- */

const KM_PER_UNIT = 0.12;
const UNITS_PER_METRE = 0.12;
let skyUnit = null;                                    // the board the picture shows
let demoGround = null;                                 // the ground of the demo sky

function toScene(a, home) {
  const east = 111.32 * Math.cos(home.lat * Math.PI / 180);
  const way = (a.track ?? 0) * Math.PI / 180;
  const speed = (a.gs ?? 0) * 1.852 / 3600 / KM_PER_UNIT;
  const feet = a.alt_baro;
  const late = a.seen_pos ?? 0;                        // seconds since this position was heard
  return {
    id: a.hex, name: (a.flight ?? '').trim() || a.r || a.hex, type: a.t || 'Unknown type', level: feet >= 6000 ? `FL${Math.round(feet / 100)}` : `${fmt(feet)} ft`,
    knots: Math.round(a.gs ?? 0), feet: `${fmt(feet)} ft`, course: String(Math.round(a.track ?? 0)).padStart(3, '0'), squawk: a.squawk ?? 'none',
    x: (a.lon - home.lon) * east / KM_PER_UNIT + Math.sin(way) * speed * late, y: -(a.lat - home.lat) * 111.32 / KM_PER_UNIT - Math.cos(way) * speed * late,
    z: 30 + Math.min(feet, 45000) / 45000 * 150,
    vx: Math.sin(way) * speed, vy: -Math.cos(way) * speed, heading: Math.atan2(-Math.cos(way), Math.sin(way)), rotor: a.category === 'A7',
  };
}

/* which board: the one on show, or the first on a cable. none: the demo again */
function pickSky() {
  const u = (app.cur?.online ? app.cur : here()[0]) ?? null;
  if (u === skyUnit) {
    return;
  }
  skyUnit = u;
  if (u) {
    planesFrom(u);                                     // what it said last: a board answers only every 15 s
    const { lat, lon } = u.cfg;
    reliefAround(lat, lon, KM_PER_UNIT, UNITS_PER_METRE).then((fn) => {
      if (skyUnit === u) {
        scene.setRelief(fn);
      }
    }).catch((e) => console.warn(e));
  } else {
    scene.setSky(null);
    scene.setRelief(demoGround);
  }
}

/* a whole answer of a board's planes */
function planesFrom(u) {
  if (u === skyUnit) {
    const home = { lat: u.cfg.lat, lon: u.cfg.lon };
    scene.setSky(u.sky.filter((p) => typeof p.lat === 'number' && typeof p.alt_baro === 'number').map((p) => toScene(p, home)));
  }
}

/* what the window of a board says: is it on the cable, on the WiFi, does the plane network answer */
const STALE_MS = 60000;

/* the chip's own guard: the voltage fell, mostly when the WiFi starts. then the board restarts, for ever.
 * the page hears it say so, and a board that does this is not one that "has no WiFi" */
const BROWN_MS = 30000;
const LOW_POWER = 'It keeps restarting: its USB gives too little power. Try another USB port, a shorter cable or a powered hub';
const browned = (u) => Date.now() - (u.browned ?? 0) < BROWN_MS;

function unitState(u) {
  if (!u.online) {
    return ['bad', 'NOT CONNECTED'];
  }
  if (browned(u)) {
    return ['bad', 'TOO LITTLE POWER: IT KEEPS RESTARTING'];
  }
  if (u.aligning) {
    return ['warn', 'TOUCH THE CROSSES'];
  }
  if (!u.wifi) {
    return ['warn', 'NO WIFI'];
  }
  if (u.server === 'DEMO') {
    return ['warn', 'WIFI SET, NOT JOINED: DEMO'];
  }
  if (!u.heard || Date.now() - u.heard > STALE_MS) {
    return ['warn', 'WIFI, NO ANSWER FROM THE PLANES YET'];
  }
  return ['good', `WIFI / ${u.server} / ${u.planes} PLANES`];
}

function showStates() {
  for (const u of app.units) {
    const w = desk.get(u.mac);
    if (w) {
      w.setState(...unitState(u));
      const note = w.q('.livenote');
      if (note) {
        note.innerHTML = liveNote(u);
      }
    }
  }
}

let cwdOf = null;

function renderBar() {
  pickSky();
  showStates();
  const chips = [];
  if (app.units.length > 1) {
    chips.push(`<button class="uchip" data-all aria-pressed="${app.view === 'all'}">ALL</button>`);
  }
  for (const u of app.units) {
    chips.push(`<button class="uchip${u.online ? '' : ' off'}" data-u="${u.mac}" aria-pressed="${app.view === 'unit' && app.cur === u}"><i></i>${esc(u.name.toUpperCase())}</button>`);
  }
  if ('serial' in navigator) {
    chips.push('<button class="uchip add" data-add>+ ADD A BOARD</button>');
  }
  if (desk.wins.size > 1) {
    chips.push('<button class="uchip add" data-arrange>ARRANGE</button>');
  }
  $('barchips').innerHTML = chips.join('');
  $('barstate').innerHTML = barState();
  // the console goes along when another board comes on top, and only then: cd is the person's
  if (term && app.cur !== cwdOf) {
    cwdOf = app.cur;
    term.shell.cwd = app.cur ? `/boards/${short(app.cur)}` : '/boards';
    term.refresh();
  }
}

$('bar').addEventListener('click', (e) => {
  const b = e.target.closest('button');
  if (!b) {
    return;
  }
  if (b.dataset.all !== undefined) {
    showAll();
  } else if (b.dataset.add !== undefined) {
    addBoard();
  } else if (b.dataset.arrange !== undefined) {
    desk.arrange();
  } else if (b.dataset.u) {
    showLive(byMac(b.dataset.u));
  } else if (b.dataset.update) {
    updateThen(byMac(b.dataset.update));
  } else if (b.dataset.skip) {
    term.run(`skip --board ${short(byMac(b.dataset.skip))}`);
  }
});

/* --- the console --- */

/* what went along each cable: { port, text, dir }. dir: 'in' a board's line, 'page' what the page did with
 * the port and the flasher's words, 'out' a line the page sent, 'sky' a board's planes coming. the
 * console's log has the first two, a board's PORT window all four */
const boardLog = [];
let following = false;                                 // true: every board, or the one board
let term = null;

const named = (l) => {
  const u = app.units.find((x) => x.port === l.port);
  return u ? `${short(u)}: ${l.text}` : l.text;
};

/* what one board said, or with no board what they all said, each line with its name */
const logLines = (u) => boardLog.filter((l) => (l.dir === 'in' || l.dir === 'page') && (!u || l.port === u.port)).slice(-200).map((l) => (u ? l.text : named(l)));

/* a line the page sends, as the PORT window shows it: a password or a key never */
const secretless = (line) => line.replace(/"(pass|key)":"(?:[^"\\]|\\.)*(?:"|\\?$)/g, '"$1":"***"');

function heard(port, text, dir) {
  if (!port) {
    return;
  }
  const l = { port, text, dir };
  boardLog.push(l);
  if (boardLog.length > 4000) {
    boardLog.shift();
  }
  for (const u of app.units.filter((v) => v.port === port)) {
    portRow(u, l);
    if (u.online && (dir === 'in' || dir === 'sky')) {
      desk.get(u.mac)?.q('.onair i')?.classList.add('beat');   // the LIVE dot beats with what the board says
    }
  }
}

/* what the page does, in the console: a button's command as if typed, its answer, a board's log.
 * 'board' a line the board said, 'page' what the page did with a board's port */
function say(text, kind = '', port = null) {
  if (kind === 'cmd') {
    term.echo(text);
  } else if (kind === 'board' || kind === 'page') {
    heard(port, text, kind === 'board' ? 'in' : 'page');
    if (following === true) {
      term.write(`\x1b[90m${named({ port, text })}\x1b[0m\r\n`);
    } else if (following && following.port === port) {
      term.write(`\x1b[90m${text}\x1b[0m\r\n`);
    }
  } else {
    term.write(`\x1b[90m${text}\x1b[0m\r\n`);
  }
}

/* a setting on one board: the screen here, and the board itself */
function setOne(u, name, value) {
  const s = u ? u.screen : hero;
  if (name === 'flip') {                               // the board's own screen: this page shows it upright anyway
    if (!u?.link || !['on', 'off'].includes(value)) {
      return false;
    }
    u.link.send(`OWLOS SET ${JSON.stringify({ flip: value === 'on' ? 1 : 0 })}`).then(() => u.link.send('OWLOS GET')).catch((e) => console.warn(e));
    return true;
  }
  if (!s.m || !s.set(name, value)) {
    return false;
  }
  if (u?.link) {
    u.link.send(`OWLOS SET ${s.setJson()}`).catch((e) => console.warn(e));
  }
  drawPane();
  return true;
}

const FILES = {
  info: (u) => unitFacts(u).map(([k, v]) => `${k}: ${v}`),
  config: (u) => {
    const c = u.screen.cfg();
    return [`theme=${THEMES[c.theme]}`, `look=${LOOKS[c.look]}`, `range=${Math.round(c.range)}`, `flip=${u.cfg?.flip ? 'on' : 'off'}`,
      ...Object.keys(SWITCHES).map((k) => `${k}=${u.screen.opts[k] ? 'on' : 'off'}`)];
  },
  wifi: (u) => (u.nets?.length ? [...u.nets, '(passwords go in, never out)'] : ['no network set']),
  log: (u) => logLines(u),
};

const boardApi = {
  units: () => app.units,
  byName: (name) => app.units.find((u) => short(u) === name),
  short: (u) => short(u),
  kind: (u) => BOARDS[u.build].name,
  current: () => app.cur,
  showAll: () => showAll(),
  showLive: (u) => showLive(u),
  files: FILES,
  settings: [...THEMES.map((v) => `theme=${v}`), ...LOOKS.map((v) => `look=${v}`), ...RANGES.map((v) => `range=${v}`), ...Object.keys(SWITCHES).flatMap((k) => [`${k}=on`, `${k}=off`]), 'flip=on', 'flip=off'],
  set: setOne,
  async send(u, line) {
    if (!u.link) {
      throw new Error(`${short(u)} is not connected`);
    }
    await u.link.send(line);
  },
  findHome: (text) => findHome(text),
  screens: (u) => SCREENS[BOARDS[u.build].screens]?.names ?? [],
  flash: (u) => updateThen(u),
  follow(on, u) {
    following = on ? (u ?? true) : false;
  },
  logLines,
};

function consoleWin() {
  let w = desk.get('console');
  if (!w) {
    w = desk.make('console', 'Console', { className: 'console', onClose: () => term.toggle(false), onResize: () => term.fit() });
    w.body.append($('termbody'));
  }
  return w;
}

term = new Terminal({
  makeShell: (io) => new Shell({
    ...io,
    commands: [...CORE_COMMANDS, ...boardCommands(boardApi)],
    groups: [...BOARD_GROUPS, ...CORE_GROUPS],
    paths: (shell) => new BoardPaths(shell, boardApi),
    home: '/boards',
    host: 'radar',
    user: 'pilot',
  }),
  onToggle(open) {
    const w = consoleWin();
    if (open) {
      w.show();
      w.front();
    } else {
      w.root.hidden = true;
      desk.layout();
    }
  },
});

/* --- a board's own terminal: what went along its cable, as it went --- */

const portId = (u) => `port ${u.mac}`;

function rowOf(l) {
  const row = document.createElement('div');
  row.className = `l-${l.dir}`;
  row.textContent = l.dir === 'out' ? `> ${l.text}` : l.text;
  return row;
}

/* one line more in a board's PORT window, when it is open. the newest stay in sight unless scrolled back */
function portRow(u, l) {
  const w = desk.get(portId(u));
  if (!w || w.root.hidden) {
    return;
  }
  const log = w.q('.portlog');
  const end = log.scrollHeight - log.scrollTop - log.clientHeight < 24;
  log.querySelector('.none')?.remove();
  log.append(rowOf(l));
  while (log.childElementCount > 500) {
    log.firstElementChild.remove();
  }
  if (end) {
    log.scrollTop = log.scrollHeight;
  }
}

function fillPort(u) {
  const w = desk.get(portId(u));
  if (!w) {
    return;
  }
  const log = w.q('.portlog');
  const lines = u.port ? boardLog.filter((l) => l.port === u.port).slice(-500) : [];
  log.replaceChildren(...lines.map(rowOf));
  if (!lines.length) {
    const none = rowOf({ dir: 'page', text: u.port ? 'nothing yet' : 'no cable of this board in this page yet: plug it in, or FIND ITS PORT' });
    none.classList.add('none');
    log.append(none);
  }
  log.scrollTop = log.scrollHeight;
}

/* the planes' own lines are left out: a hundred every 15 seconds */
function portWin(u) {
  let w = desk.get(portId(u));
  if (!w) {
    w = desk.make(portId(u), `${u.name}: port`, { className: 'port' });
    w.body.innerHTML = `<div class="portlog" role="log"></div>
      <form class="portsend"><span>&gt;</span><input aria-label="a line to the board" placeholder="a line to the board as it is, Enter sends it: OWLOS GET" autocomplete="off" spellcheck="false"></form>`;
    w.q('.portsend').addEventListener('submit', (e) => {
      e.preventDefault();
      const input = w.q('input');
      const line = input.value.trim();
      if (!line) {
        return;
      }
      input.value = '';
      if (u.link) {
        u.link.send(line).catch((err) => heard(u.port, err.message, 'page'));
      } else {
        portRow(u, { dir: 'page', text: 'not connected: nothing went to the board' });
      }
    });
  }
  w.show();
  fillPort(u);
  w.front();
  w.root.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  return w;
}

/* --- a board on a cable --- */

/* connecting and flashing lie over the picture on top: the board they bring shows in it */
function wizard() {
  let w = desk.get('connect');
  if (!w) {
    w = desk.make('connect', 'Connect a device', { className: 'wide', dock: $('sky') });
    w.body.innerHTML = '<ol class="steps" id="steps"><li>1 CONNECT</li><li>2 YOUR BOARD</li><li>3 FLASH</li><li>4 LIVE</li></ol><div id="stage"></div>';
  }
  w.show();
  w.front();
  w.root.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  return w;
}

function step(n) {
  wizard();
  $('steps').hidden = false;
  [...$('steps').children].forEach((li, i) => {
    li.className = i + 1 === n ? 'now' : i + 1 < n ? 'done' : '';
  });
}

function manage() {
  wizard();
  $('steps').hidden = true;
}

function stage(html) {
  wizard();
  app.troubleFor = null;
  const s = $('stage');
  s.innerHTML = `<div class="stage">${html}</div>`;
  desk.layout();
  return s;
}

/* unit: the board this was for, and its PORT window shows what came */
function trouble(title, text, { again = () => addBoard(), unit = null } = {}) {
  stage(`<h2>${esc(title)}</h2><p>${esc(text)}</p><div class="row"><button class="btn" id="again">TRY AGAIN</button>${unit ? '<button class="btn quiet" id="seeport">PORT</button>' : ''}</div>`);
  app.troubleFor = unit;                               // that board coming back by itself closes this
  $('again').addEventListener('click', again);
  $('seeport')?.addEventListener('click', () => portWin(unit));
}

/* the USB chip a board's cable ends in: the browser's list can then show the ports with that chip alone */
function usbOf(port) {
  const { usbVendorId, usbProductId } = port?.getInfo?.() ?? {};
  if (!usbVendorId) {
    return null;
  }
  return usbProductId ? { usbVendorId, usbProductId } : { usbVendorId };
}

/* a board this page knows by its MAC that runs another build now, flashed here or anywhere else: its
 * screen and what an update flashes follow the board. a name the page gave it goes too, one its owner
 * gave stays. kept as it was, an update would flash the old build back */
function rebuild(u, build, port) {
  say(`${u.name} runs the ${BOARDS[build].name} build now`, 'page', port);
  if (u.name === BOARDS[u.build].name) {
    u.name = BOARDS[build].name;
    desk.get(u.mac)?.setTitle(u.name);
    desk.get(portId(u))?.setTitle(`${u.name}: port`);
  }
  u.build = build;
  const zoom = u.screen.zoom;
  u.screen = new Screen(BOARDS[build].twin);         // the window and the picture on top take it at their next draw
  u.screen.zoom = zoom;
  u.ready = u.screen.start();
  remember();
}

/* a Flight Radar answered: it is one of your boards from now on. it goes live last of all: a try that
 * breaks before that leaves the board as it was, never on a link that is gone */
async function attach(link, port, who) {
  let u = byMac(who.mac);
  if (u && u.build !== who.build) {
    rebuild(u, who.build, port);
  }
  if (!u) {
    u = addUnit({ mac: who.mac, name: BOARDS[who.build].name, build: who.build });
    remember();
  }
  await u.ready;
  const c = await link.config();
  if (!c) {
    throw new Error('the board did not say what it has');
  }
  if (link.ended) {
    throw new Error('the cable went away');            // while the board was being asked
  }
  u.screen.fromBoard(c);
  u.screen.onChange = (what) => {
    if (what & 1) {
      link.send(`OWLOS SET ${u.screen.setJson()}`).catch((e) => console.warn(e));
    }
    if (what & 2) {
      link.send(`OWLOS PICK ${(u.screen.m._twin_sel() >>> 0).toString(16)}`).catch((e) => console.warn(e));
    }
  };
  link.onLine = (line) => {
    if (line.includes('Brownout detector')) {
      u.browned = Date.now();
      renderBar();
      showStates();
    }
    if (line.startsWith('OWLOS CFG ')) {
      const now = JSON.parse(line.slice(10));
      const moved = now.lat !== u.cfg?.lat || now.lon !== u.cfg?.lon;
      u.wifi = now.ssid || null;
      u.nets = now.nets ?? [];
      u.cfg = now;
      u.screen.fromBoard(now);
      if (moved && u === skyUnit) {
        skyUnit = null;                                // a new home: the ground under it again
        pickSky();
      }
      const names = SCREENS[BOARDS[u.build].screens]?.names ?? [];
      desk.get(u.mac)?.body.querySelectorAll('[data-screen]').forEach((el) => {
        el.setAttribute('aria-pressed', String(names[now.screen] === el.dataset.screen));
      });
      drawPane(u);
    } else if (line.startsWith('OWLOS ALIGN ')) {
      u.aligning = line.endsWith('1');
      renderBar();
    } else if (line.startsWith('OWLOS PLANE ')) {
      u.screen.take(line);
      try {
        u.sky.push(JSON.parse(line.slice(12)));
      } catch (e) {
        console.warn(e);
      }
    } else if (line.startsWith('OWLOS SKY BEGIN ')) {
      u.screen.take(line);
      u.sky = [];
      u.server = line.slice(16).trim().toUpperCase();
      u.heard = Date.now();
    } else if (u.screen.take(line)) {
      u.planes = parseInt(line.slice(14), 10) || 0;
      renderBar();
      planesFrom(u);
    }
    if (line.startsWith('OWLOS SKY')) {
      heard(port, line, 'sky');                        // in its PORT window, not in the console's log
    } else if (!line.startsWith('OWLOS PLANE')) {
      say(line, 'board', port);
    }
  };
  link.onLost = () => {
    if (u.link !== link) {
      return;                                          // the board is on a newer link already
    }
    u.online = false;
    u.link = null;
    u.screen.m._twin_board(0);
    say(`${u.name} is gone from its cable`, 'dim');
    heard(port, 'the cable went away', 'page');
    if (desk.get(u.mac)) {
      fillBoard(u);
    }
    renderBar();
  };
  Object.assign(u, { link, port, online: true, nextLook: 0, version: who.version, wifi: c.ssid || null, nets: c.nets ?? [], cfg: c });
  fillPort(u);
  for (const v of app.units) {
    if (v !== u && v.port === port && !v.link) {
      v.port = null;                                   // a port tried for another board was this one's
      fillPort(v);
    }
  }
  const usb = usbOf(port);
  if (usb && JSON.stringify(usb) !== JSON.stringify(u.usb)) {
    u.usb = usb;
    remember();
  }
  link.attached = true;
  link.send('OWLOS SKY ON').catch((e) => console.warn(e));   // a send that fails is tried again below
  link.keep = setInterval(() => {
    link.send('OWLOS SKY ON').catch((e) => console.warn(e));
  }, 30000);
  tellHelp(u, false);
  askHelp();
  return u;
}

/* the visitor's answer about counting, for a board: it reports itself only with a 1. a board its owner
 * answered on its own screen keeps that answer, unless the visitor answers here again */
function tellHelp(u, again) {
  const a = helpAnswer();
  const want = a === 'yes' ? 1 : 2;
  if (a && u.link && (again ? u.cfg?.help !== want : !u.cfg?.help)) {
    u.link.send(`OWLOS SET {"help":${want}}`).catch((e) => console.warn(e));
  }
}
onHelp(() => {
  for (const u of here()) {
    tellHelp(u, true);
  }
});

/* the try running on each port. the browser's connect event, the look for returns, a click and the
 * start can all reach one port at once: the second waits for the first, the port is opened once */
const trying = new WeakMap();

/* one port, whatever is on it. an answer of ours: attached. anything else: the caller goes on, told
 * whether anything at all came. a port a board of ours is live on is that board, left as it is */
function tryPort(port) {
  const live = app.units.find((u) => u.port === port && u.link);
  if (live) {
    return Promise.resolve({ unit: live });
  }
  if (!trying.has(port)) {
    // and never longer than this: a try that hangs would otherwise hold the port for every try after it
    const late = wait(30000).then(() => {
      throw new Error('the port gave no answer in 30 seconds');
    });
    trying.set(port, Promise.race([tryOnce(port), late]).finally(() => trying.delete(port)));
  }
  return trying.get(port);
}

async function tryOnce(port) {
  const link = new Link(port);
  let low = false;
  link.onLine = (line) => {                            // what a board says while it starts: `log` shows it
    low = low || line.includes('Brownout detector');
    say(line, 'board', port);
  };
  link.onSent = (line) => heard(port, secretless(line), 'out');
  let who;
  try {
    await link.open();                                 // in the try: a port opened half way is closed again
    heard(port, 'port open, 115200 baud', 'page');
    who = await link.hello();
    if (who?.mac && BOARDS[who.build]) {
      return { unit: await attach(link, port, who) };
    }
  } catch (e) {
    await link.close();                                // a try that broke half way must not keep the port
    heard(port, e.notOpened ? 'the port did not open' : `port closed: ${e.message}`, 'page');
    throw low ? new Error(LOW_POWER) : e;
  }
  const silent = link.bytes === 0;
  if (!who) {
    say(silent ? 'no answer: not a byte came from this port' : `no answer of a Flight Radar: ${link.bytes} bytes came`, 'page', port);
  }
  await link.close();
  heard(port, 'port closed', 'page');
  if (low) {
    throw new Error(LOW_POWER);
  }
  return { who, silent };
}

/* wanted: a board this page knows and does not have. the browser does not always give a page back the
 * cable of a board plugged in before it started (seen with CH340 boards in Chrome): its own list
 * then, with this board's USB chip alone when the page knows it */
async function addBoard(wanted = null) {
  if (app.busy || app.settling) {
    wizard();                                          // one board at a time: the one being flashed is in it
    return;
  }
  let port;
  try {
    port = await navigator.serial.requestPort({ filters: wanted?.usb ? [wanted.usb] : [] });
  } catch (e) {
    if (e.name !== 'NotFoundError') {
      trouble('No port', e.message);
    }
    return;
  }
  const owner = app.units.find((u) => u.port === port && u.link);
  if (owner) {                                         // this cable is in use already: that is this board
    if (wanted && owner !== wanted) {
      say(`that port is ${owner.name}: ${wanted.name} is on another one`, 'dim');
    }
    showLive(owner);
    return;
  }
  step(1);
  stage('<h2>Asking the board</h2><p>One moment.</p>');
  say('connect', 'cmd');
  app.port = port;
  app.kind = null;
  app.ours = false;
  app.chip = null;
  app.mac = null;
  const before = wanted?.port ?? null;
  if (wanted) {
    wanted.port = port;                                // what this cable carries shows in its PORT window, answer or not
    fillPort(wanted);
  }
  const notHere = () => {
    if (wanted) {
      wanted.port = before;
      fillPort(wanted);
    }
  };
  const again = () => addBoard(wanted);
  try {
    const { unit, who, silent } = await tryPort(port);
    if (unit) {
      say(`${unit.name}: Flight Radar ${unit.version}`, 'dim');
      if (wanted && unit !== wanted) {
        notHere();
        say(`that port is ${unit.name}: ${wanted.name} is on another one`, 'dim');
      }
      showLive(unit, { top: true });
      return;
    }
    if (who) {                                         // an older Flight Radar: it can't name itself
      notHere();
      app.ours = true;
      say(`Flight Radar ${who.version}`, 'dim');
      showBoards(true, who.version);
      return;
    }
    if (wanted) {
      // a board with this firmware on it that does not answer: its chip's bootloader would only keep it dark.
      // and no tries by themselves on this port: a cable that carries nothing back needs a hand
      wanted.nextLook = Infinity;
      trouble(silent ? 'Nothing came from that port' : 'That port does not answer as a Flight Radar', silent
        ? `The cable carried not a single byte back. Take the cable of ${wanted.name} out and put it back: it then comes back by itself, or with TRY AGAIN.`
        : 'Is it the right port? PORT shows what came along it.', { again, unit: wanted });
      return;
    }
    say('no Flight Radar on it, asking the chip', 'dim');
    const found = await detect(port, (t) => say(t, 'page', port));
    app.chip = found.chip;
    app.mac = found.mac;                               // a board on its chip's own USB comes back on a new port after flashing
    say(`chip ${found.name}`, 'dim');
    showBoards(!found.chip);
  } catch (e) {
    console.warn(e);
    if (wanted) {
      wanted.nextLook = Infinity;
    }
    trouble(e.notOpened ? 'The port did not open' : 'The board did not answer', e.notOpened ? `${e.message}.`
      : `${e.message}. Is it the right port? What came along it: ${wanted ? 'PORT' : 'CONSOLE, then log --all'}.`, { again, unit: wanted });
  }
}

function card(id) {
  const b = BOARDS[id];
  return `<button class="board" data-board="${id}" aria-pressed="${app.kind === id}"><img src="shots/${b.shot}" alt=""><b>${b.name}</b><span>${b.spec}</span></button>`;
}

function showBoards(all = false, oldVersion = null) {
  step(2);
  const fits = Object.keys(BOARDS).filter((id) => BOARDS[id].chip === app.chip);
  const list = all ? Object.keys(BOARDS) : fits;
  if (!all && fits.length === 1) {
    app.kind = fits[0];
  }
  const sure = !all && fits.length === 1;
  const words = oldVersion ? `It runs Flight Radar ${esc(oldVersion)}, which can't say which board it is. Pick yours: your settings stay.`
    : all ? 'All the boards we have firmware for. Pick yours.'
      : sure ? 'This chip goes on one board of ours.'
        : `It has an ${CHIP_NAME[app.chip]}. The chip does not say which screen is on it, so pick yours.`;
  const name = app.kind ? BOARDS[app.kind].name : null;
  const s = stage(`<h2>${sure ? `We found your board: ${name}` : 'Which one is yours?'}</h2><p>${words}</p>
    <div class="boards">${list.map(card).join('')}</div>
    <div class="row"><button class="btn" id="go" ${app.kind ? '' : 'disabled'}>${app.kind ? `FLASH ${newest} TO THE ${name.toUpperCase()}` : 'PICK YOUR BOARD'}</button>
    ${all ? '' : '<button class="plain" id="all">Not this one? Show all boards</button>'}</div>
    <label class="erase"><input type="checkbox" id="erase" ${app.erase ? 'checked' : ''}> Erase the board first: its WiFi, settings and saved maps go too</label>`);
  $('erase').addEventListener('change', (e) => {
    app.erase = e.target.checked;
  });
  s.querySelectorAll('.board').forEach((el) => {
    el.addEventListener('click', () => {
      app.kind = el.dataset.board;
      showBoards(all, oldVersion);
    });
  });
  s.querySelector('#all')?.addEventListener('click', () => showBoards(true, oldVersion));
  s.querySelector('#go').addEventListener('click', flashNew);
}

/* --- flashing --- */

async function flashing(port, build, name, erase, others) {
  app.busy = { name, pct: 0 };
  renderBar();
  stage(`<h2>Flashing ${esc(name)}</h2><p>Leave the cable in, it takes about a minute.${others ? ' The other boards go on running.' : ''}</p>
    <div class="flashing"><span id="what">Starting</span><b id="pct">0%</b></div><div class="bar"><i id="fill"></i></div>`);
  say(`flash ${newest}`, 'cmd');
  try {
    await writeFirmware(port, build, erase, (pct) => {
      app.busy.pct = pct;
      if ($('pct')) {
        $('what').textContent = pct < 100 ? 'Writing' : 'Restarting';
        $('pct').textContent = `${pct}%`;
        $('fill').style.width = `${pct}%`;
      }
      $('barstate').textContent = `FLASHING ${name.toUpperCase()} ${pct}%`;
    }, (t) => say(t, 'page', port));
    reportFlash(build, newest);                        // counted for the map only when the visitor allowed it
  } finally {
    app.busy = null;
  }
}

/* the board restarts after flashing, and a board with its own USB comes back as a new port */
async function afterFlash(port) {
  app.why = 'it stayed silent';
  app.settling = true;                                 // this port is ours for now: lookForReturns keeps off
  try {
    await wait(1500);
    for (let t = 0; t < 4; t++) {
      try {
        const { unit } = await tryPort(port);
        if (unit) {
          return unit;
        }
        say('no answer yet', 'page', port);
      } catch (e) {
        console.warn(e);
        app.why = e.message;
        say(e.message, 'page', port);
      }
      await wait(1500);
    }
    return null;
  } finally {
    app.settling = false;
  }
}

async function flashNew() {
  step(3);
  const build = app.kind;
  const port = app.port;
  try {
    await flashing(port, build, BOARDS[build].name, app.erase, here().length);
  } catch (e) {
    console.warn(e);
    trouble('Flashing stopped', `${e.message}. Nothing was left half way that a new try can't fix.`);
    return;
  }
  // a board on its chip's own USB comes back as a new port, and the browser's connect event brings it
  const u = (await afterFlash(port)) ?? app.units.find((x) => x.mac === app.mac && x.online);
  if (u) {
    showLive(u, { top: true });
  } else {
    stage(`<h2>Flashed</h2><p>The firmware is on the board, but the page could not reach it: ${esc(app.why)}. Press its reset button, or plug it in again, and connect it. What it said: CONSOLE, then log --all.</p><div class="row"><button class="btn" id="again">CONNECT</button></div>`);
    $('again').addEventListener('click', () => addBoard());
  }
}

async function update(u) {
  if (!u.link) {
    throw new Error(`${u.name} is not connected`);
  }
  manage();
  const port = u.port;
  app.port = port;                                     // the connect window is about this port now
  const link = u.link;
  u.link = null;
  u.online = false;
  await link.close();
  await flashing(port, u.build, u.name, false, here().length);
  return (await afterFlash(port)) ?? u;
}

async function updateThen(u) {
  try {
    showLive(await update(u), { top: true });
  } catch (e) {
    console.warn(e);
    trouble('Flashing stopped', e.message);
  }
}

async function updateAll() {
  for (const u of here().filter((v) => older(v.version, newest))) {
    await update(u);
  }
  showAll();
}

/* --- a board's window --- */

/* a board's screen one to one: its pixel is one pixel of the monitor, the picture exactly as the board
 * has it. + and - beside it make that a whole square of two, three, as the person likes. counted in the
 * monitor's pixels, not the page's, or a Windows scale of 125% stretches and blurs it. only a screen
 * too big for its place is shrunk, and smoothed */
function fit(el, screen, maxed = false) {
  const photo = el.classList.contains('yellow');
  const dpr = devicePixelRatio || 1;
  const wide = innerWidth - (maxed ? 60 : 100);
  const aroundX = photo ? (PHOTO.left + PHOTO.right) / PHOTO.w : 0;
  const aroundY = photo ? (PHOTO.top + PHOTO.bottom) / PHOTO.w * (screen.w / screen.h) : 0;
  const room = wide * dpr / (1 + aroundX);
  const high = Math.max(240, innerHeight * (maxed ? 0.74 : 0.8)) * dpr / (1 + aroundY);
  const most = Math.min(room / screen.w, high / screen.h);
  const base = Math.max(1, Math.round(dpr));
  screen.zoomMost = Math.max(1, Math.floor(most / base));
  const want = maxed ? screen.zoomMost : Math.min(screen.zoom, screen.zoomMost);
  const k = Math.min(most, base * want);
  const w = Math.round(screen.w * k);
  const h = Math.round(screen.h * k);
  screen.k = k;
  screen.canvas.width = w;
  screen.canvas.height = h;
  screen.canvas.style.width = `${w / dpr}px`;
  screen.canvas.style.height = `${h / dpr}px`;
  screen.canvas.style.imageRendering = k >= 1 ? 'pixelated' : 'auto';
  if (photo) {
    const s = w / dpr / PHOTO.w;
    el.style.borderWidth = `${PHOTO.top * s}px ${PHOTO.right * s}px ${PHOTO.bottom * s}px ${PHOTO.left * s}px`;
  }
  screen.show();
}

/* the photo of a CYD round the screen: where the glass lies in img/board-35.png, its index.html border-image */
const PHOTO = { top: 34, right: 78, bottom: 24, left: 100, w: 522 };

function facts(u) {
  return u ? unitFacts(u) : [['Board', 'none, a demo sky in this page']];
}

function unitFacts(u) {
  const b = BOARDS[u.build];
  return [['Board', b.name], ['Chip', CHIP_NAME[b.chip]], ['Screen', `${b.w} x ${b.h}, ${b.inch}"`], ['Touch', b.touch], ['Firmware', u.version || 'not known yet'], ['Cable', u.online ? usbName(u.port) : 'not connected']];
}

/* the boards sold with more than one screen: the board can't tell which it has. the page switches it
 * at once, BOOT in the first 8 seconds does the same on the board itself. the order is board.h's */
const SCREENS = {
  three: { names: ['V1', 'V2', 'V3'], words: 'Picture mirrored, upside down, like a negative or in wrong colours? This board is sold with three screens, try the others:' },
  two: { names: ['RGB', 'BGR'], words: 'Red and blue swapped? Try the other colour order:' },
};

function screenWords(u) {
  const s = SCREENS[BOARDS[u.build].screens];
  if (!s) {
    return '';
  }
  const chips = s.names.map((n, i) => `<button class="chip" data-screen="${n}" aria-pressed="${u.cfg?.screen === i}">${n}</button>`).join('');
  const boot = BOARDS[u.build].noboot ? '' : ' Or press BOOT within 8 seconds after a restart.';
  return `<div class="note wait boothint"><p>${s.words}</p><div class="chips">${chips}</div>
    <p>The board keeps the one you stop on.${boot} <a href="previous/#boot" target="_blank">More</a></p></div>`;
}

function liveNote(u) {
  if (!u) {
    return '<p class="note wait"><b>A demo sky.</b> Tap a plane, drag the rim to turn the map, drag inside to zoom.</p>';
  }
  if (browned(u)) {
    return '<p class="note wait"><b>Too little power.</b> The board restarts each time its WiFi starts, so this picture is not what it shows. Try another USB port, a shorter cable or a powered hub.</p>';
  }
  if (!u.wifi) {
    return '<p class="note wait"><b>No WiFi yet.</b> SETTINGS has the WiFi.</p>';
  }
  return `<p class="note"><b>On "${esc(u.wifi)}".</b></p>`;
}

async function findHome(text) {
  const url = `https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&accept-language=en&q=${encodeURIComponent(text)}`;
  const list = await (await fetch(url)).json();
  return list[0] ? { lat: Number(list[0].lat), lon: Number(list[0].lon) } : null;
}

const winOf = (u) => desk.get(u ? u.mac : 'demo');

function zoomWords(u) {
  const w = winOf(u);
  const s = u ? u.screen : hero;
  if (w?.q('.zw')) {
    const z = w.maxed ? s.zoomMost : Math.min(s.zoom, s.zoomMost);
    w.q('.zw').textContent = `${z}x`;
    w.q('.zo').disabled = z <= 1 || w.maxed;
    w.q('.zi').disabled = z >= s.zoomMost || w.maxed;
  }
}

function refit(u) {
  const w = winOf(u);
  if (w?.q('.gl')) {
    fit(w.q('.gl'), u ? u.screen : hero, w.maxed);
    zoomWords(u);
  }
}

function zoomBy(u, step) {
  const s = u ? u.screen : hero;
  s.zoom = Math.max(1, Math.min(s.zoomMost, Math.min(s.zoom, s.zoomMost) + step));
  refit(u);
  remember();
}

addEventListener('resize', () => {
  for (const u of [null, ...app.units]) {
    refit(u);
  }
});

function panePart(u, tab) {
  const w = winOf(u);
  const pane = w.q('.pane');
  const s = u ? u.screen : hero;
  w.body.querySelectorAll('.tabs button').forEach((t) => t.setAttribute('aria-selected', String(t.dataset.tab === tab)));
  if (tab === 'setup') {
    if (!u) {
      pane.innerHTML = '<small>The WiFi and your home go to a board. Connect one to set them.</small>';
      return;
    }
    if (document.activeElement?.closest?.('.pane') === pane) {
      return;                                          // a board's answer must not wipe what is being typed
    }
    const nets = (u.nets ?? []).map((n) => `<div class="net"><span>${esc(n)}</span><button class="plain" data-forget="${esc(n)}">Forget</button></div>`).join('');
    pane.innerHTML = `<label>WiFi network<input type="text" class="ssid" autocomplete="off"></label>
      <label>Password<input type="password" class="pass" autocomplete="off"></label>
      ${nets ? `<div class="nets"><span>Kept on the board</span>${nets}</div>` : ''}
      <label>Your town<input type="text" class="home" autocomplete="off" placeholder="a town, a postcode or a point"></label>
      <button class="btn quiet save">SEND TO THE BOARD</button>
      <small class="saved">They go to the board over the cable, never to a server of ours. A town is looked up on OpenStreetMap.</small>`;
    pane.querySelectorAll('[data-forget]').forEach((el) => {
      el.addEventListener('click', () => term.run(`wifi forget --board ${short(u)} ${el.dataset.forget}`));
    });
    pane.querySelector('.save').addEventListener('click', async () => {
      const set = {};
      const ssid = pane.querySelector('.ssid').value.trim();
      const town = pane.querySelector('.home').value.trim();
      if (ssid) {
        set.ssid = ssid;
        set.pass = pane.querySelector('.pass').value;
        say(`wifi add ${ssid}`, 'cmd');
      }
      if (town) {
        const found = await findHome(town);
        if (!found) {
          pane.querySelector('.saved').textContent = `OpenStreetMap does not know "${town}".`;
          return;
        }
        Object.assign(set, found);
        say(`home ${town}`, 'cmd');
      }
      if (Object.keys(set).length && u.link) {
        await u.link.send(`OWLOS SET ${JSON.stringify(set)}`);
        await u.link.send('OWLOS GET');
        pane.querySelector('.saved').textContent = 'Sent. The console says how it goes: log on.';
      }
    });
  } else if (tab === 'view') {
    const c = s.cfg();
    const range = RANGES.reduce((best, r, i) => (Math.abs(r - c.range) < Math.abs(RANGES[best] - c.range) ? i : best), 0);
    const round = u && BOARDS[u.build].round;
    pane.innerHTML = `${round ? '' : `<div class="field"><span>Look</span><div class="chips">${LOOKS.map((l, i) => `<button class="chip" data-set="look=${l}" aria-pressed="${c.look === i}">${l.toUpperCase()}</button>`).join('')}</div></div>`}
      <div class="field"><span>Colours</span><div class="chips">${THEMES.map((t, i) => `<button class="chip" data-set="theme=${t}" aria-pressed="${c.theme === i}">${t.toUpperCase()}</button>`).join('')}</div></div>
      <label>Range: <b class="rangeword">${Math.round(c.range)}</b> km<input type="range" class="range" min="0" max="${RANGES.length - 1}" value="${range}"></label>
      <div class="field"><span>On the radar</span><div class="chips">${Object.entries(SWITCHES).map(([k, v]) => `<button class="chip" data-set="${k}=${s.opts[k] ? 'off' : 'on'}" aria-pressed="${!!s.opts[k]}">${v.toUpperCase()}</button>`).join('')}</div></div>
      ${u ? `<div class="field"><span>The board's screen</span><div class="chips"><button class="chip" data-set="flip=${u.cfg?.flip ? 'off' : 'on'}" aria-pressed="${!!u.cfg?.flip}">UPSIDE DOWN</button></div></div>` : ''}`;
    const apply = (name, value) => {
      if (u) {
        term.run(`set ${name}=${value} --board ${short(u)}`);
      } else {
        setOne(null, name, value);
      }
    };
    pane.querySelectorAll('[data-set]').forEach((el) => {
      el.addEventListener('click', () => {
        const [name, value] = el.dataset.set.split('=');
        apply(name, value);
      });
    });
    const slider = pane.querySelector('.range');
    slider.addEventListener('input', () => {
      pane.querySelector('.rangeword').textContent = RANGES[slider.value];
    });
    slider.addEventListener('change', () => apply('range', RANGES[slider.value]));
  } else {
    pane.innerHTML = `${u ? `<label>Name<input type="text" class="rename" autocomplete="off" value="${esc(u.name)}"></label>` : ''}
      <dl class="facts">${facts(u).map(([k, v]) => `<dt>${k}</dt><dd>${esc(v)}</dd>`).join('')}</dl>
      ${u ? '<button class="plain forget">Forget this board</button><small class="forgetnote" hidden>Only this page forgets it: the firmware and its settings stay on the board, and + ADD A BOARD brings it back. Click again to forget it.</small>' : ''}`;
    pane.querySelector('.rename')?.addEventListener('change', (e) => {
      u.name = e.target.value.trim() || u.name;
      w.setTitle(u.name);
      desk.get(portId(u))?.setTitle(`${u.name}: port`);
      remember();
      renderBar();
    });
    pane.querySelector('.forget')?.addEventListener('click', async (e) => {
      const note = pane.querySelector('.forgetnote');
      if (note.hidden) {                               // the first click only says what it means
        note.hidden = false;
        e.target.textContent = 'Yes, forget this board';
        return;
      }
      if (u.link) {
        await u.link.close();
      }
      app.units = app.units.filter((v) => v !== u);
      remember();
      desk.remove(u.mac);
      desk.remove(portId(u));
      say(`board forget "${u.name}"`, 'cmd');
      app.cur = null;
      renderBar();
    });
  }
}

/* the settings of a board again: what it said changed them */
function drawPane(u = app.cur) {
  const w = winOf(u);
  if (w?.q('.pane') && !w.q('.side').hidden) {
    panePart(u, w.tab ?? 'view');
  }
}

function fillBoard(u) {
  const w = winOf(u);
  if (u && !u.online) {
    w.body.innerHTML = `<div class="gone"><p>Not connected. Plug it in and it comes back by itself.</p>
      <p>Plugged in already? The browser does not always give this page its cable back: take the cable out
      and put it back, or pick its port.</p><p class="row"><button class="btn again find">FIND ITS PORT</button><button class="btn quiet again port">PORT</button></p>
      <dl class="facts">${facts(u).map(([k, v]) => `<dt>${k}</dt><dd>${esc(v)}</dd>`).join('')}</dl></div>`;
    w.q('.find').addEventListener('click', () => addBoard(u));
    w.q('.port').addEventListener('click', () => portWin(u));
    return;
  }
  const round = u && BOARDS[u.build].round;
  // people took the screen here for a picture of one: over it, in words that can't be missed, that it is
  // the board's own, now, and that a touch here reaches the board
  const onair = u ? '<div class="onair"><p><i></i><b>LIVE</b><span>your board\'s own screen, right now</span></p><small>Touch it here and the board moves too</small></div>'
    : '<div class="onair demo"><p><b>DEMO</b><span>a radar in this page, no board on it</span></p></div>';
  w.body.innerHTML = `<div class="bw"><div class="main">${onair}<div class="gl glass"></div>
    <div class="zoom"><button class="zo" aria-label="smaller">-</button><span class="zw"></span><button class="zi" aria-label="bigger">+</button>
    <button class="again settings">SETTINGS</button>${u ? '<button class="again port">PORT</button><button class="again flash">FLASH AGAIN</button>' : ''}</div>
    <div class="livenote">${liveNote(u)}</div>${u ? screenWords(u) : ''}</div>
    <div class="side" hidden><div class="tabs" role="tablist"><button role="tab" data-tab="setup">SETUP</button><button role="tab" data-tab="view">VIEW</button><button role="tab" data-tab="board">BOARD</button></div><div class="pane"></div></div></div>`;
  const gl = w.q('.gl');
  gl.classList.toggle('round', !!round);
  gl.classList.toggle('yellow', !!(u && BOARDS[u.build].yellow));
  gl.append((u ? u.screen : hero).canvas);
  w.body.querySelectorAll('[data-screen]').forEach((el) => {
    el.addEventListener('click', () => term.run(`screen ${el.dataset.screen} --board ${short(u)}`));
  });
  w.q('.onair i')?.addEventListener('animationend', (e) => {
    e.target.classList.remove('beat');                // back to its steady blink
  });
  w.q('.zo').addEventListener('click', () => zoomBy(u, -1));
  w.q('.zi').addEventListener('click', () => zoomBy(u, 1));
  w.q('.port')?.addEventListener('click', () => portWin(u));
  /* flashing is not a click to make by accident: the first one only asks */
  const flash = w.q('.flash');
  flash?.addEventListener('click', () => {
    if (flash.classList.contains('armed')) {
      clearTimeout(flash.calm);
      updateThen(u);
      return;
    }
    flash.classList.add('armed');
    flash.textContent = 'SURE? CLICK AGAIN';
    flash.calm = setTimeout(() => {
      flash.classList.remove('armed');
      flash.textContent = 'FLASH AGAIN';
    }, 4000);
  });
  w.q('.settings').addEventListener('click', () => {
    const side = w.q('.side');
    side.hidden = !side.hidden;
    w.tab = w.tab ?? (u && !u.wifi ? 'setup' : 'view');
    drawPane(u);
  });
  w.body.querySelectorAll('.tabs button').forEach((t) => {
    t.addEventListener('click', () => {
      w.tab = t.dataset.tab;
      drawPane(u);
    });
  });
  refit(u);
}

function boardWin(u) {
  const id = u ? u.mac : 'demo';
  return desk.get(id) ?? desk.make(id, u ? u.name : 'The radar, a demo sky', {
    onFront() {
      scene.mark(u?.online ? 'board' : null);
      if (app.cur !== (u ?? null)) {
        app.cur = u ?? null;
        app.view = u ? 'unit' : 'demo';
        renderBar();
      }
    },
    onClose: () => renderBar(),
    onResize: () => refit(u),
  });
}

/* top: a board just found or flashed. the page goes up to the picture, which shows that board now.
 * background: it came by itself, its cable back in. the connect window, open about something else (a
 * board being picked or flashed, a word about another port), is then left as it is, and the page does
 * not move. one about this board or its port goes */
async function showLive(u, { top = false, background = false } = {}) {
  if (u) {
    await u.ready;
  }
  const wiz = desk.get('connect');
  const midway = background && !!wiz && !wiz.root.hidden && app.troubleFor !== u && app.port !== u?.port;
  if (!midway) {
    wiz?.hide();
  }
  app.cur = u ?? null;
  app.view = u ? 'unit' : 'demo';
  const w = boardWin(u);
  w.show(u ? document.querySelector(`[data-u="${u.mac}"]`) : null);
  fillBoard(u);
  w.front();
  if (u?.online) {
    offerTip(w, u);
  }
  renderBar();
  desk.layout();
  if (midway) {
    return;
  }
  if (top) {
    scrollTo({ top: 0, behavior: 'smooth' });
  } else {
    w.root.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  }
}

/* the first board in this browser: a word that the screen here is the board's, for a little while */
function offerTip(w, u) {
  try {
    if (localStorage.getItem('owlos.tip')) {
      return;
    }
    localStorage.setItem('owlos.tip', '1');
  } catch (e) {
    console.warn(e);
  }
  const tip = document.createElement('div');
  tip.className = 'tip';
  tip.textContent = 'Control your board from here: touch this screen';
  w.q('.main').prepend(tip);
  const gone = () => tip.remove();
  setTimeout(gone, 20000);
  u.screen.canvas.addEventListener('pointerdown', gone, { once: true });
}

/* --- all your boards at once: every window open, and what goes for all of them --- */

async function showAll() {
  for (const u of app.units) {
    const w = desk.get(u.mac);
    if (!w || w.root.hidden) {
      await showLive(u);
    }
  }
  app.view = 'all';
  app.cur = null;
  const old = here().filter((u) => older(u.version, newest)).length;
  const w = desk.get('all') ?? desk.make('all', 'On every board', { onClose: () => renderBar() });
  w.body.innerHTML = `<div class="pane common"><div class="field"><span>Look</span><div class="chips">${LOOKS.map((l) => `<button class="chip" data-every="look=${l}">${l.toUpperCase()}</button>`).join('')}</div></div>
    <div class="field"><span>Colours</span><div class="chips">${THEMES.map((t) => `<button class="chip" data-every="theme=${t}">${t.toUpperCase()}</button>`).join('')}</div></div>
    ${old ? `<button class="btn quiet updateall">UPDATE ${old === 1 ? 'THE OLD ONE' : `ALL ${old}`} TO ${newest}</button>` : ''}</div>`;
  w.body.querySelectorAll('[data-every]').forEach((el) => {
    el.addEventListener('click', () => term.run(`set ${el.dataset.every} --all`));
  });
  w.q('.updateall')?.addEventListener('click', updateAll);
  w.show();
  renderBar();
}

/* --- what a click on the picture opens --- */

function describe(id) {
  const { kind, thing, count } = scene.about(id);
  if (kind === 'plane' && !thing) {
    return { title: 'Gone', what: 'It has left the sky here', facts: [], text: 'Planes come and go: this one flew out of range.' };
  }
  if (kind === 'plane' || kind === 'heli') {
    return {
      title: thing.name, what: thing.live ? `${thing.type}, in the air right now` : `${thing.type}, in the demo sky`,
      facts: [['Height', thing.feet], ['Speed', `${thing.knots} kt`], ['Course', thing.course], ['Squawk', thing.squawk]],
      text: kind === 'heli'
        ? 'A helicopter with ADS-B sends the same signal as a plane.'
        : 'It sends this by itself, about twice a second, on 1090 MHz. Nobody asks it, and anyone with an antenna can hear it.',
    };
  }
  if (kind === 'receiver') {
    return {
      title: 'A volunteer\'s antenna', what: 'On a roof, somewhere near you',
      facts: [['Heard here', `${fmt(thing.heard)} messages`], ['Reach', 'line of sight, often hundreds of km'], ['Made of', 'an antenna, a USB receiver, a small computer']],
      text: 'It passes on what it hears, and the network puts it together with all the other antennas. You can build one too.',
    };
  }
  if (id === 'server') {
    return {
      title: 'The network', what: 'adsb.lol, adsb.fi, or Flightradar24 with a key',
      facts: [['In', `${fmt(count.into)} from the antennas`], ['Out', `${fmt(count.out)} answers`]],
      text: 'IN: what every antenna heard. The network makes one picture of the sky out of it. OUT: only what someone asked for, the planes around one point.',
    };
  }
  if (id === 'router') {
    return {
      title: 'Your router', what: 'The one you already have',
      facts: [['Asks', 'every few seconds'], ['For', 'the planes around your home']],
      text: 'Nothing to set up on it. The board joins your WiFi the way a phone does.',
    };
  }
  if (id === 'home') {
    return {
      title: 'Your home', what: 'The middle of your radar',
      facts: [['Set on', 'the touch screen, or from this page']],
      text: 'Tell the board where you live, by a town, a postcode or a point, and it shows the planes around it.',
    };
  }
  if (id === 'board') {
    return {
      title: 'Your board', what: 'A small ESP32 with a touch screen',
      facts: [['Boards', `${Object.keys(BOARDS).length} to pick from`], ['Screens', '1.28" round to 5"'], ['Needs', 'a USB cable, once']],
      text: 'It draws the planes itself, on its own screen. Your computer can go to sleep.',
      action: 'serial' in navigator,
    };
  }
  return {
    title: 'Another board', what: 'At a neighbour\'s, or in your kitchen',
    facts: [],
    text: 'Every board asks for itself, so you can have as many as you like.',
  };
}

function infoBody(i) {
  return `<h3>${esc(i.title)}</h3><p class="what">${esc(i.what)}</p>
    ${i.facts.length ? `<dl class="facts" id="infofacts">${i.facts.map(([k, v]) => `<dt>${k}</dt><dd>${esc(v)}</dd>`).join('')}</dl>` : ''}
    <p>${esc(i.text)}</p>`;
}

let infoId = null;

function closeInfo() {
  infoId = null;
  $('info').hidden = true;
  scene.clear();
}

function showInfo(id, at) {
  $('hint').hidden = true;
  if (!id) {
    closeInfo();
    return;
  }
  infoId = id;
  const box = $('info');
  const i = describe(id);
  box.innerHTML = `<button class="x" aria-label="close">x</button>${infoBody(i)}${i.action ? '<button class="btn" id="infogo">CONNECT A DEVICE</button>' : ''}`;
  box.hidden = false;
  box.querySelector('.x').addEventListener('click', closeInfo);
  box.querySelector('#infogo')?.addEventListener('click', () => {
    closeInfo();
    addBoard();
  });
  const wrap = box.parentElement;
  const left = at.x + 330 > wrap.clientWidth ? at.x - 326 : at.x + 16;
  box.style.left = `${Math.max(8, left)}px`;
  box.style.top = `${Math.max(8, Math.min(at.y - 30, wrap.clientHeight - box.offsetHeight - 8))}px`;
}

setInterval(() => {
  const list = $('infofacts');
  if (infoId && list) {
    list.innerHTML = describe(infoId).facts.map(([k, v]) => `<dt>${k}</dt><dd>${esc(v)}</dd>`).join('');
  }
}, 500);

addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && infoId) {
    closeInfo();
  }
});

/* --- start --- */

function tickAll(now) {
  requestAnimationFrame(tickAll);
  if (hero.m && (scene.isShown() || (hero.canvas.isConnected && !desk.get('demo')?.root.hidden))) {
    hero.frame(now);
  }
  for (const u of app.units) {
    if (u.screen.m && ((u.screen.canvas.isConnected && !desk.get(u.mac)?.root.hidden) || (u === skyUnit && scene.isShown()))) {
      u.screen.frame(now);
    }
  }
}

/* a board back on its cable. the browser's connect event mostly tells, but once, a CH340 on a bad cable,
 * it did not come: the port was there, connected and closed. so the page looks for itself too. a board
 * that does not answer is left alone for a while: each try is 7 seconds of asking */
async function lookForReturns() {
  if (app.busy || app.settling) {
    return;
  }
  for (const u of app.units) {
    if (!u.online && u.port?.connected && !u.port.readable && Date.now() > (u.nextLook ?? 0)) {
      u.nextLook = Date.now() + 30000;
      try {
        const { unit } = await tryPort(u.port);
        if (unit) {
          say(`${unit.name} is back`, 'dim');
          showLive(unit, { top: true, background: true });
        }
      } catch (e) {
        console.warn(e);
        say(`${u.name}: ${e.message}`, 'page', u.port);
      }
    }
  }
}

/* the boards this browser knows come back as they were, and those on a cable it may use connect by
 * themselves, with no window: the browser remembers the ports it was allowed */
async function comeBack() {
  let kept = [];
  try {
    kept = JSON.parse(localStorage.getItem(KEPT) ?? '[]');
  } catch (e) {
    console.warn(e);
  }
  for (const k of kept) {
    if (BOARDS[k.build]) {
      addUnit(k);
    }
  }
  renderBar();
  if (!('serial' in navigator)) {
    return;
  }
  // first of all: a board plugged in while the others are being asked comes too
  navigator.serial.addEventListener('connect', async (e) => {
    await wait(800);
    try {
      const { unit } = await tryPort(e.target);
      if (unit) {
        showLive(unit, { top: true, background: true });
      }
    } catch (err) {
      console.warn(err);
    }
  });
  // all at once: one board that does not answer must not keep the others waiting 7 seconds each
  const ports = await navigator.serial.getPorts();
  await Promise.all(ports.map(async (port) => {
    try {
      await tryPort(port);
    } catch (e) {
      console.warn('a remembered port', e);
      if (e.message === LOW_POWER) {
        trouble('A board does not start', `${LOW_POWER}.`);
      } else if (e.notOpened) {
        trouble('A port of your boards did not open', `${e.message}.`);
      }
    }
  }));
  for (const u of here()) {
    await showLive(u, { top: true, background: true });
  }
  setInterval(lookForReturns, 3000);
}

$('try').addEventListener('click', () => {
  showLive(null);
});
if ('serial' in navigator) {
  $('connect').addEventListener('click', () => addBoard());
} else {
  $('connect').hidden = true;
  $('cablenote').textContent = 'Connecting a board needs Chrome or Edge on a computer. Here you can look around.';
}

scene = startScene($('scene'), () => (skyUnit?.screen.drawn ? skyUnit.screen.raw : hero.drawn ? hero.raw : null), showInfo);
loadRelief().then((fn) => {
  demoGround = fn;
  if (!skyUnit) {
    scene.setRelief(fn);
  }
}).catch((e) => console.warn(e));
fetch('version.json').then((r) => r.json()).then((v) => {
  newest = v.version;
  renderBar();
}).catch((e) => console.warn(e));
setInterval(showStates, 10000);                        // an answer that stopped coming shows by itself
startStats(BOARDS);                                    // the boards out there, when our server answers
setInterval(() => reportCable(here().length), 5 * 60000);   // how many are on this page's cable, when allowed
hero.start().then(() => {
  requestAnimationFrame(tickAll);
  return comeBack();
}).catch((e) => {
  console.warn('the radar did not start', e);
});
