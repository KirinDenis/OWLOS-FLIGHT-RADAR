// a test only, never on the site: boards behind a pretend navigator.serial, speaking the console's lines.
// assemble.sh --test puts it in _public/test.html
(() => {
  const BOARDS = [
    { mac: 'aa0000000002', build: 'ili9341', theme: 2 },
    { mac: 'aa0000000001', build: 'st7796', theme: 0 },
    { mac: 'aa0000000003', build: 'gc9a01', theme: 1 },
    { mac: 'aa0000000004', build: 'ili9486', theme: 0 },
  ];
  class FakePort extends EventTarget {
    constructor(b) {
      super();
      this.b = b;
      this.text = '';
      this.connected = true;
    }

    getInfo() {
      return { usbVendorId: 0x1a86 };
    }

    async open() {
      if (this.readable) {
        throw new DOMException('The port is already open.', 'InvalidStateError');
      }
      this.opens = (this.opens ?? 0) + 1;               // a test counts them: one try, one open
      this.fresh();
      this.writable = new WritableStream({
        // stuck: a USB chip that takes nothing, its writes end only when aborted
        write: (chunk, c) => (this.b.stuck ? new Promise((resolve, reject) => {
          c.signal.addEventListener('abort', () => reject(c.signal.reason));
        }) : this.heard(new TextDecoder().decode(chunk))),
      });
      if (this.b.build === 'ili9486') {                // a CP210x says so when a board restarts: the read breaks
        setTimeout(() => this.noise(), 30);
      }
    }

    fresh() {
      this.readable = new ReadableStream({
        start: (c) => {
          this.out = c;
        },
      });
    }

    noise() {
      const broken = this.out;
      this.fresh();
      broken?.error(new DOMException('A framing error', 'FramingError'));
    }

    async setSignals() {
    }

    /* the cable out, and in again: a NetworkError for the reader, and here no connect event */
    unplug() {
      this.connected = false;
      clearInterval(this.sky);
      const broken = this.out;
      this.out = null;
      this.readable = null;
      this.writable = null;
      broken?.error(new DOMException('The device has been lost.', 'NetworkError'));
    }

    plug() {
      this.connected = true;
    }

    async close() {
      if (this.readable?.locked || this.writable?.locked) {   // as the browser's: a stream in use keeps the port open
        throw new TypeError('Cannot close: a stream is locked');
      }
      clearInterval(this.sky);
      this.out = null;
      this.readable = null;
      this.writable = null;
    }

    line(text) {
      this.out?.enqueue(new TextEncoder().encode(`${text}\r\n`));
    }

    /* mute: a cable that carries nothing back, as a CH340 sometimes does until it is plugged in again */
    heard(text) {
      if (this.b.mute) {
        return;
      }
      this.text += text;
      let i;
      while ((i = this.text.indexOf('\n')) >= 0) {
        const l = this.text.slice(0, i).trim();
        this.text = this.text.slice(i + 1);
        if (l === 'OWLOS HELLO') {
          this.line(`OWLOS READY flight-radar 0.8 ${this.b.build} ${this.b.mac}`);
        } else if (l === 'OWLOS GET') {
          this.line(`OWLOS CFG ${JSON.stringify({
            ssid: 'test-net', nets: ['test-net'], lat: 50.9, lon: 4.48, place: 'Testville Sčín', country: 'XX', up: 0, theme: this.b.theme, look: 0, map: 1, ground: 1, range: 50, dist_unit: 0,
            alt_unit: 0, labels: 1, vectors: 0, beam_step: 0, clock: 0, block: 0, autopilot: 0, lost: 0, conflict: 0, fl_lo: 0, fl_hi: 0, version: '0.8', screen: this.b.screen ?? 1, flip: this.b.flip ?? 0,
          })}`);
        } else if (l === 'OWLOS SKIP') {
          this.line('OWLOS ALIGN 0');
        } else if (l === 'OWLOS SKY ON') {
          clearInterval(this.sky);
          this.line(`OWLOS ALIGN ${this.b.build === 'gc9a01' ? 1 : 0}`);
          const answer = () => {
            this.line('OWLOS SKY BEGIN TEST');
            for (let k = 0; k < 3; k++) {
              this.line(`OWLOS PLANE {"hex":"abc00${k}","flight":"TST${k}","lat":${50.9 + k * 0.1},"lon":${4.48 + k * 0.1},"alt_baro":${20000 + k * 5000},"gs":400,"track":${k * 90}}`);
            }
            this.line('OWLOS SKY END 3');
          };
          answer();
          this.sky = setInterval(answer, 15000);
        } else if (l.startsWith('OWLOS SET ')) {
          let set;
          try {
            set = JSON.parse(l.slice(10));
          } catch (e) {
            continue;                                  // a line typed wrong in a PORT window: the board lets it pass
          }
          if (set.screen !== undefined) {
            this.b.screen = set.screen;
          }
          if (set.flip !== undefined) {
            this.b.flip = set.flip;
          }
          this.line('OWLOS OK');
        }
      }
    }
  }
  const ports = BOARDS.map((b) => new FakePort(b));
  // test.html?allow=0,1: the ports this browser was allowed before, given back at the start as getPorts does
  for (const i of (new URLSearchParams(location.search).get('allow') ?? '').split(',').filter(Boolean)) {
    ports[Number(i)].allowed = true;
  }
  let given = 0;
  const fake = new EventTarget();
  fake.requestPort = async () => {
    const port = ports[given++ % ports.length];
    port.allowed = true;
    return port;
  };
  fake.getPorts = async () => ports.filter((p) => p.allowed && p.connected);
  Object.defineProperty(navigator, 'serial', { value: fake, configurable: true });
  window.fakePorts = ports;                            // a test reaches its boards: fakePorts[0].b.mute = true
  window.fakeNext = (i) => {                            // and picks the port the next list gives
    given = i;
  };
  /* the cable out, or in again: the browser's disconnect and connect events, their target the port */
  const tell = (name, port) => {
    const e = new Event(name);
    Object.defineProperty(e, 'target', { value: port });
    fake.dispatchEvent(e);
  };
  window.fakeUnplug = (i) => {
    ports[i].unplug();
    tell('disconnect', ports[i]);
  };
  window.fakePlug = (i) => {
    ports[i].plug();
    tell('connect', ports[i]);
  };
})();
