// Windows on the page: dragged by the title, on top when touched, with the usual three buttons.
// They start like tiles under the picture, in the middle of the page, and the page grows down for
// them. One that was moved stays where it was put until ARRANGE or the next visit. On a phone they
// simply follow each other, as a page does.

const GAP = 14;
const SNAP = 10;                                       // this close to an edge, a window lies against it
const ROW = 1180;                                      // how wide the tiles may lie

const ICON = {
  min: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M1.5 9.5h9" /></svg>',
  max: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M3.5 1.5h7v7M1.5 3.5h7v7h-7z" /></svg>',
  back: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M1.5 4.5h6v6h-6zM4.5 4.5v-3h6v6h-3" /></svg>',
  close: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 2l8 8M10 2l-8 8" /></svg>',
};

class Win {
  /* dock: a place of its own on the page instead of a tile on the desk. it is not dragged */
  constructor(desk, id, title, { onFront = () => {}, onClose = () => {}, onResize = () => {}, className = '', dock = null }) {
    this.desk = desk;
    this.id = id;
    this.docked = !!dock;
    this.onFront = onFront;
    this.onClose = onClose;
    this.onResize = onResize;
    this.root = document.createElement('section');
    this.root.className = `win ${className}`;
    this.root.hidden = true;
    this.root.innerHTML = `<i class="winglow"></i><i class="winmark"></i><header class="winbar"><i class="windot"></i><span class="wintitle"></span><span class="winstate"></span>
      <button class="winbtn" data-act="min" title="Minimize" aria-label="minimize">${ICON.min}</button>
      <button class="winbtn" data-act="max" title="Maximize" aria-label="maximize">${ICON.max}</button>
      <button class="winbtn close" data-act="close" title="Close" aria-label="close">${ICON.close}</button></header><div class="winbody"></div>`;
    this.bar = this.root.querySelector('.winbar');
    this.body = this.root.querySelector('.winbody');
    this.setTitle(title);
    this.at = null;                                    // { x, y } once it was moved
    this.listen();
    (dock ?? desk.el).append(this.root);
    new ResizeObserver(() => desk.layout()).observe(this.root);
  }

  q(selector) {
    return this.body.querySelector(selector);
  }

  setTitle(text) {
    this.root.querySelector('.wintitle').textContent = text;
  }

  /* what the title says of the thing inside, and the colour of the frame: good, warn, bad, or none */
  setState(tone, text) {
    this.root.querySelector('.winstate').textContent = text;
    for (const el of [this.root, this.mintab]) {
      el?.classList.remove('good', 'warn', 'bad');
      if (tone) {
        el?.classList.add(tone);
      }
    }
  }

  /* down to a small tab in the corner, still alive: a click on the tab brings it back */
  minimize() {
    if (this.maxed) {
      this.toggleMax();
    }
    const tab = document.createElement('button');
    tab.className = `mintab ${['good', 'warn', 'bad'].filter((c) => this.root.classList.contains(c)).join(' ')}`;
    tab.innerHTML = '<i class="windot"></i><span></span>';
    tab.querySelector('span').textContent = this.root.querySelector('.wintitle').textContent;
    tab.title = 'Bring it back';
    tab.addEventListener('click', () => {
      this.show(tab);
      this.front();
    });
    this.desk.tray.append(tab);
    this.mintab = tab;
    const r = this.root.getBoundingClientRect();
    const f = tab.getBoundingClientRect();
    const dx = f.left + f.width / 2 - (r.left + r.width / 2);
    const dy = f.top + f.height / 2 - (r.top + r.height / 2);
    const away = matchMedia('(prefers-reduced-motion: reduce)').matches ? null
      : this.root.animate([{ transform: 'none', opacity: 1 }, { transform: `translate(${dx}px, ${dy}px) scale(.15)`, opacity: 0 }], { duration: 190, easing: 'ease-in' });
    const done = () => {
      this.root.hidden = true;
      this.desk.layout();
      this.onClose('min');
    };
    if (away) {
      away.onfinish = done;
    } else {
      done();
    }
  }

  listen() {
    this.root.addEventListener('pointerdown', () => this.front());
    this.bar.addEventListener('click', (e) => {
      const act = e.target.closest('[data-act]')?.dataset.act;
      if (act === 'max') {
        this.toggleMax();
      } else if (act === 'min') {
        this.minimize();
      } else if (act) {
        this.hide();
        this.onClose(act);
      }
    });
    this.bar.addEventListener('dblclick', (e) => {
      if (!e.target.closest('[data-act]')) {
        this.toggleMax();
      }
    });
    this.bar.addEventListener('pointerdown', (e) => {
      if (e.target.closest('[data-act]') || this.maxed || this.docked || this.desk.flat()) {
        return;
      }
      this.desk.hold();
      const startX = e.clientX - this.root.offsetLeft;
      const startY = e.clientY - this.root.offsetTop;
      this.bar.setPointerCapture(e.pointerId);
      const move = (m) => {
        this.at = this.desk.snapped(this, m.clientX - startX, m.clientY - startY);
        this.place();
        this.desk.layout();
      };
      const up = () => {
        this.bar.removeEventListener('pointermove', move);
        this.bar.removeEventListener('pointerup', up);
        this.bar.removeEventListener('pointercancel', up);
      };
      this.bar.addEventListener('pointermove', move);
      this.bar.addEventListener('pointerup', up);
      this.bar.addEventListener('pointercancel', up);
    });
  }

  place() {
    if (this.at && !this.desk.flat()) {
      const top = -this.desk.el.getBoundingClientRect().top - scrollY;
      this.root.style.left = `${Math.max(-this.root.offsetWidth + 90, Math.min(this.at.x, this.desk.el.clientWidth - 90))}px`;
      this.root.style.top = `${Math.max(top, this.at.y)}px`;
    }
  }

  /* from: an element it grows out of, the button that called it */
  show(from = null) {
    const opening = this.root.hidden;
    from = from ?? this.mintab;
    this.tabPlace = this.mintab?.getBoundingClientRect();
    this.mintab?.remove();
    this.mintab = null;
    this.root.hidden = false;
    this.desk.layout();
    if (opening && !matchMedia('(prefers-reduced-motion: reduce)').matches) {
      const r = this.root.getBoundingClientRect();
      const f = from?.isConnected === false ? this.tabPlace : from?.getBoundingClientRect();
      const dx = f ? f.left + f.width / 2 - (r.left + r.width / 2) : 0;
      const dy = f ? f.top + f.height / 2 - (r.top + r.height / 2) : -14;
      this.root.animate([{ transform: `translate(${dx}px, ${dy}px) scale(${f ? 0.2 : 0.97})`, opacity: 0 }, { transform: 'none', opacity: 1 }], { duration: 170, easing: 'ease-out' });
    }
    return this;
  }

  hide() {
    this.root.hidden = true;
    if (this.maxed) {
      this.toggleMax();
    }
    this.desk.layout();
  }

  front() {
    this.root.style.zIndex = String(++this.desk.z);
    for (const w of this.desk.wins.values()) {
      w.root.classList.toggle('active', w === this);
    }
    this.onFront();
  }

  toggleMax() {
    this.maxed = !this.maxed;
    this.root.classList.toggle('maxed', this.maxed);
    this.root.querySelector('[data-act=max]').innerHTML = this.maxed ? ICON.back : ICON.max;
    this.onResize();
    this.desk.layout();
  }
}

export class Desk {
  constructor(el) {
    this.el = el;
    this.z = 10;
    this.wins = new Map();
    this.tray = document.createElement('div');         // the minimized ones, in the corner
    this.tray.id = 'tray';
    document.body.append(this.tray);
    addEventListener('resize', () => this.layout());
  }

  flat() {
    return innerWidth < 720;
  }

  get(id) {
    return this.wins.get(id);
  }

  make(id, title, options = {}) {
    const w = new Win(this, id, title, options);
    this.wins.set(id, w);
    return w;
  }

  remove(id) {
    this.wins.get(id)?.root.remove();
    this.wins.delete(id);
    this.layout();
  }

  /* one is being moved by hand: the others stay exactly where they are, no tile jumps away */
  hold() {
    for (const w of this.wins.values()) {
      if (!w.root.hidden && !w.maxed && !w.docked && !w.at) {
        w.at = { x: w.root.offsetLeft, y: w.root.offsetTop };
      }
    }
  }

  /* where a dragged window comes to rest: against the page's edges and the others, when it is near */
  snapped(win, x, y) {
    const w = win.root.offsetWidth;
    const h = win.root.offsetHeight;
    const xs = [[0, 0], [this.el.clientWidth, -w]];
    const ys = [[0, 0]];
    for (const o of this.wins.values()) {
      if (o !== win && !o.root.hidden && !o.maxed && !o.docked) {
        const r = o.root;
        xs.push([r.offsetLeft, 0], [r.offsetLeft, -w - GAP], [r.offsetLeft + r.offsetWidth, -w], [r.offsetLeft + r.offsetWidth + GAP, 0]);
        ys.push([r.offsetTop, 0], [r.offsetTop, -h - GAP], [r.offsetTop + r.offsetHeight, -h], [r.offsetTop + r.offsetHeight + GAP, 0]);
      }
    }
    const near = (v, list) => {
      for (const [edge, shift] of list) {
        if (Math.abs(v - (edge + shift)) < SNAP) {
          return edge + shift;
        }
      }
      return v;
    };
    return { x: near(x, xs), y: near(y, ys) };
  }

  /* the highest free place, then the leftmost, for a window of this size, among those already lying */
  spot(w, h, lying) {
    const width = Math.min(ROW, this.el.clientWidth);
    const xs = [0, ...lying.map((r) => r.x + r.w + GAP)].filter((x) => x === 0 || x + w <= width).sort((a, b) => a - b);
    const ys = [0, ...lying.map((r) => r.y + r.h + GAP)].sort((a, b) => a - b);
    for (const y of ys) {
      for (const x of xs) {
        const free = lying.every((r) => x + w + GAP <= r.x || r.x + r.w + GAP <= x || y + h + GAP <= r.y || r.y + r.h + GAP <= y);
        if (free) {
          return { x, y };
        }
      }
    }
    return { x: 0, y: Math.max(0, ...lying.map((r) => r.y + r.h + GAP)) };
  }

  /* those never moved are laid like tiles round those that were. the page reaches past the lowest one */
  layout() {
    if (this.flat()) {
      this.el.style.height = '';
      return;
    }
    const shown = [...this.wins.values()].filter((w) => !w.root.hidden && !w.maxed && !w.docked);
    const tiles = [];
    for (const w of shown.filter((v) => !v.at)) {
      const r = w.root;
      const { x, y } = this.spot(r.offsetWidth, r.offsetHeight, tiles);
      tiles.push({ r, x, y, w: r.offsetWidth, h: r.offsetHeight });
    }
    // the tiles as one block in the middle of the page
    const used = Math.max(0, ...tiles.map((t) => t.x + t.w));
    const left = Math.max(0, (this.el.clientWidth - used) / 2);
    for (const t of tiles) {
      t.r.style.left = `${left + t.x}px`;
      t.r.style.top = `${t.y}px`;
    }
    let bottom = Math.max(0, ...tiles.map((t) => t.y + t.h));
    for (const w of shown.filter((v) => v.at)) {
      w.place();
      bottom = Math.max(bottom, w.root.offsetTop + w.root.offsetHeight);
    }
    this.el.style.height = `${bottom + GAP}px`;
  }

  /* everything back in its tiles */
  arrange() {
    for (const w of this.wins.values()) {
      w.at = null;
      if (w.maxed) {
        w.toggleMax();
      }
    }
    this.layout();
  }
}
