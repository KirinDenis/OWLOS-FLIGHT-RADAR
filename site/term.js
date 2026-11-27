// The console on the page: the shell of ./console/ (a copy of WIRE CITY's) in plain text on black.
// Ctrl+` opens and closes it. Tab finishes a word, Up and Down bring back what was typed, Ctrl+L
// clears, Ctrl+C drops the line or stops what runs. The history stays in this browser.

const SGR = {
  30: '#000', 31: '#cc4444', 32: '#44cc66', 33: '#cca633', 34: '#4477cc', 35: '#aa55cc', 36: '#33bbcc', 37: '#cfe7f7',
  90: '#7f9bb5', 91: '#ff6070', 92: '#7dffa8', 93: '#ffb848', 94: '#6aa8ff', 95: '#e08cff', 96: '#5ce1ff', 97: '#ffffff',
};
const HISTORY = 'owlos.history';
const KEEP_LINES = 2000;

export class Terminal {
  constructor({ makeShell, onToggle = () => {} }) {
    this.head = document.getElementById('termhead');
    this.body = document.getElementById('termbody');
    this.log = document.getElementById('termlog');
    this.promptBox = document.getElementById('termprompt');
    this.input = document.getElementById('termin');

    this.onToggle = onToggle;
    this.colour = null;
    this.bold = false;
    this.row = null;
    this.index = 0;
    this.typed = '';
    this.asking = null;
    this.tabs = 0;
    this.busy = Promise.resolve();
    this.shell = makeShell({ write: (t) => this.write(t), ask: (q, o) => this.ask(q, o) });
    this.shell.exit = () => this.toggle(false);
    this.shell.saveHistory = () => this.saveHistory();
    try {
      this.shell.history = JSON.parse(localStorage.getItem(HISTORY) ?? '[]').slice(-500);
    } catch (e) {
      this.shell.history = [];
    }
    this.index = this.shell.history.length;
    this.listen();
    this.refresh();
  }

  listen() {
    this.head.addEventListener('click', () => this.toggle(this.body.hidden));
    this.body.addEventListener('click', () => {
      if (!getSelection().toString()) {
        this.input.focus({ preventScroll: true });
      }
    });
    addEventListener('keydown', (e) => {
      if (e.ctrlKey && e.code === 'Backquote') {
        e.preventDefault();
        this.toggle(this.body.hidden);
      }
    });
    this.input.addEventListener('keydown', (e) => this.key(e));
  }

  fit() {
    this.shell.width = Math.floor(this.body.clientWidth / 8.4);
    this.body.scrollTop = this.body.scrollHeight;
  }

  toggle(open) {
    this.body.hidden = !open;
    this.head.setAttribute('aria-pressed', String(open));
    this.onToggle(open);
    if (open) {
      if (!this.greeted) {
        this.greeted = true;
        this.write('\x1b[97mOWLOS Flight Radar\x1b[0m \x1b[90mhelp lists the commands, man NAME says more of one\x1b[0m\r\n');
      }
      this.shell.width = Math.floor(this.body.clientWidth / 8.4);
      this.body.scrollTop = this.body.scrollHeight;
      this.input.focus({ preventScroll: true });
    }
  }

  /* the shell's text: colours and line ends of a real terminal */
  write(text) {
    for (const part of text.split(/(\x1b\[[0-9;]*[A-Za-z])/)) {
      const code = /^\x1b\[([0-9;]*)([A-Za-z])$/.exec(part);
      if (code) {
        this.escape(code[1], code[2]);
      } else if (part) {
        this.plain(part);
      }
    }
    this.body.scrollTop = this.body.scrollHeight;
  }

  escape(numbers, letter) {
    if (letter === 'J') {
      this.log.textContent = '';
      this.row = null;
    } else if (letter === 'm') {
      for (const n of (numbers || '0').split(';').map(Number)) {
        if (n === 0) {
          this.colour = null;
          this.bold = false;
        } else if (n === 1) {
          this.bold = true;
        } else if (SGR[n]) {
          this.colour = SGR[n];
        }
      }
    }
  }

  plain(text) {
    const lines = text.replace(/\r/g, '').split('\n');
    lines.forEach((line, i) => {
      if (i > 0) {
        this.row = null;
      }
      if (line) {
        this.span(this.rowNow(), line);
      } else if (i < lines.length - 1) {
        this.rowNow();
      }
    });
    while (this.log.childElementCount > KEEP_LINES) {
      this.log.firstElementChild.remove();
    }
  }

  rowNow() {
    if (!this.row) {
      this.row = document.createElement('div');
      this.log.append(this.row);
    }
    return this.row;
  }

  span(into, text) {
    const s = document.createElement('span');
    s.textContent = text;
    if (this.colour) {
      s.style.color = this.colour;
    }
    if (this.bold) {
      s.style.fontWeight = '700';
    }
    into.append(s);
  }

  /* the prompt again, as the shell says it now */
  async refresh() {
    const text = await this.shell.prompt();
    this.promptText = text;
    if (this.asking) {
      return;
    }
    this.promptBox.textContent = '';
    for (const part of text.split(/(\x1b\[[0-9;]*[A-Za-z])/)) {
      const code = /^\x1b\[([0-9;]*)([A-Za-z])$/.exec(part);
      if (code) {
        this.escape(code[1], code[2]);
      } else if (part) {
        this.span(this.promptBox, part);
      }
    }
    this.colour = null;
    this.bold = false;
  }

  /* a line that a button ran, shown as if typed */
  echo(line) {
    this.write(`${this.promptText ?? ''}${line}\r\n`);
  }

  /* one line through the shell. buttons come here too: one at a time, in order */
  run(line) {
    this.busy = this.busy.then(async () => {
      this.echo(line);
      this.stop = new AbortController();
      try {
        await this.shell.run(line, { signal: this.stop.signal });
      } catch (e) {
        this.write(`\x1b[91m${e.message}\x1b[0m\r\n`);
      }
      this.stop = null;
      await this.refresh();
    });
    return this.busy;
  }

  ask(question, { secret = false } = {}) {
    this.toggle(true);
    this.promptBox.textContent = question;
    this.input.type = secret ? 'password' : 'text';
    return new Promise((done, fail) => {
      this.asking = (answer) => {
        this.asking = null;
        this.cancelAsk = null;
        this.input.type = 'text';
        this.write(`${question}${secret ? '' : answer}\r\n`);
        this.refresh();
        done(answer);
      };
      /* Escape or Ctrl+C: the question goes, the command with it. without this it stayed: every button that runs a
       * line waited behind it, and the next line typed, a command too, went to the board as the password (the
       * 2026-11-27 review) */
      this.cancelAsk = () => {
        this.asking = null;
        this.cancelAsk = null;
        this.input.type = 'text';
        this.write(`${question}^C\r\n`);
        this.refresh();
        fail(new Error('not asked any more'));
      };
    });
  }

  saveHistory() {
    try {
      localStorage.setItem(HISTORY, JSON.stringify(this.shell.history.slice(-500)));
    } catch (e) {
      console.warn(e);
    }
  }

  async key(e) {
    const input = this.input;
    if (e.key !== 'Tab') {
      this.tabs = 0;
    }
    if (e.key === 'Enter') {
      const line = input.value;
      input.value = '';
      if (this.asking) {
        this.asking(line);
        return;
      }
      if (line.trim()) {
        if (!line.startsWith(' ')) {
          this.shell.history.push(line);
          this.saveHistory();
        }
        this.index = this.shell.history.length;
        this.run(line.trim());
      } else {
        this.echo('');
      }
    } else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
      e.preventDefault();
      const h = this.shell.history;
      if (this.index === h.length) {
        this.typed = input.value;
      }
      this.index = Math.max(0, Math.min(h.length, this.index + (e.key === 'ArrowUp' ? -1 : 1)));
      input.value = this.index === h.length ? this.typed : h[this.index];
    } else if (e.key === 'Tab') {
      e.preventDefault();
      await this.finish();
    } else if (e.ctrlKey && e.key.toLowerCase() === 'l') {
      e.preventDefault();
      this.write('\x1b[2J');
    } else if (e.ctrlKey && e.key.toLowerCase() === 'c' && !input.selectionEnd !== input.selectionStart) {
      if (input.selectionStart === input.selectionEnd) {
        e.preventDefault();
        if (this.cancelAsk) {                          // a password typed so far is not echoed
          input.value = '';
          this.cancelAsk();
          return;
        }
        this.stop?.abort();
        this.echo(`${input.value}^C`);
        input.value = '';
      }
    } else if (e.key === 'Escape') {
      if (this.cancelAsk) {
        input.value = '';
        this.cancelAsk();
        return;
      }
      this.toggle(false);
    }
  }

  async finish() {
    const input = this.input;
    const { start, end, candidates } = await this.shell.complete(input.value, input.selectionStart);
    if (!candidates.length) {
      return;
    }
    let common = candidates[0];
    for (const c of candidates) {
      while (!c.startsWith(common)) {
        common = common.slice(0, -1);
      }
    }
    const one = candidates.length === 1;
    const word = common + (one && !common.endsWith('/') ? ' ' : '');
    input.value = input.value.slice(0, start) + word + input.value.slice(end);
    input.selectionStart = input.selectionEnd = start + word.length;
    this.tabs++;
    if (!one && this.tabs > 1) {
      this.echo(input.value);
      this.write(`${candidates.join('  ')}\r\n`);
    }
  }
}
