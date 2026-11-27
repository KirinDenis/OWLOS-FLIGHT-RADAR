# The build PC

The quirks of the Windows PC these projects are built and flashed on, and what an agent working there must know.
How sure: [measured] seen here, [docs] read, [reasoned] worked out.

## What is there and what is not

- Windows 10, Git Bash and PowerShell. No `gh`, no `jq`, no `zip`, no WSL distribution, Docker Desktop not running.
  Use the GitHub REST API anonymously for reads, `curl`, `unzip`, and Windows `tar.exe -a -cf x.zip` for zips.
- No Python for project tools: every converter, baker and rig is C# (.NET 8) or PowerShell. Python appears only
  inside the toolchains that bring their own (ESP-IDF's `idf.py`, `esptool`, `esp_coredump`; Emscripten).
- Do not start the maintainer's own applications (their browsers, Docker Desktop). Use the agent's own browser pane
  or a headless tool.

## ESP-IDF

- ESP-IDF is at `%USERPROFILE%\esp\v5.5\esp-idf`, the v5.5 tag. `BUILD.BAT` sets it up, putting IDF's own Python
  first on PATH. Run it by full path: a `cd` in PowerShell does not reach `cmd /c`.

```powershell
cmd /c "<repo>\firmware\BUILD.BAT <screen> > %TEMP%\build.log 2>&1"
"exit $LASTEXITCODE, warnings: $((Select-String -Path $env:TEMP\build.log -Pattern 'warning:').Count)"
```

- From Git Bash, `cmd //c` did not find `idf.py` [measured]. Use PowerShell.
- The C5 is a preview target on the 5.5 tag: `idf.py --preview`. CI's `release-v5.5` image needs no flag.
- The P4 builds stop at configure here with the component manager's `MissingKconfigError`; CI builds them
  [measured]. Do not chase it inside an unrelated change.
- With Docker not running, CI's image cannot be used here. The local v5.5 stood in for it in a release rehearsal:
  fresh builds with the workflow's `idf.py` line came out byte for byte the size of the dev builds [measured]. The
  two are still not the same bytes, so a local ELF does not decode a board flashed from the site.

## PlatformIO

- `pio run` started from Git Bash fails mid-build with "ERROR: MSys/Mingw is not supported" [measured, PlatformIO
  6.1.18, pioarduino 55.03.37]. Run it from PowerShell: `cmd /c "pio run ... > log 2>&1"`.
- Set `PLATFORMIO_CORE_DIR` to a work folder of the project, so the maintainer's own `~/.platformio` (another
  project's pinned platform) is never touched. The first install there is about a gigabyte and several minutes.
- pioarduino's `pio project metadata` lists the flash images but no application offset; its esptool (5.1.2) is its
  own, not the one on PyPI [measured].

## Emscripten (the web twin)

- Call `emcc` from its own folder with `EM_CONFIG` and `EMSDK_PYTHON` set (`web/BUILD.BAT` does it, from `EMSDK`).
  Running `emsdk_env.bat` from a batch file left no `emcc` on PATH [measured].
- About 2 GB installed. The local version matches CI's image (6.0.13 on both); keep them equal, the flags live in one
  file both read.

## Boards on the desk

- Port numbers move when boards are replugged. Know a board by its chip and MAC, never by a port number from last
  time: a wrong board was once blamed that way [measured]. Ask an unknown MAC `OWLOS HELLO` before flashing: it says
  its version, build and MAC.
- A port held by a browser tab with the flasher site connected says "Access is denied" to esptool. The tab is the
  maintainer's: ask for it to be closed.
- An `idf.py monitor` open in the maintainer's terminal holds the port too: read the terminal, never kill it.
- Kill only processes you started.
- Before writing a board that holds something of value, read its whole flash to a backup. Backups and rigs go in a
  folder that lasts: scratch and temp folders get cleaned, and rigs were lost that way [measured].
- The WiFi password of the bench is the maintainer's: a rig asks for it at run time, the agent never types it.
- CH340 reads fail at 921600 baud: 460800.

## The browser pane and local sites

- Local web previews run from a launch configuration, each site on its own port; a test copy of a site gets its
  own port and folder, never the repository's.
- The browser pane paints stale pictures while its window is hidden: check through the DOM and the network log,
  not a screenshot, and say so. A hidden pane also runs about one frame a second and slows timers.
- A 0x0 viewport breaks pages that size things from it: set the window size before loading.
- Its Web Serial chooser returns no port at once: test the serial path with a fake port object.

## Small things

- A shell `rm -rf "$VAR"` is refused by a safety check. Copy over the old folder, or use a new folder.
- Files for DOS need CRLF. The ESP32 repositories keep LF in git; a Windows checkout with `core.autocrlf` has CRLF
  in the working copy, a few files LF, and an edit keeps each file's own. Git Bash's `sed -i` drops the CRs of a
  CRLF file: two core files came back LF (measured). `unix2dos` puts them back; count them with
  `tr -cd '\r' < f | wc -c` against `wc -l`, since `grep -c $'\r$'` miscounted an LF file as all CRLF (measured).
- Comparing sizes before and after a change is the cheapest regression test there is.
- A rig run through `powershell -File` loses the quotes in its JSON arguments: call it in the same process with
  `& script -Send @(...)` [measured].
