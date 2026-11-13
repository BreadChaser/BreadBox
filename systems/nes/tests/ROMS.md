No commercial games are used by this system's tests.

- `make-rom.py`: original test cartridge source in this repository, CC0-1.0.
  It generates mapper-0 content with a blue screen and a battery frame counter.
  Reproduce with `python3 systems/nes/tests/make-rom.py /tmp/host-fixture.nes`.
  No assembler or downloaded build script is executed.
- Additional local smoke: Damian Yerrick's `volume_tests/volumes.nes` from
  https://github.com/christopherpow/nes-test-roms at
  95d8f621ae55cee0d09b91519a8989ae0e64753b, downloaded 2026-10-28.
  `volume_tests/README.txt` explicitly grants use, alteration and redistribution
  under a three-clause zlib-style licence, copyright (c) 2009 Damian Yerrick.
  Download and licence copy stay in the ignored host evidence workspace.
