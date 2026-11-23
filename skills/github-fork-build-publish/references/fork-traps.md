# What differs in a fork

Each trap with its fix. The GitHub rules are from GitHub's documentation [docs]; what the radar does about them is
marked by its own word.

## GitHub's rules for forks

- **Workflows do not run in a fork until its owner enables them** on the Actions tab [docs; measured with the ESP32
  Web Flasher template].
- **Pages is off in a fork.** Settings, Pages, Source: GitHub Actions, once. Until then `configure-pages` fails
  [measured in our repositories].
- **The `github-pages` environment deploys from the default branch only**, unless its branch rules are changed.
  Run workflow on another branch builds, and the deploy is refused by the environment's protection rules [docs].
  Run it on `main`, or allow the branch in Settings, Environments, github-pages.
- **Secrets are not copied into a fork**, and a pull request from a fork gets no secrets and a read-only token
  [docs]. The radar's build needs none.
- **Scheduled workflows are off in a fork** until enabled [docs]. The radar has none.
- **Pages is free for public repositories**; a private one needs a paid plan [docs].
- `github.event.repository.fork` is true in a fork's runs [docs]. The radar's workflow uses it.

## The radar's workflow in a fork

Fixed in the workflow on 2026-11-23 [built; the scripts were run locally against an empty site, the workflow not
yet run in a fork]:

- **The seed versions.** `archive-seed.txt` names runs of the parent. In a fork `gh run download` cannot fetch them
  and the run would stop before it publishes. In a fork the site job passes `/dev/null` as the seed list.
- **A beta on an empty site.** A beta needs a stable on the site to stand beside, and a fork's site starts empty:
  `archive.sh` stopped with "the site has no stable version to keep beside it" (measured locally with the parent's
  path). In a fork each build is published as the site's newest version; the page still says "beta" where
  `CHANGES.md` does.
- **One board.** A run with the `board` input publishes in a fork only. The fork's site then holds that board for
  that version. A second one-board run of the same version replaces it with the other board: run without `board`,
  or give the version a new number, to keep both.

## Addresses the fork must change itself

- **The firmware's site address.** `SITE_URL` in `core/qr_site.h`, baked by `tools/QrBake` (.NET 8) with the QR code.
  The boards ask `SITE_URL` + `version.json` for updates and the ABOUT page shows its QR. Until it is changed, a
  fork's boards follow the parent's versions [reasoned from the code].
- **The stats address.** `server/public-url.txt` goes into `version.json` as `stats`, and the page and the boards
  count there. Empty it in a fork to count nowhere [reasoned from the workflow].
- **Repo links on the page.** Anything typed as the parent's address in `site/`. FusionEdge's job replaces `@REPO@`
  instead, which a fork needs no edit for [measured there].

## Before you push a fork's first change

- Keep the settings format compatible: boards flashed from the parent and from the fork read the same NVS. A changed
  stored block gets a new version number and still reads the old one (esp32-flash-and-settings, nvs-settings.md).
- Keep the version line: the first `## ` line of `CHANGES.md` is the version. A fork that wants its own numbers can
  add a suffix in the text, never a lower number than its site has (the run stops on that).
