# Near-Grog

Near-Grog is an unofficial fork of [ScummVM](https://www.scummvm.org), named after the
watered-down grog in *Monkey Island 2*: not the real thing, but it'll do.

It's the home for new engines for forgotten adventure games, optional enhancement
packs, and iPhone-first polish. Much of it is built with AI, which is why it's a
separate project: official ScummVM doesn't accept AI-authored code or AI art.

**This is not official ScummVM.** Please don't report Near-Grog problems to the
ScummVM team, and don't send Near-Grog code to them.

- **Rules for working in this repo:** [AGENTS.md](AGENTS.md)
- **ScummVM's own README:** [doc/README-ScummVM.md](doc/README-ScummVM.md)
- **Based on ScummVM at commit:** see [devtools/near-grog/UPSTREAM](devtools/near-grog/UPSTREAM)
- **Licence:** GPLv3 or later, the same as ScummVM ([COPYING](COPYING))

You need your own copies of the games. Near-Grog never includes game files.

## Roadmap

1. iOS build
2. **Dark Lord** (Datasoft, 1987, C64): playable, testing. The `darklord` engine runs
   the game's original 6502 code on a minimal C64 (no Commodore ROMs). Needs both
   disk sides, `DARKLRD0.D64` and `DARKLRD1.D64`. Research notes live in the
   owner's `jk-files` repo under `projects/dark-lord`.
3. **Living pictures for Infocom games:** an optional pack showing pictures and
   short loops above the text, starting with Zork I.
4. **The Fool's Errand, DOS colour version** (1989): a new engine, after Dark Lord.
   ScummVM's existing `fool` engine covers the Mac versions.
5. **The Transylvania games:** both C64 versions of Transylvania, The Crimson Crown,
   and Transylvania III for PC.
