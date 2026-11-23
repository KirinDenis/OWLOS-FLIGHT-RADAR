# Giving the work back

## What a pull request from a fork gets

- The parent's workflow runs on `pull_request` with the fork's code: every board is built, nothing is published,
  the firmware stays with the run as artifacts that anyone can flash to try [measured on the radar].
- No secrets and a read-only token [docs]. A build that needs a secret cannot be checked from a fork.
- A first-time contributor's run may wait for a maintainer's approval [docs].

## How the radar took work back

The technique, from esp32-project-playbook, `references/contributions.md`:

- **One board in a pull request**: reviewed by what it changes on boards the author does not have. Shared code
  behind the new board's switch, so the other boards build as before.
- **A whole fork with history you do not want** (a vendor's GPL firmware in an old commit, megabytes of video):
  `git merge --squash` of the fork's last commit onto a branch of the project's own, conflicts resolved there. The
  unwanted history never enters.
- **Licences**: nothing GPL into an MIT repository; vendored libraries keep their own licence files.
- **Credit** by the public GitHub handle the person uses, in the README and CHANGES, never an email.
- **While a beta is out**: a merge goes into the next version, not into the beta being held.

## What makes a pull request easy to take

- One thing per pull request, and the boards it was run on, with the words of this collection: measured on which
  board, or built only.
- Settings stay compatible (fork-traps.md).
- The PC preview shots before and after, for anything drawn.
- What you learned, said in the pull request: it goes back into these skills.
