# Building in Actions

How the radar and FusionEdge build their firmware on GitHub. The full workflow is copied in
esp32-flash-and-settings, `references/ci-and-versions.md` and `files/flight-radar/pages.yml`.

## One job a board, in the vendor's image

- The radar: a matrix with one line a board, `{ disp, num, target }`, each job runs `idf.py` in
  `espressif/idf:release-v5.5`, the whole repository mounted, `-DRADAR_DISPLAY=<num> -DIDF_TARGET=<target>`, its
  own build folder and sdkconfig [measured, every push since 0.2].
- `fail-fast: false`: one board that does not build does not stop the others, and the site still goes out with
  the boards that built [measured].
- git inside the container runs as root on files that are not root's: `git config --global --add
  safe.directory '*'` before the build, or the version from git fails [measured].
- FusionEdge does the same with PlatformIO: the board's options folder copied over the root in the job's own
  checkout, a cache keyed by chip and by every `platformio.ini` [measured]. esp32-flash-and-settings,
  `references/platformio-pipeline.md`.
- The radar in the browser is its own job (`web`): Emscripten in `emscripten/emsdk`, one WebAssembly file a screen
  size [measured].

## One board only, by hand

Building every board takes a while and fills the queue with jobs a fork does not need. The radar's
`workflow_dispatch` takes an input [built 2026-11-23, not yet run on GitHub]:

```yaml
  workflow_dispatch:
    inputs:
      board:
        description: "One board only, by its build name (ili9341, st7796, gc9a01 ...). Empty: every board"
        required: false
        default: ""
```

- The matrix cannot be filtered at the job level: a job's `if` is worked out before the matrix exists, so it has
  no `matrix` [docs]. Each step carries the condition instead, and the other boards' jobs pass over every step and
  end at once:

  ```yaml
      - name: Build
        if: ${{ !inputs.board || inputs.board == matrix.disp }}
  ```

- `!inputs.board` is true on a push, where there are no inputs, and on an empty input [docs: a missing value is
  null, and null and '' are both false].
- The `web` job is passed over in a one-board run (`if: ${{ !inputs.board }}`); the site job still runs, because its
  `if` starts with `!cancelled()`: a job that needs a skipped job runs only when its `if` uses a status function
  [docs].
- A name that matches no board builds nothing. The site job stops then, with the names, before it publishes.
- In the parent repository a one-board run is built and kept with the run, never published: the site would lose
  every other board of that version (`archive.sh` replaces a version by each build of it). In a fork it is
  published, see fork-traps.md.

## Pull requests and concurrency

- `pull_request` into `main`: every board and the web page are built, nothing is published, the firmware stays
  with the run as artifacts [measured on the contributions that came in].
- `concurrency: group: pages-${{ github.ref }}, cancel-in-progress: true`: a push to `main` cancels the run of
  `main` before it, a pull request's push the run of that request, so a pull request never stops the site going
  out [measured].
- `permissions: contents: read` at the top, each job asks for more only where it needs it (the site job:
  `pages: write`, `id-token: write`, `actions: read`) [measured].

## Artifacts

- `fw-<board>`: the parts and `flash_args`, what the site job collects.
- `elf-<board>`: the ELF and sdkconfig, 90 days. A crash backtrace from a board flashed off the site turns into
  file and line only with the ELF of that very build; a local build does not match [measured].
- `site-fw`: a second copy of every version on the site, each run's, 90 days [measured].
- Artifacts of a public repository's runs are public, with sizes and expiry [docs].
