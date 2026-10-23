#---------------------------------------------------------------------------------
# Crash Bandicoot: Warped (PSX) -> Nintendo 3DS homebrew port
#
# Build (devkitPro + devkitARM + 3ds-dev):
#   export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
#   make
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TARGET   := crashwarped3ds
BUILD    := build
SOURCES  := source
DATA     := game_data

#---------------------------------------------------------------------------------
# Build variants:
#   make                  -> crashwarped3ds.3dsx
#                            (the 16 MB level blob is embedded in the .3dsx)
#   make LEVELS_FROM_SD=1 -> crashwarped3ds_sd.3dsx
#                            (no blob in the .3dsx, the levels are read at
#                             start-up from sdmc:/3ds/crashwarped3ds/levels.bin)
#---------------------------------------------------------------------------------
LEVELS_FROM_SD ?= 0

ifeq ($(strip $(LEVELS_FROM_SD)),1)
TARGET   := crashwarped3ds_sd
# a separate object directory: the objects are compiled with different
# CFLAGS, so they must not be shared between the two variants
BUILD    := build_sd
endif

PREFIX   := arm-none-eabi-
CC       := $(DEVKITARM)/bin/$(PREFIX)gcc
LD       := $(CC)
AS       := $(DEVKITARM)/bin/$(PREFIX)as
STRIP    := $(DEVKITARM)/bin/$(PREFIX)strip

CTRULIB  := $(DEVKITPRO)/libctru

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -O3 -ffast-math -fno-finite-math-only -mword-relocations -ffunction-sections $(ARCH) \
          -I$(SOURCES) -I$(BUILD) -I$(CTRULIB)/include -D__3DS__ $(CFLAGS_EXTRA)

# Asset and collision validation must continue to reject NaN and infinity.
# Override fast-math's finite-only assumption while retaining its other optimizations.

# "levels from the SD card" variant: the level code is compiled so that it
# reads the blob from the SD card instead of the embedded copy.
ifeq ($(strip $(LEVELS_FROM_SD)),1)
CFLAGS += -DLEVELS_FROM_SD
# ... and the embedded copy (the 16 MB .incbin) is not linked in at all.
LEVELS_BLOB_OBJ :=
else
LEVELS_BLOB_OBJ := $(BUILD)/levels_bin.o
endif

ASFLAGS := -g $(ARCH)

LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections

LIBS     := -lcitro2d -lcitro3d -lctru -lm
LIBPATHS := -L$(CTRULIB)/lib

CFILES := $(wildcard $(SOURCES)/*.c)
OFILES := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean

all: $(TARGET).3dsx

$(BUILD):
	@mkdir -p $(BUILD)

#---------------------------------------------------------------------------------
# C sources
#---------------------------------------------------------------------------------
$(BUILD)/%.o: $(SOURCES)/%.c Makefile | $(BUILD)
	@$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD)/$*.d -c $< -o $@

#---------------------------------------------------------------------------------
# GPU shader
#---------------------------------------------------------------------------------
$(BUILD)/shader.shbin: $(SOURCES)/shader.v.pica | $(BUILD)
	@echo $(notdir $<)
	@picasso -o $@ $<

$(BUILD)/shader_shbin.s: $(BUILD)/shader.shbin
	@bin2s -a 4 $< > $@

$(BUILD)/shader_shbin.o: $(BUILD)/shader_shbin.s
	@$(CC) -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

$(BUILD)/world_shader.shbin: $(SOURCES)/world_shader.v.pica | $(BUILD)
	@picasso -o $@ $<

$(BUILD)/world_shader_shbin.s: $(BUILD)/world_shader.shbin
	@bin2s -a 4 $< > $@

$(BUILD)/world_shader_shbin.o: $(BUILD)/world_shader_shbin.s
	@$(CC) -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

$(OFILES): $(SOURCES)/shader_shbin.h $(SOURCES)/levels_bin.h

#---------------------------------------------------------------------------------
# embedded level data (the .incbin stub lives in source/levels_bin.s)
#---------------------------------------------------------------------------------
$(BUILD)/levels_bin.o: $(SOURCES)/levels_bin.s $(DATA)/levels.bin | $(BUILD)
	@echo $(notdir $<)
	@$(CC) -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

#---------------------------------------------------------------------------------
# link
#---------------------------------------------------------------------------------
$(TARGET).elf: $(OFILES) $(BUILD)/shader_shbin.o $(BUILD)/world_shader_shbin.o $(LEVELS_BLOB_OBJ)
	$(LD) $(LDFLAGS) $^ $(LIBPATHS) $(LIBS) -o $@

$(TARGET).smdh: icon.png
	@smdhtool --create "Crash Bandicoot: Warped" \
	          "Crash Bandicoot Warped (PS1) ported to the Nintendo 3DS" \
	          "Crash 3DS port" $< $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	@3dsxtool $< $@ --smdh=$(TARGET).smdh
	@echo built $(notdir $@)

clean:
	@echo clean ...
	@rm -fr $(BUILD) build_sd $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf
	@rm -fr crashwarped3ds.3dsx crashwarped3ds.smdh crashwarped3ds.elf \
	       crashwarped3ds_sd.3dsx crashwarped3ds_sd.smdh crashwarped3ds_sd.elf

-include $(BUILD)/*.d
