![Logo QLuzznic](ql/assets/logo_qluzznic.png)

# QLuzznic

A Vexed-style block-matching puzzle game for the Sinclair QL, written in C
and 68000 assembly, running in mode 8 (256x256, 8 colours) on real
128K-RAM QL hardware (and in emulation via [QemuLator][qemulator] or the
[MiSTer QL core][ql-mister]).

Ships its own 100-level campaign (`ql/assets/qluzznic-levels.json`), a
"joker" wildcard piece, a hex continue-code system, and a title screen
with a bounce-in logo animation -- all built to fit the real hardware's
128K ceiling.

## Building

This repo doesn't vendor the QDOS cross-compiler toolchain -- set up your
own copy of:

- [xtc68][xtc68] (MIT licensed) -- the `qcc`/`as68`/`qld` cross-compiler.
- [c68-support][c68-support] -- the QDOS C runtime headers and libraries
  xtc68 needs (no license declared upstream; check with the author before
  redistributing it yourself).

Follow xtc68's own README for installation (`qcc`/`as68` on `PATH`, the
C68 runtime headers/libs reachable the way xtc68 expects -- `QLINC`/
`QLLIB` or an `sdk-install.sh`-style layout). Once `qcc -o hw hw.c` works
for a trivial QDOS "hello world", this project's own build will too:

```sh
cd ql
make          # builds build/game.qlpak
make run      # builds, then opens it in QemuLator (set QEMULATOR_APP=
              # to point at your own install -- see the Makefile)
```

`ql/asm/*_data.s` and `ql/src/levels.c` are generated from the source
assets in `ql/assets/` (PNGs, `qluzznic-levels.json`) by the scripts in
`ql/tools/` -- not part of the normal `make` build, re-run the relevant
script by hand whenever you change a source asset (see that generated
file's own header comment for the exact command).

## Testing

`ql/tests/host_test.c` runs the portable game-logic (`ql/src/game.c`)
natively, without the QL toolchain or an emulator:

```sh
cd ql
cc -I src tests/host_test.c src/game.c src/levels.c -o tests/host_test
./tests/host_test
```

## License

QLuzznic's own code and assets are MIT licensed -- see [LICENSE](LICENSE).
This does not cover the third-party toolchain you set up separately to
build it (see above).

[qemulator]: http://www.qemulator.com/
[ql-mister]: https://github.com/MiSTer-devel/QL_MiSTer
[xtc68]: https://github.com/stronnag/xtc68
[c68-support]: https://github.com/xXorAa/c68-support
