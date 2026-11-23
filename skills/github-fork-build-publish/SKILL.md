---
name: github-fork-build-publish
description: Fork a firmware repository on GitHub, build it in GitHub Actions and publish its own site on GitHub Pages, as OWLOS Flight Radar, FusionEdge, ESP32 Web Flasher and WIRE CITY do it. Use when someone wants their own copy of a project with their own build and their own browser flasher, when adding a workflow that builds one job per board in the vendor's Docker image (ESP-IDF, PlatformIO, Arduino, Emscripten), when a workflow should build one board only from Run workflow, when publishing with configure-pages, upload-pages-artifact and deploy-pages or from a docs/ folder, when a fork's run fails (Actions off in a fork, Pages source, the github-pages environment's branch rule, secrets and scheduled runs not in forks, files fetched from the parent's runs, a site URL or a stats address that still points at the parent), when keeping every old version on the site itself instead of GitHub Releases, or when sending the work back as a pull request.
---

# Fork, build in Actions, publish on Pages

Written 2026-11-23 from OWLOS Flight Radar (`.github/workflows/pages.yml`, `.github/archive.sh`,
`.github/make-manifests.sh`), the FusionEdge fork (PlatformIO in Actions, its own Pages flasher), ESP32 Web Flasher
(the smallest template) and WIRE CITY (Pages from a `docs/` folder, no Actions). It is the road a person takes to
make a project their own: fork it, build their board, get their own flasher page, and give the work back.

The deep details of the radar's pipeline (the manifests, the version line, the beta channel, the archive of old
versions) are in esp32-flash-and-settings. This skill is the road through them, and what changes in a fork.

## How sure

As in every skill of this collection: **measured** (run on GitHub or on the bench here), **built** (written and
checked locally, not yet run on GitHub), **docs** (GitHub's documentation), **reasoned**.

## The road, short

1. **Fork** on GitHub. Keep it public: Pages is free for public repositories [docs].
2. **Allow the workflows.** A fork's Actions tab says workflows are not run on a fork until you enable them
   [measured on the ESP32 Web Flasher template; docs].
3. **Pages source: GitHub Actions.** Settings, Pages, Build and deployment, Source: GitHub Actions. Once [measured].
4. **Run workflow** from the Actions tab. On the radar, type your board's build name in "board" (`ili9341`,
   `st7796`, `gc9a01` ...) to build only it, a few minutes instead of every board; empty builds all
   [built 2026-11-23, see references/actions-build.md].
5. **Open your site** (`https://<you>.github.io/<repo>/`), flash your board from it over Web Serial.
6. **Change, push, the site follows.** A push to `main` builds and publishes again.
7. **Give it back** as a pull request: it is built, never published, by the parent (references/pull-request-back.md).

## What a fork must change (or it still talks to the parent)

Anything the project wrote as its own address stays the parent's in a fork. On the radar:

- The boards ask the parent's `version.json` for updates and show the parent's QR code: `SITE_URL` is baked in
  `core/qr_site.h` by `tools/QrBake`. Run QrBake with your site's address [measured that the QR reads; the fork
  road reasoned].
- The map of boards counts where `server/public-url.txt` says. Empty it in a fork to count nowhere [reasoned from
  the workflow].
- Older versions the parent fetched from its own runs (`.github/archive-seed.txt`) do not exist in a fork, and a beta
  needs a stable already on the site. The radar's workflow skips the seeds and publishes each build as the newest
  in a fork, on `github.event.repository.fork` [built 2026-11-23, the scripts run locally against an empty site].

All of it, with the GitHub rules behind it, in references/fork-traps.md.

## The files

- [references/actions-build.md](references/actions-build.md): one job a board in the vendor's image, one board by a
  `workflow_dispatch` input, pull request builds, concurrency, artifacts and the ELF for crash lines.
- [references/pages-publish.md](references/pages-publish.md): the two ways to publish (an Actions artifact, or a
  `docs/` folder), `base_url` instead of a typed address, the old versions kept on the site itself, why not GitHub
  Releases, Pages answering 503.
- [references/fork-traps.md](references/fork-traps.md): everything that differs in a fork, each with its fix.
- [references/pull-request-back.md](references/pull-request-back.md): what a pull request from a fork can and cannot
  do, and how the radar took a board and a whole fork back.

Related: [esp32-flash-and-settings](../esp32-flash-and-settings/SKILL.md) (ci-and-versions.md, archive.md,
beta-and-stable.md, platformio-pipeline.md, template.md), [esp32-project-playbook](../esp32-project-playbook/SKILL.md)
(contributions, releases).
