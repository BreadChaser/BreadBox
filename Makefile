# Stable compiler timestamps even without env.sh or Git (source archives).
# Release builders may override this with their frozen source epoch.
SOURCE_DATE_EPOCH ?= $(or $(shell git log -1 --format=%ct 2>/dev/null),0)
export SOURCE_DATE_EPOCH

# BreadBox is the only product (PSX3D was frozen at v0.5.2, docs/building.md).
# apps/breadbox.mk holds its metadata and compile-time identity.
ifneq ($(filter-out breadbox,$(APP)),)
$(error APP=$(APP) no longer exists: PSX3D is retired, plain make builds BreadBox)
endif
include apps/breadbox.mk
.DEFAULT_GOAL := all

# Builds the PCSX-ReARMed core as a static library, then the BreadBox app.
.PHONY: all core app cia cxi clean sdk-check host-tests title-version-check

all: app

# The private citro3d code (platform/3ds/renderqueue.c, gpu_cache.c) needs the pinned SDK:
# stop before compiling anything when libcitro3d, its headers or libctru differ from
# tools/build/toolchain.lock. SDK_CHECK=0 skips it (deliberate SDK upgrades only).
SDK_CHECK ?= 1
sdk-check:
	@python3 tools/build/toolchain.py check --abi
ifeq ($(SDK_CHECK),1)
core: sdk-check
endif

# Every host check that needs no ROM, BIOS or console (what CI runs).
host-tests:
	bash tools/build/host-tests.sh

# The hardware-validated renderer layout. Other layouts belong in host research tools.
ifneq ($(filter-out split,$(PICA_LAYOUT)),)
$(error Only the validated split renderer layout is supported)
endif
export PICA_LAYOUT_CFLAGS := -DPICA_LAYOUT_SPLIT

EXTERN_SYMS := psx3d_depth_get psx3d_depth_frame psx3d_overlay_get psx3d_ov_seq psx3d_set_strength \
	psx3d_right_get psx3d_stats psx3d_set_half3d psx3d_prof_t psx3d_prof_c psx3d_prof_on new_dynarec_print_stats \
	psx3d_irq_t psx3d_irq_c CdromId
EXTERN_SYMS += CdromExeHash psx3d_widescreen_hook psx3d_widescreen_state_hook psx3d_patch_clear
EXTERN_SYMS += Mcd1Data Mcd2Data psx3d_clip_prepare padAnalogMode padToggleAnalog
EXTERN_SYMS += SaveFuncs LoadMcd Config
# Already used by the PS1 core: expose their existing sections for the library
# probe. Keep bindings identical across APP switches without moving core code.
EXTERN_SYMS += chd_open_file chd_close chd_read chd_get_header
# core-internal calls that go to PSX3D (the core's code stays as it is)
WRAP_SYMS := LoadMcd
EXTERN_SYMS += psx3d_idle_skip psx3d_idle_pointer psx3d_idle_delay
EXTERN_SYMS += psx3d_spu_worker_ticks psx3d_spu_worker_items psx3d_skip pica_display_changed
# Real 3D (systems/ps1/gpustereo.c): the frontend takes over two core hooks
# (--wrap inside the core's partial link; -u __real_ keeps the originals in place)
# and reads the GTE depth table and gpulib's drawing registers.
STEREO_WRAP_SYMS := psx3d_depth_cmd psx3d_stereo_set
WRAP_SYMS += $(STEREO_WRAP_SYMS)
EXTERN_SYMS += $(STEREO_WRAP_SYMS) $(addprefix __real_,$(STEREO_WRAP_SYMS)) psx3d_stereo_enabled psx3d_ztable psx3d_zframe gpu
# Uploads that change nothing stop at software VRAM (systems/ps1/uploadskip.c)
UPLOAD_WRAP_SYMS := pica_settle_rect renderer_update_caches
WRAP_SYMS += $(UPLOAD_WRAP_SYMS)
EXTERN_SYMS += $(UPLOAD_WRAP_SYMS) pica_drawn_blocks pica_drawn_tiles pica_rtt_detected
EXTERN_SYMS += pica_init pica_exit pica_game_begin pica_game_end pica_flush_vertices pica_frame_started pica_frame_ended \
	pica_texmask pica_texmemo psx3d_skipgate pica_thread_call pica_thread_sync pica_state_lost pica_prepare_present pica_picture pica_stats pica_probe pica_debug pica_vertices pica_submit pica_prims pica_pmu pica_open_frame pica_async pica_double_buffer pica_wait_frame pica_frame_pending pica_dump_vram

core:
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=pica EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)" EXTRA_WRAP_SYMS="$(WRAP_SYMS)"

# Presets and defaults.ini (docs/settings.md section 6): a changed default stops
# the build until every tested preset pins it or is retested.
# Systems with their own core (systems/<id>/core.mk) build it into a namespaced
# blob that borrows the PS1 dynarec's idle executable arena; the PS1 archive
# copy then exposes that arena and the linker profile gets each system's overlay.
CORE_SYSTEMS := $(patsubst systems/%/core.mk,%,$(wildcard $(SYSTEMS:%=systems/%/core.mk)))
ifneq ($(CORE_SYSTEMS),)
.PHONY: system-cores
system-cores: core
	python3 tools/build/core-blob.py $(CORE_SYSTEMS)
	python3 tools/build/ps1-arena.py
	python3 tools/build/system-linker.py $(SYSTEMS)
app: system-cores
endif

app: title-version-check core
	@python3 tools/presets/check.py
	$(MAKE) -C build/3ds

# Installable CIA. Needs makerom (Project_CTR) and bannertool on PATH,
# or MAKEROM=/path/to/makerom BANNERTOOL=/path/to/bannertool.
MAKEROM    ?= makerom
BANNERTOOL ?= bannertool
# Preserve released title versions; reject any semantic-field carry/alias.
# VERSION=N is an explicit private test counter, still checked as uint16 and
# verified against the packed TMD (stock makerom corrupts values such as 5136).
VERSION ?= $(shell python3 tools/build/title-version.py encode $(APP_VERSION_HEADER))

title-version-check:
	@python3 tools/build/title-version.py check $(APP_VERSION_HEADER) '$(VERSION)'

cia: app
	@python3 tools/build/update-release-check.py build/3ds/$(APP_OUTPUT).elf --private $(if $(UPDATE_DEVELOPMENT_PACKAGE),--development-package)
	$(BANNERTOOL) makebanner -i $(APP_META)/banner.png -a $(APP_META)/banner.wav -o build/3ds/$(APP_BUILD)/banner.bnr
	$(BANNERTOOL) makesmdh -s "$(APP_OUTPUT)" -l "$(APP_DESCRIPTION)" -p "BreadChaser" \
		-i $(APP_META)/icon.png -o build/3ds/$(APP_BUILD)/icon.icn
	python3 tools/art/smdh24.py build/3ds/$(APP_BUILD)/icon.icn $(APP_META)/icon24.png
	$(MAKEROM) -f cia -o build/3ds/$(APP_OUTPUT).cia -elf build/3ds/$(APP_OUTPUT).elf -rsf $(APP_META)/app.rsf \
		-icon build/3ds/$(APP_BUILD)/icon.icn -banner build/3ds/$(APP_BUILD)/banner.bnr -target t -ver $(VERSION)
	@python3 tools/build/title-version.py verify-cia build/3ds/$(APP_OUTPUT).cia '$(VERSION)' --title-id '$(APP_TITLE_ID)'

# The executable container inside the CIA. The CIA carries a randomized ticket, so two
# CIAs of one build differ; two CXIs of one commit are byte-identical (docs/building.md).
cxi: cia
	$(MAKEROM) -f cxi -o build/3ds/$(APP_OUTPUT).cxi -elf build/3ds/$(APP_OUTPUT).elf -rsf $(APP_META)/app.rsf \
		-icon build/3ds/$(APP_BUILD)/icon.icn -banner build/3ds/$(APP_BUILD)/banner.bnr -target t -ver $(VERSION)

clean:
	$(MAKE) -C core -f Makefile.libretro platform=ctr CTR_GPU=pica EXTRA_EXTERN_SYMS="$(EXTERN_SYMS)" clean
	$(MAKE) -C build/3ds clean

.PHONY: breadbox
breadbox: cia

# Explicit private emulator packaging. Never use this target for distribution.
.PHONY: cia-development release
cia-development:
	$(MAKE) cia UPDATE_DEVELOPMENT_PACKAGE=1
release: cia
	@python3 tools/build/update-release-check.py build/3ds/$(APP_OUTPUT).elf
