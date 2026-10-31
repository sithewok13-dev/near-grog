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
2. **Dark Lord** (Datasoft, 1987, C64 and Apple II): playable, testing. The `darklord`
   engine runs the game's original 6502 code on a minimal C64 or Apple II (no Commodore
   or Apple ROMs). Apple II: both sides as 140K `.dsk`/`.do` images, loose or zipped.
   C64: both disk sides, under any names, loose or in a `.zip`: the cracked release as `.d64`
   files, or the original copy-protected disks as `.g64`/`.nib` (or `.d64`) images. Research notes live in the
   owner's `jk-files` repo under `projects/dark-lord`.
3. **Living pictures for Infocom games:** an optional pack showing pictures and
   short loops above the text, starting with Zork I.
4. **The Fool's Errand, DOS colour version** (1989) and **Dondra: A New Beginning**
   (Questmaster I, DOS, 1991): playable, testing. The `dosgames` engine runs each game's
   original program on a minimal PC (8086, VGA mode 13h, mouse, keyboard, timer, AdLib;
   no Microsoft or IBM code). Point it at the game's folder as installed.
   ScummVM's existing `fool` engine covers the Mac versions of The Fool's Errand.
5. **Transylvania (C64, 1984 and 1985) and Crimson Crown (C64)**: playable, testing, in
   the same engine as Dark Lord (their own programs on the minimal C64). Disk images
   loose or zipped. Typing `sithewok` turns a cheat on or off: no death (a fatal move
   is undone, and a monster that would have killed you leaves), and the 1984 clock stops.
   Still to come: Transylvania III for PC.
