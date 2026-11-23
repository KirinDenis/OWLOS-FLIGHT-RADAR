# The site's console and its command contract

The Flight Radar's page has a console since 0.8, grown in each version since: CONSOLE at the top right, or
Ctrl+`. It works on the boards
on the page's cable the way a shell works on files, and a line typed there acts on the live board at once. Under
it are the USB lines of `usb-console.md`. Real file: `../files/flight-radar/site/board-console.js` (the board
commands and the `/boards` folders). The shell itself (grammar, help, man, ls, cat, grep and the like) is a copy
of a shared core that several of the owner's web consoles use, copied, not linked.

How sure: built and live on the site. Checked in a test copy of the site with a fake board (`set`, `config`
written and read, the chips that follow it): measured in the browser, not with a person.

## What a person types

- `help` lists the commands, `man NAME` says more of one.
- `boards` lists your boards, connected or not. Each has a name, `2.8-cyd`, `1.28-round`, with the end of its MAC
  when two are alike (`2.8-cyd-1a2b`). `use NAME` shows one, `use all` all of them.
- `set NAME=VALUE ...` on the board on show, `--board NAME` on another, `--all` on every connected one: theme,
  look, range, vectors, clock, block, conflict, flip, backlight, night, alerts, names, temp, pressure, wind,
  rain, wxpage. As many at once as you like: `set look=radar theme=amber backlight=25 --all`. Each is done on
  its own; one no board took is said, the rest are set.
- `home PLACE` moves a board's home to a town, a postcode or a point: the page asks OpenStreetMap, then sends
  SET.
- `wifi` lists the networks a board keeps, `wifi add NETWORK` asks for the password (typed hidden, never kept in
  the history), `wifi forget NETWORK`.
- `screen V2` (or V1, V3, RGB, BGR) says which screen a board was sold with.
- `skip` leaves the touch alignment on a board's screen (`OWLOS SKIP`).
- `log` shows what the board said, `log on` as it says it, `--all` every board's.
- `send LINE` sends one line as it is, `send OWLOS GET`. One with JSON goes in single quotes, as a shell wants:
  `send 'OWLOS SET {"led_test":1}'`.
- `flash` writes the version the board follows, stable or beta (`beta-and-stable.md`). Its settings stay.
- Each board is a folder, `/boards/NAME`, with the files `info`, `config`, `wifi` and `log`. `cat config` reads
  its settings as `NAME=VALUE` lines, and writing the file sets them on the live board: `echo theme=amber >
  config`, or `cat /boards/2.8-cyd/config > /boards/1.28-round/config` to give one board all another one has.
  A line a board does not take (a light that does not dim, a word `set` does not know) is said and left; a
  file with nothing set is an error. The other files only show what a board says.
- It is a shell: `|` into `grep`, `sort`, `head`, `tail` and `wc`, `history` for what you typed (never a
  password), and `alias` for a word of your own: `alias evening='set look=radar theme=amber backlight=25 --all'`,
  then `evening`.

The names with the MAC's end fixed a real bug: with two boards of one kind, a button in one's window could go
to the other, often one not on the cable, and nothing happened (fixed in 0.16.1).

## The command contract

Every UI's buttons and every model run commands, so each command says what it does. A command is a
description, and `man`, Tab and the check of options come from it:

```js
{
  name: 'set', group: 'boards', summary: 'settings: theme, look, range, ...',
  usage: 'set NAME=VALUE [NAME=VALUE...] [--all] [--board NAME]', where: 'here',
  options: { all: 'on every connected board', board: 'on this board, not the one on show' },
  valued: ['board'],          // options that take a value
  effect: 'writes',           // what it does
  data: '[{ name, value, boards }]',   // the shape of its --json
  complete: (word) => ...,    // what Tab offers
  run(ctx) { ... },           // ctx.out(text, tone), ctx.err(text), ctx.data(object), ctx.ask(question, { secret })
}
```

The effects, one word each:

- `reads`: only looks, nothing changes anywhere.
- `shows`: changes what this page shows or where the terminal is; nothing kept.
- `runs`: starts something on the machine (what ran before is gone).
- `writes`: makes new files or new things.
- `deletes`: takes away, or writes over, what was there.
- `setup`: changes the person's setup: drives, settings, names, variables.
- `shares`: gives something to others, or sends it out of this page.

The rules around them:

- `effects` names an option that changes the effect (`history -c` is `deletes`), `bare` the effect with no
  words after the name (`alias` alone only lists: `reads`; `wifi` alone lists: `reads`).
- A command that declares no effect counts as `setup`: not knowing what it does is a reason to ask.
- `deletes`, `setup` and `shares` run only after the person's Yes when a model, or a UI somebody else
  described, runs them. `model: 'never'` marks a command a model may not run at all.
- `help --json` is the whole list, each command's contract as data: the API a UI or a model builds on.
  `man NAME --json` gives one.
- Never rename a command, never change what one means: a profile or an alias is a list of commands, and old
  ones must still run. Use the Linux name wherever Linux has the word.

The board commands as declared: `boards` reads, `use` shows, `set` writes, `wifi` writes (alone: reads),
`home` writes, `screen` writes, `flash` runs, `skip` writes, `send` writes, `log` reads.

## The grammar, in short

- Words, quotes as bash, `$NAME` and `${NAME}` from the variables, `~` is `$HOME`, aliases, and `|`, `&&`, `;`,
  `>`, `>>`.
- A backslash before a letter, a digit, `_` or `.` is kept (a DOS path typed as DOS has it); before anything else
  it escapes it, as bash's does.
- Pipes are not streams: each command of a pipeline runs to its end and hands the next its lines. Enough for
  grep, sort, head and wc; `tail -f` would want more.
- A UI gives its paths: `resolve`, `list`, `read`, `isDir`, `write`, `complete`. The `/boards` paths are the
  page's (`BoardPaths` in `board-console.js`): only a board's `config` is written.

## Why a console on a flasher page

- A person who knows a shell does in one line what takes many clicks, and an alias is a whole setup in one word.
- A model working in the page uses the same commands as the buttons, with the same effects, and asks the person
  where the contract says so.
- A test drives the page by typing, with no pixel hunting (`fake-board-testing.md`).
