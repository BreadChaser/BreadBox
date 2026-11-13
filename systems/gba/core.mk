# How BreadBox builds and links the GBA core (tools/build/core-blob.py reads
# this file; build/3ds/Makefile includes it). Every key starts with the system id.
gba_CORE_DIR := gba-core
gba_CORE_MAKE := platform=ctr EXTRA_CFLAGS=-DBREADBOX_ARENA
gba_CORE_ARCHIVE := gpsp_libretro_ctr.a
gba_CORE_PREFIX := gpsp_
# translation cache bytes the core takes from the shared executable arena
gba_CORE_JIT_HEADER := gpsp_config.h
gba_CORE_JIT_DEFINES := -DSMALL_TRANSLATION_CACHE
gba_CORE_JIT_EXPR := ROM_TRANSLATION_CACHE_SIZE + RAM_TRANSLATION_CACHE_SIZE
# what the frontend side of the system compiles and links against
gba_CORE_INCLUDES := gba-core/libretro/libretro-common/include
gba_CORE_LIBS := -lminizip
gba_CORE_LICENSE := GPL-2.0-or-later
# Isolate journal lighting while retaining the established SDK entry addresses.
gba_CORE_PREPARE := python3 systems/gba/lighting-pins.py
gba_CORE_LIBDIRS := build/systems/gba/sdk
# Preserve the measured incremental archive's member order on clean builds.
gba_CORE_OBJECT_ORDER := systems/gba/core-objects.txt
# Legacy cartridge loader already supplies content and AV geometry.
gba_CORE_HOST_OMIT := get_system_info
