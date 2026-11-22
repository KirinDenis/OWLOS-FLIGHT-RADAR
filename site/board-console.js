// What the console has for boards: the folders /boards/NAME and the commands. The shell itself, its
// grammar, help and man, and ls, cat, grep and the like are ./console/, a copy of WIRE CITY's.

/* /boards, a folder for each board, a few files in each. `api` is the page's */
export class BoardPaths {
  constructor(shell, api) {
    this.shell = shell;
    this.api = api;
  }

  resolve(word = '.') {
    const abs = word.startsWith('/') ? word : `${this.shell.cwd}/${word}`;
    const parts = [];
    for (const p of abs.split('/')) {
      if (p === '..') {
        parts.pop();
      } else if (p && p !== '.') {
        parts.push(p);
      }
    }
    return `/${parts.join('/')}`;
  }

  where(path) {
    const [top, name, file, more] = path.split('/').filter(Boolean);
    if (!top) {
      return { root: true };
    }
    if (top !== 'boards' || more) {
      return {};
    }
    if (!name) {
      return { boards: true };
    }
    const unit = this.api.byName(name);
    return unit ? { unit, file } : {};
  }

  async list(path) {
    const at = this.where(path);
    if (at.root) {
      return [{ name: 'boards', dir: true, info: 'your boards' }];
    }
    if (at.boards) {
      return this.api.units().map((u) => ({ name: this.api.short(u), dir: true, info: `${this.api.kind(u)}${u.online ? '' : ', not connected'}` }));
    }
    if (at.unit && !at.file) {
      return Object.keys(this.api.files).map((name) => ({ name, dir: false, info: name === 'config' ? 'its settings: cat it, or write it' : '' }));
    }
    throw new Error(`${path}: no such folder`);
  }

  async read(path) {
    const at = this.where(path);
    const file = at.unit && this.api.files[at.file];
    if (!file) {
      throw new Error(`${path}: no such file`);
    }
    return file(at.unit).join('\n');
  }

  async isDir(path) {
    const at = this.where(path);
    return !!(at.root || at.boards || (at.unit && !at.file));
  }

  /* a board's config is written as it reads: each line NAME=VALUE goes to the board as set sends it, there and
   * then. so `cat /boards/a/config > /boards/b/config` gives b what a has, `echo theme=amber > config` one
   * setting. a line the board does not take (a light that does not dim, a word set does not know) is said and
   * left, the rest are set. the other files only show what a board says */
  async write(path, text) {
    const at = this.where(path);
    if (!at.unit || at.file !== 'config') {
      throw new Error(`${path}: only a board's config is written, its other files show what it says`);
    }
    const lines = String(text).split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#'));
    const left = lines.filter((l) => {
      const eq = l.indexOf('=');
      return eq <= 0 || !this.api.set(at.unit, l.slice(0, eq).trim(), l.slice(eq + 1).trim());
    });
    if (left.length === lines.length) {
      throw new Error(`${path}: nothing set, a line is NAME=VALUE as cat shows them`);
    }
    if (left.length) {
      this.shell.write(`\x1b[2m${this.api.short(at.unit)}: ${lines.length - left.length} set, not taken: ${left.join(' ')}\x1b[0m\r\n`);
    }
  }

  async complete(word) {
    const cut = word.lastIndexOf('/');
    const head = cut >= 0 ? word.slice(0, cut + 1) : '';
    const tail = word.slice(cut + 1);
    const entries = await this.list(this.resolve(head || '.')).catch(() => []);
    return entries.filter((e) => e.name.startsWith(tail)).map((e) => `${head}${e.name}${e.dir ? '/' : ''}`);
  }

  async names() {
    return [];
  }
}

export const BOARD_GROUPS = [['boards', 'Your boards']];

export function boardCommands(api) {
  const names = (word) => [...api.units().map(api.short), 'all'].filter((n) => n.startsWith(word));
  /* which boards a command means: --all, --board NAME, or the one on show */
  const chosen = (ctx) => {
    if (ctx.opts.all) {
      return api.units().filter((u) => u.online);
    }
    if (ctx.opts.board) {
      const u = api.byName(ctx.opts.board);
      if (!u) {
        throw new Error(`${ctx.opts.board}: no such board, boards lists them`);
      }
      return [u];
    }
    const u = api.current();
    if (!u) {
      throw new Error('no board on show: use NAME, or --all');
    }
    return [u];
  };
  const on = { all: 'on every connected board', board: 'on this board, not the one on show' };
  return [
    {
      name: 'boards', group: 'boards', summary: 'your boards, connected or not', usage: 'boards', where: 'here', effect: 'reads', data: '[{ name, kind, version, connected }]',
      run(ctx) {
        const list = api.units();
        if (!list.length) {
          ctx.out('no boards yet: CONNECT A DEVICE', 'muted');
        }
        for (const u of list) {
          ctx.out(`${api.short(u).padEnd(14)} ${api.kind(u).padEnd(24)} ${(u.version || '-').padEnd(6)} ${u.online ? 'connected' : 'not connected'}`, u.online ? 'text' : 'muted');
          ctx.data({ name: api.short(u), kind: api.kind(u), version: u.version, connected: u.online });
        }
      },
    },
    {
      name: 'use', group: 'boards', summary: 'show one board, or all of them', usage: 'use NAME | use all', where: 'here', complete: names, effect: 'shows', data: 'none',
      run(ctx) {
        const name = ctx.args[0];
        if (name === 'all') {
          api.showAll();
          return 0;
        }
        const u = name && api.byName(name);
        if (!u) {
          ctx.err(`${name ?? ''}: no such board, boards lists them`);
          return 1;
        }
        api.showLive(u);
        return 0;
      },
    },
    {
      name: 'set', group: 'boards', summary: 'settings: theme, look, range, vectors, clock, block, conflict, flip, backlight, night, alerts, names, temp, pressure, wind, rain, wxpage', usage: 'set NAME=VALUE [NAME=VALUE...] [--all] [--board NAME]', where: 'here',
      options: on, valued: ['board'], effect: 'writes', data: '[{ name, value, boards }]',
      complete: (word) => api.settings.filter((s) => s.startsWith(word)),
      run(ctx) {
        // as many as are given, each on its own: set look=radar theme=amber backlight=25 --all is an evening in one line
        if (!ctx.args.length) {
          ctx.err('set takes NAME=VALUE: theme=amber look=map range=50 vectors=on');
          return 1;
        }
        let code = 0;
        for (const arg of ctx.args) {
          const [name, value] = arg.split('=');
          const done = chosen(ctx).filter((u) => api.set(u, name, value));
          if (!done.length) {
            ctx.err(`${arg}: no board took it. set takes NAME=VALUE: theme=amber look=map range=50 vectors=on`);
            code = 1;
            continue;
          }
          ctx.out(`${name} is ${value} on ${done.map(api.short).join(', ')}`, 'good');
          ctx.data({ name, value, boards: done.map(api.short) });
        }
        return code;
      },
    },
    {
      name: 'wifi', group: 'boards', summary: 'the networks a board keeps, a new one, or one to forget', usage: 'wifi | wifi add NETWORK | wifi forget NETWORK', where: 'here',
      options: { board: on.board }, valued: ['board'], effect: 'writes', bare: 'reads', data: '{ networks }',
      async run(ctx) {
        const [u] = chosen(ctx);
        const [how, ...rest] = ctx.args;
        const network = rest.join(' ');
        if (how === 'add' && network) {
          const pass = await ctx.ask(`password for ${network}: `, { secret: true });
          await api.send(u, `OWLOS SET ${JSON.stringify({ ssid: network, pass })}`);
          await api.send(u, 'OWLOS GET');
        } else if (how === 'forget' && network) {
          await api.send(u, `OWLOS SET ${JSON.stringify({ forget: network })}`);
          await api.send(u, 'OWLOS GET');
        } else if (how) {
          ctx.err('wifi add NETWORK, or wifi forget NETWORK');
          return 1;
        } else {
          for (const n of u.nets ?? []) {
            ctx.out(n === u.wifi ? `${n}  (first)` : n);
          }
          ctx.data({ networks: u.nets ?? [] });
        }
        return 0;
      },
    },
    {
      name: 'home', group: 'boards', summary: 'the middle of a board\'s radar, by a town, a postcode or a point', usage: 'home PLACE', where: 'here',
      options: { board: on.board }, valued: ['board'], effect: 'writes', data: '{ lat, lon }',
      async run(ctx) {
        const [u] = chosen(ctx);
        const found = await api.findHome(ctx.args.join(' '));
        if (!found) {
          ctx.err(`OpenStreetMap does not know "${ctx.args.join(' ')}"`);
          return 1;
        }
        await api.send(u, `OWLOS SET ${JSON.stringify(found)}`);
        await api.send(u, 'OWLOS GET');
        ctx.data(found);
        return 0;
      },
    },
    {
      name: 'screen', group: 'boards', summary: 'which of the screens a board is sold with: V1, V2, V3, or RGB, BGR', usage: 'screen NAME [--board NAME]', where: 'here',
      options: { board: on.board }, valued: ['board'], effect: 'writes', data: 'none',
      complete: (word) => ['V1', 'V2', 'V3', 'RGB', 'BGR'].filter((n) => n.startsWith(word)),
      async run(ctx) {
        const [u] = chosen(ctx);
        const names = api.screens(u);
        const at = names.indexOf((ctx.args[0] ?? '').toUpperCase());
        if (!names.length) {
          ctx.err(`${api.short(u)} has only one screen`);
          return 1;
        }
        if (at < 0) {
          ctx.err(`screen takes ${names.join(', ')}`);
          return 1;
        }
        await api.send(u, `OWLOS SET ${JSON.stringify({ screen: at })}`);
        await api.send(u, 'OWLOS GET');
        return 0;
      },
    },
    {
      name: 'flash', group: 'boards', summary: 'write the newest firmware to a board, its settings stay', usage: 'flash [--board NAME]', where: 'here',
      options: { board: on.board }, valued: ['board'], effect: 'runs', data: 'none',
      run(ctx) {
        api.flash(chosen(ctx)[0]);
      },
    },
    {
      name: 'skip', group: 'boards', summary: 'leave the touch alignment on a board\'s screen', usage: 'skip [--board NAME]', where: 'here',
      options: { board: on.board }, valued: ['board'], effect: 'writes', data: 'none',
      async run(ctx) {
        await api.send(chosen(ctx)[0], 'OWLOS SKIP');
      },
    },
    {
      name: 'send', group: 'boards', summary: 'one line to a board as it is: send OWLOS GET', usage: 'send LINE', where: 'here',
      options: { board: on.board }, valued: ['board'], effect: 'writes', data: 'none',
      async run(ctx) {
        await api.send(chosen(ctx)[0], ctx.args.join(' '));
      },
    },
    {
      name: 'log', group: 'boards', summary: 'what the board on show said, --all: every board; log on: as it says it', usage: 'log [on|off] [--board NAME] [--all]', where: 'here',
      options: { board: on.board, all: on.all }, valued: ['board'], effect: 'reads', data: '[{ line }]',
      run(ctx) {
        const only = ctx.opts.all ? null : ctx.opts.board ? chosen(ctx)[0] : api.current();
        const how = ctx.args[0];
        if (how === 'on' || how === 'off') {
          api.follow(how === 'on', only);
          return 0;
        }
        for (const line of api.logLines(only)) {
          ctx.out(line, 'muted');
          ctx.data({ line });
        }
        return 0;
      },
    },
  ];
}
