# Working in Near-Grog

Near-Grog is the owner's private fork of ScummVM. It is a separate project with its
own rules, which replace ScummVM's `AGENTS.md` here.

ScummVM's [AI-GUIDELINES.md](AI-GUIDELINES.md) governs contributions **to official
ScummVM**. Near-Grog never submits code upstream, so AI-written code is allowed in
this repo. Respect ScummVM's rules in spirit:

- **Never open pull requests, issues or patches against scummvm/scummvm** from this
  repo, and never present Near-Grog work as ScummVM's.
- **Say it's AI-built.** End commit messages with `Assisted-by: Claude`, plus
  the attribution lines the session provides. Don't name AI models anywhere else:
  code, comments, docs or notes.

## Rules for this repo

- **Follow ScummVM's coding conventions** (formatting, naming, engine structure),
  so the code stays readable and upstream merges stay easy.
- **Never commit game files** or anything extracted from them (pictures, text,
  music). Players supply their own games.
- **Engines are faithful.** A new engine reproduces the original game with its own
  art. No AI art inside an engine.
- **Enhancements are optional packs**, kept separate from engines and switchable
  off by the player: AI pictures and loops, new sound, living pictures.
- **Keep upstream files untouched where possible.** Put Near-Grog work in new
  engine folders and small, clearly marked hooks, so syncing with ScummVM stays
  simple.
- **Licence:** everything stays GPLv3 or later.

## Syncing with ScummVM

The repo holds a snapshot of ScummVM, not its full history (the history is over
2 GB). `devtools/near-grog/UPSTREAM` records the upstream commit the snapshot came
from. To update, run `devtools/near-grog/sync-upstream.sh`: it applies the changes
between the recorded commit and ScummVM's latest `master` on top of Near-Grog, then
records the new commit. Resolve any conflicts in favour of upstream unless the file
is Near-Grog's own (`README.md`, `AGENTS.md`, `CLAUDE.md`, `devtools/near-grog/`,
`.github/workflows/`). Near-Grog runs its own CI, not ScummVM's.

## iOS builds

`.github/workflows/near-grog-ios.yml` builds the iPhone app on every push to
`main` that changes code, and publishes `Near-Grog-ios.ipa` (unsigned, for
sideloading) to the `ios-latest` prerelease. **Pushing to `main` publishes a test
build.** The repo is private, so macOS build minutes are limited: the build only
includes the engines listed in the workflow's `ENGINES` line. Add engines there
when they're needed.

## Related notes

The owner's `jk-files` repo has the project plan (`projects/near-grog`) and the
Dark Lord research (`projects/dark-lord`).
