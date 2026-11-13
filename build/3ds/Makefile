#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
ifneq ($(filter-out breadbox,$(APP)),)
$(error APP=$(APP) no longer exists: PSX3D is retired)
endif
include $(TOPDIR)/../../apps/breadbox.mk
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	$(APP_OUTPUT)
ICON		:=	../../$(APP_META)/icon.png
APP_TITLE	:=	$(APP_OUTPUT)
APP_AUTHOR	:=	BreadChaser
COREDIR		:=	$(TOPDIR)/../../core
CORELIB		:=	$(COREDIR)/pcsx_rearmed_libretro_ctr.a
BUILD		:=	$(APP_BUILD)
SOURCES		:=	../../app/ui ../../app/settings ../../app/services ../../platform/3ds $(addprefix ../../systems/,$(SYSTEMS))
SOURCES += ../../systems/common $(wildcard $(SYSTEMS:%=../../build/systems/%))
DATA		:=	data
INCLUDES	:=	include
GRAPHICS	:=	../../assets/gfx
ROMFS		:=	../../assets/romfs
GFXBUILD	:=	$(ROMFS)/gfx

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations -fomit-frame-pointer -mfpu=vfp \
			-ffunction-sections \
			$(ARCH)

CFLAGS += -I$(TOPDIR)/../../systems/common
CFLAGS += -DPICA_LAYOUT_NAME=\"split\"

CFLAGS	+=	$(INCLUDE) -D__3DS__ $(EXTRA_DEFS) -I$(COREDIR)/deps/libretro-common/include
# New 3DS per-game PS1 clock presets (versioned in romfs/presets).
CFLAGS	+=	-DPSX3D_PS1_OVERCLOCK_PRESETS

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
# Core first, then libraries, then the frontend (core-first.ld): frontend
# changes never move the core. Check: tools/layout/check.sh.
PICA_SPECS := $(TOPDIR)/core-first.specs
ifneq ($(wildcard $(SYSTEMS:%=$(TOPDIR)/../../systems/%/core.mk)),)
PICA_SPECS := $(TOPDIR)/breadbox.specs
endif
LDFLAGS	=	-specs=$(PICA_SPECS) -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LDFLAGS += -Wl,--wrap=GSPGPU_FlushDataCache,--wrap=C3D_DrawElements,--wrap=C3D_GetCmdBufUsage,-z,noexecstack
LDFLAGS += -Wl,--wrap=fread
# save states and memory cards through PSX3D (statefile.c, emu.c)
LDFLAGS += -Wl,--wrap=retro_serialize,--wrap=retro_unserialize,--wrap=LoadMcd
LDFLAGS += -Wl,--wrap=C3D_UpdateUniforms,--wrap=C3Di_LoadShaderUniforms,--wrap=C3Di_DirtyUniforms
LDFLAGS += -Wl,--wrap=C3D_DrawArrays,--wrap=GX_TextureCopy
# Real 3D draw-order layers (gpustereo_layers.c) hook into the existing code
LDFLAGS += -Wl,--wrap=gs_shift,--wrap=gs_core_frame,--wrap=gs_core_alloc,--wrap=gs_core_clear,--wrap=gs_core_free
LDFLAGS += -Wl,--wrap=gpustereo_status,--wrap=pica_picture,--wrap=gs_flat

# Keep rc9 archive members after replacing the old image backdrop. Their
# removal would shift gameplay library code on Old 3DS. Never executed here.
LDFLAGS += -Wl,-u,Tex3DS_TextureImportStdio,-u,Tex3DS_GetSubTexture,-u,Tex3DS_TextureFree,-u,fmodf

# Systems with their own core (systems/<id>/core.mk, built by the root
# Makefile's system-cores): their namespaced blobs, headers and libraries, the
# PS1 archive copy that exposes the shared executable arena, the PS1 handoff
# diagnostics and the linker profile with each system's arena overlay.
CORE_SYSTEMS := $(patsubst $(TOPDIR)/../../systems/%/core.mk,%,$(wildcard $(SYSTEMS:%=$(TOPDIR)/../../systems/%/core.mk)))
include $(CORE_SYSTEMS:%=$(TOPDIR)/../../systems/%/core.mk)
CORE_BLOBS := $(foreach s,$(CORE_SYSTEMS),$(TOPDIR)/../../build/systems/$(s)/$(s)_core_blob.o)
ifneq ($(CORE_SYSTEMS),)
LDFLAGS += -Wl,--wrap=retro_init,--wrap=retro_deinit
# Blocks of <=64 bytes come from a fixed pool (app/ui/bb_smallheap.c) so they
# can never split the heap between systems' big blocks.
LDFLAGS += -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc
# The PS1 teardown skips GPU work once the app is closing (app/ui/breadbox_handoff.c).
LDFLAGS += -Wl,--wrap=retro_unload_game,--wrap=video_game_end
CORELIB := $(TOPDIR)/../../build/systems/ps1/pcsx_rearmed_libretro_ctr.a
SYSTEM_LDSCRIPT := $(TOPDIR)/core-first-breadbox.ld
CFLAGS += $(foreach s,$(CORE_SYSTEMS),$(addprefix -I$(TOPDIR)/../../,$($(s)_CORE_INCLUDES)))
endif

LIBS	:= $(CORELIB) -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpng16 -lz -lctru -lm
ifneq ($(CORE_SYSTEMS),)
LIBS += $(CORE_BLOBS) -lctru $(foreach s,$(CORE_SYSTEMS),$($(s)_CORE_LIBS)) -lz -lm
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(foreach s,$(CORE_SYSTEMS),$(addprefix $(TOPDIR)/../../,$($(s)_CORE_LIBDIRS))) $(CTRULIB) $(PORTLIBS)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
.DEFAULT_GOAL := all
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

SYSTEM_TABLE := $(CURDIR)/$(BUILD)/systems_table.c
$(shell mkdir -p $(CURDIR)/$(BUILD))
$(shell python3 ../../tools/build/systems-table.py $(SYSTEM_TABLE) $(SYSTEMS))

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
# Fixed link order (link-order.txt): moving a file between folders or building
# under another locale leaves the code layout unchanged; unlisted files follow.
LINK_ORDER := $(shell grep -E '^[a-z0-9_]+\.c$$' $(TOPDIR)/link-order.txt)
CFILES := $(foreach f,$(LINK_ORDER),$(filter $(f),$(CFILES))) $(sort $(filter-out $(LINK_ORDER),$(CFILES)))
CFILES += systems_table.c
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
# Reject collisions across C, C++ and assembly before VPATH selects one.
SOURCE_BASENAMES := $(basename $(CFILES) $(CPPFILES) $(SFILES))
DUPLICATE_SOURCES := $(sort $(foreach f,$(SOURCE_BASENAMES),$(if $(word 2,$(filter $(f),$(SOURCE_BASENAMES))),$(f))))
ifneq ($(strip $(DUPLICATE_SOURCES)),)
$(error Duplicate source basenames: $(DUPLICATE_SOURCES))
endif
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

# picadirect.o, presetupdate.o, uploadskip.o, display.o and boxart.o link after
# every other frontend object, so they move no existing frontend code
# (docs/building.md); presetupdate.o and boxart.o run only in the idle
# library, display.o once per picture. Share PSX3D (qrcode.o, share.o, mp_ui.o)
# and playlog.o (library play history and favourites) follow; the save code
# links last. cmdroom.o's code is PSX3D_COLD (after everything);
# gpustereo_layers.o (New 3DS 3D) comes last.
LATE_OFILES := picadirect.o presetupdate.o uploadskip.o display.o boxart.o qrcode.o share.o mp_ui.o playlog.o history.o safefile.o saveid.o savestates.o statefile.o scene.o icons.o cmdroom.o gpustereo_layers.o
export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(filter-out $(LATE_OFILES),$(CFILES:.c=.o)) $(SFILES:.s=.o) $(LATE_OFILES)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/../../app/ui -I$(CURDIR)/../../include/breadbox -I$(CURDIR)/../../platform/3ds -I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

#---------------------------------------------------------------------------------

# A converted graphic is stale when the PNG its .t3s names changes, not only the .t3s. (tex3ds -d
# writes the PNG into a .d file, but only the inner make reads those and it converts nothing.)
ifneq ($(GFXBUILD),$(BUILD))
$(foreach t,$(GFXFILES),$(eval $(GFXBUILD)/$(t:.t3s=.t3x) $(BUILD)/$(t:.t3s=.h) : $(addprefix $(CURDIR)/$(GRAPHICS)/,$(filter %.png,$(shell cat $(CURDIR)/$(GRAPHICS)/$(t))))))
endif

all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD)

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES) $(CORELIB) $(CORE_BLOBS) $(SYSTEM_LDSCRIPT) $(TOPDIR)/core-first.ld $(TOPDIR)/core-first.specs

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x %.shbin
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.shbin.o %_shbin.h : %.shbin
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

# Kept from the presets round: the frontend's code layout (not the core's)
# with it is the one measured on the Old 3DS (docs/building.md).
presets.o: CFLAGS += -Os
# save names: runs once per game start, kept small
saveid.o: CFLAGS += -Os
# preset updates: once a day in the library, kept small
presetupdate.o: CFLAGS += -Os
# the core's state layout (statefile.c reads core headers)
statefile.o: CFLAGS += -I$(COREDIR)/include

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------

# Diagnostic only; ordinary and release builds contain no trace or new wrappers.
ifeq ($(HT_TRACE),1)
CFLAGS += -DHT_TRACE -include $(TOPDIR)/../../app/ui/ht_calls.h
endif

# Remote reports identify the actual source revision. Recompile the small remote
# status module on each build so committing/changing sources cannot leave an old ID.
# In the container (no .git) tools/build/container.sh passes the host's describe as PSX3D_DESCRIBE.
.PHONY: build-identity-force
build-identity-force:
devcontrol.o: build-identity-force
devcontrol.o: CFLAGS += -DBB_SOURCE_REV=\"$(or $(shell git -C $(TOPDIR)/../.. describe --always --abbrev=12 --dirty 2>/dev/null),$(PSX3D_DESCRIBE),unversioned)\"
