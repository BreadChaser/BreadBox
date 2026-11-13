# BreadBox out-of-tree CTR adapter for QuickNES. Upstream Makefile is retained
# as Makefile.upstream; source list and source contents are unchanged.
include $(CORE_DIR)/Makefile.common
CXX := $(DEVKITARM)/bin/arm-none-eabi-g++
AR := $(DEVKITARM)/bin/arm-none-eabi-ar
FLAGS := -O2 -DNDEBUG -D__LIBRETRO__ -DARM11 -D_3DS -DNO_UNALIGNED_ACCESS \
 -march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -mtp=soft \
 -mword-relocations -fomit-frame-pointer -fno-rtti -fno-exceptions \
 -ffunction-sections -fdata-sections -DGIT_VERSION=\"26bb785\"
OBJECTS := $(patsubst $(CORE_DIR)/%.cpp,%.o,$(SOURCES_CXX))
all: quicknes_libretro_ctr.a
quicknes_libretro_ctr.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(OBJECTS): %.o: $(CORE_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(INCFLAGS) -MMD -MP -c $< -o $@
-include $(OBJECTS:.o=.d)
