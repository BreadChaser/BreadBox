APP_OUTPUT := BreadBox
APP_DESCRIPTION := BreadBox - PS1 and GBA emulator for 3DS
APP_META := apps/breadbox
APP_BUILD := build-breadbox
APP_VERSION_HEADER := apps/breadbox/version.h
APP_TITLE_ID := 000400000B0B0B00

SYSTEMS := ps1 gba

# Experimental shared-host conformance core; excluded from shipping targets.
ifeq ($(BB_EXPERIMENTAL_NES),1)
SYSTEMS += nes
endif
