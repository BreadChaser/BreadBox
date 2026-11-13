# ARM fixture comparisons

Use `runner.c` with a baseline and candidate **ARM Linux** gpSP library. Run it
with qemu-arm, not a native x86 interpreter: the changed emitter must execute.
ROMs and BIOS files are private inputs and are not distributed with the tests.

The driver leaves option updates disabled, matching a production cold load, and
asserts the actual engine immediately after loading. A 1 MiB mirrored cartridge
must retain JIT when requested and available; ample heap must never silently
select the interpreter. Run a mirrored mini ROM as well as a padded fixture to
cover both New's materialized image and Old's ROM-cache path. An interpreter-only
hash match cannot count as an ARM emitter test.

Build each library in a private copy, with `platform=armv6-hardfloat`, an ARM
Linux `CC`/`CXX`, and the same optimization/cache/ROM-buffer settings for both.
Delete stale objects when changing build settings. The CTR build uses a 384 KiB
RAM translation cache and 4 MiB ROM buffer; define `SMALL_TRANSLATION_CACHE`
and `ROM_BUFFER_SIZE=4` when reproducing those limits on Linux.

Link the driver with `-Wl,--enable-new-dtags,-rpath,'$ORIGIN/base-core'` so that
`LD_LIBRARY_PATH` can actually select the candidate. The cross toolchain's
legacy RPATH otherwise silently wins over that variable.

```
arm-linux-gnueabihf-gcc -O2 -I CORE/libretro/libretro-common/include \
  tests/gba_arm_exact/runner.c -L CORE -l:gpsp_libretro.so \
  -Wl,--enable-new-dtags,-rpath,'$ORIGIN/base-core' -o runner
AUDIT=1 LD_LIBRARY_PATH=CORE qemu-arm -L ARM_LINUX_SYSROOT \
  ./runner 1 ROM.gba 600 0 - /dev/null - - final.state
```

Arguments: JIT enabled, ROM, frames, stereo strength, RGB565 dump path (`-` for
none), input script, battery input (`-`), state input (`-`), state output (`-`).
Input script rows are `first_frame end_frame hex_libretro_key_mask`.
`AUDIT=1` hashes every video frame and PCM sample. Compare the complete final
`frames=... video=... audio=...` line **and** SHA256 of `final.state`.
`time()` is fixed in the driver, including initialization of the serialized RTC
baseline; monotonic elapsed time and the run guard remain real.

The Pokemon Video fixture presses A with `600 606 100` and runs 1800 frames.
For a playback-only profile, first save at frame 900, then load that state and
run another 1800 frames with no input. Verify the captured picture is episode
content. Menu throughput is not playback throughput.

This is an exact-output gate, not an Old 3DS speed predictor. Real hardware
acceptance still requires a controlled run of the final CIA.
