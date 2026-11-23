# Publishing on GitHub Pages

## Two ways

- **From Actions** (the radar, FusionEdge, ESP32 Web Flasher): Settings, Pages, Source: GitHub Actions, once. The
  site job builds a `public/` folder and publishes it with `actions/configure-pages`, `actions/upload-pages-artifact`
  and `actions/deploy-pages`, in the `github-pages` environment, with `pages: write` and `id-token: write`
  [measured]. Nothing built goes into git.
- **From a folder** (WIRE CITY): Pages serves `docs/` of the branch as it is. The site is whatever is committed
  there; a script puts the built files in `docs/` before the commit (`docs/pack-site.ps1` there) [measured]. Simple,
  but every build lands in the history.

Pick Actions for firmware: the binaries are big, change every build, and a fork gets them built without anyone
committing them.

## The site's own address, never typed

`actions/configure-pages` answers `base_url`, where the site is now. The radar's site job reads the older versions
from there (`SITE: ${{ steps.pages.outputs.base_url }}`), so the same workflow works in any fork [measured in the
parent]. Only the firmware still has the parent's address baked in (fork-traps.md).

FusionEdge's page gets `@VERSION@` and `@REPO@` replaced by the job, so a fork's page links to the fork
[measured].

## Old versions kept on the site itself

A deploy replaces the whole site. To keep every version a person can go back to, the radar's run takes the older
versions from the live site and puts its own build next to them (`.github/archive.sh`) [measured since 0.4]:

- The newest stable in `fw/<board>/`, the older ones in `fw/<version>/<board>/`, all listed in `fw/versions.json`.
- A version the site has and the run cannot fetch, or a number lower than one the site has, stops the run before it
  publishes: the site stays as it was.
- A beta goes beside the stable in `fw/<version>/`, and needs a stable on the site.
- The versions from before the site kept them come once from the runs that built them (`archive-seed.txt`, `gh run
  download`, `actions: read`). Those runs are the parent's.

The design and its tests: esp32-flash-and-settings, `references/archive.md` and `references/beta-and-stable.md`.

## Why not GitHub Releases

The first design made a release a version. It failed twice on GitHub with 403 "Resource not accessible by
integration": the workflow's token may not create a tag on a commit that changes `.github/workflows`, and the
commits to tag did [measured, archive.md tells it]. The archive moved onto the site.

## Pages answers 503 now and then

A run asks the live site for hundreds of files. Every fetch retries a passing error (`curl --retry 4 --retry-delay
5`, which covers 408, 429, 500, 502, 503, 504 and timeouts) and an answer that stays bad still stops the run
[measured that 503 happens; how often is not recorded].
