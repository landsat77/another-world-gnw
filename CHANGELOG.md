# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`): CI uses the matching
section as the GitHub Release notes.

## [Unreleased]

## [v1.1.0] - 2026-10-18

### Added

- Pause menu option "Swap A/B" (A = jump, B = action), saved per pack in
  `/data/anotherworld/<name>/gnw_options.bin`.

## [v1.0.0] - 2026-10-18

### Added

- Another World (PC DOS) as a Retro-Go SD GWHB homebrew, built on the raw
  engine (NEO-RAW) and the retro-go-sd-templates SDK (firmware ABI v2,
  Retro-Go SD v2.0.0-rc1 or later).
- Game data loaded from a `.aw` pack next to the `.bin`
  (`tools/aw_pack.py`: the banks are unpacked on the PC, the console uses
  the data in place from its external flash).
- Savestates, 4:3 scaling with optional filtering, 16 bit sound mixing.
- Copy protection screen skipped.
