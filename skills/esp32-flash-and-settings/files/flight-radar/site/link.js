// One board on one cable: the lines of firmware/main/console.h over Web Serial.

const within = (promise, ms) => Promise.race([promise, new Promise((r) => {
  setTimeout(r, ms);
})]);

/* the link on each port: a port left open by a try that failed must be let go, or it stays busy */
const links = new WeakMap();

/* what a serial line can say and still go on: the read breaks, a new reader carries on. a board
 * restarting is full of them, a CP210x reports them, a CH340 hardly ever */
const LINE_ERRORS = ['BreakError', 'BufferOverrunError', 'FramingError', 'ParityError'];

/* a write the cable has not taken in this long is dropped. a USB chip that stopped answering would
 * otherwise hold the port's writer for ever, and a port with a writer on it can never close again */
const WRITE_MS = 2000;

export class Link {
  constructor(port) {
    this.port = port;
    this.encoder = new TextEncoder();
    this.lines = [];
    this.writing = Promise.resolve();
    this.onLine = null;                                // every line, the log too
    this.onSent = null;                                // every line the page sends
    this.onLost = null;
    this.bytes = 0;                                    // all that came, lines or not
    this.attached = false;                             // a board of ours, sending its planes
    this.ended = false;                                // nothing more comes from the port
  }

  async open() {
    await links.get(this.port)?.close();
    links.set(this.port, this);
    try {
      await this.port.open({ baudRate: 115200, bufferSize: 65536 });
    } catch (e) {
      const why = new Error(`The port did not open (${e.message}). Another tab or program may have it: the previous page, a serial monitor. Or its USB chip stopped answering: take the cable out and put it back`);
      why.notOpened = true;
      throw why;
    }
    this.opened = true;
    // the browser opens a port with DTR and RTS both on. RTS goes off first, then DTR: on the usual
    // two-transistor circuit EN is never pulled low that way, and on the chip's own USB the line state
    // never passes through "reset". so opening does not restart the board. DTR first did, on every
    // open, and on the chip's own USB under Windows, whose driver may not send an RTS change alone
    // (esptool works round it), it could hold the chip in reset: a dark screen until the cable came out
    await within(this.port.setSignals({ requestToSend: false }), 1000);
    await within(this.port.setSignals({ dataTerminalReady: false }), 1000);
    this.pumping = this.pump();
  }

  async pump() {
    const decoder = new TextDecoder();
    let text = '';
    while (this.port.readable && !this.closing) {
      this.reader = this.port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) {
            this.closing = true;
            break;
          }
          this.bytes += value.length;
          text += decoder.decode(value, { stream: true });
          let i;
          while ((i = text.indexOf('\n')) >= 0) {
            const line = text.slice(0, i).replace(/\r$/, '').replace(/\x1b\[[0-9;]*m/g, '');
            text = text.slice(i + 1);
            try {
              this.take(line);
            } catch (e) {
              console.warn('a line the page could not take', e);   // the page's fault, not the cable's
            }
          }
        }
      } catch (e) {
        if (LINE_ERRORS.includes(e.name)) {
          this.take(`(${e.name} on the line)`);
        } else {
          console.warn('the port went away', e);
          this.closing = true;
        }
      } finally {
        this.reader.releaseLock();
      }
    }
    this.ended = true;
    clearInterval(this.keep);                          // nothing more comes: asking for the planes is over
    this.onLost?.();
  }

  take(line) {
    if (line.startsWith('OWLOS ') && !line.startsWith('OWLOS PLANE') && !line.startsWith('OWLOS SKY')) {
      this.lines.push(line);
      if (this.lines.length > 64) {
        this.lines.shift();
      }
    }
    this.onLine?.(line);
  }

  // a newline first: bytes garbled while the board restarts would otherwise glue onto the command.
  // one at a time: the port has one writer. a write that failed fails alone, the next ones still go
  send(line) {
    this.writing = this.writing.catch(() => {}).then(async () => {
      const writable = this.port.writable;
      if (!writable) {
        throw new Error('the port is closed');         // and nothing went
      }
      const writer = writable.getWriter();
      this.onSent?.(line);
      try {
        const went = await within(writer.write(this.encoder.encode(`\n${line}\n`)).then(() => true), WRITE_MS);
        if (!went) {
          await within(writer.abort().catch(() => {}), 1000);
          throw new Error(`the cable took nothing for ${WRITE_MS / 1000} s`);
        }
      } finally {
        writer.releaseLock();
      }
    });
    return this.writing;
  }

  async expect(prefix, ms) {
    const until = Date.now() + ms;
    while (Date.now() < until) {
      const i = this.lines.findIndex((l) => l.startsWith(prefix));
      if (i >= 0) {
        return this.lines.splice(i, 1)[0];
      }
      await new Promise((r) => setTimeout(r, 60));
    }
    return null;
  }

  /* who is on the cable: { version, build, mac }, build and mac only from 0.8 on. null: no Flight Radar */
  async hello(tries = 8) {
    for (let t = 0; t < tries; t++) {
      await this.send('OWLOS HELLO');
      const line = await this.expect('OWLOS READY', 800);
      if (line) {
        const [, , what, version, build, mac] = line.split(' ');
        return what === 'flight-radar' ? { version, build, mac } : null;
      }
    }
    return null;
  }

  async config() {
    await this.send('OWLOS GET');
    const line = await this.expect('OWLOS CFG ', 3000);
    return line ? JSON.parse(line.slice(10)) : null;
  }

  async close() {
    this.onLost = null;
    this.onLine = null;
    clearInterval(this.keep);
    if (links.get(this.port) === this) {              // a newer link on this port stays known
      links.delete(this.port);
    }
    if (!this.opened) {
      return;
    }
    this.opened = false;
    if (this.attached) {
      this.send('OWLOS SKY OFF').catch((e) => console.warn(e));   // the board stops sending its planes
    }
    await this.writing.catch(() => {});                // every write done or dropped: none holds the port
    this.closing = true;
    try {
      await within(this.reader?.cancel(), 1000);
    } catch (e) {
      console.warn(e);                                 // it broke already: the port still has to close
    }
    await within(this.pumping, 1000);
    try {
      await within(this.port.close(), 2000);
    } catch (e) {
      console.warn(e);
    }
  }
}
