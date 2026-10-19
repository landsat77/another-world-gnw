# Another World (raw engine) for Game & Watch Retro-Go SD — GWHB homebrew.
#
#   make                  — build + pack → AnotherWorld.bin
#   make docker           — same build inside the Retro-Go SD builder image
#   make harness          — headless Linux test harness (harness/aw_host.c)
#
# SD card (Retro-Go SD v2.0.0 or later):
#   /homebrews/AnotherWorld.bin   (this homebrew)
#   /homebrews/AnotherWorld.aw    (game data: tools/aw_pack.py, same name)
#
# Verbose compiler lines: make V=

#######################################
# Project identity
#######################################
PROJECT_KIND := homebrew

CORE_NAME  := aw
CORE_ENTRY := app_main

HB_NAME    := Another World
PACKED_BIN := AnotherWorld.bin

RAW_SRC := src/raw
AW_PORT := src/port

# raw (NEO-RAW), the Another World virtual machine
RAW_CXX_SOURCES := \
$(RAW_SRC)/engine.cpp \
$(RAW_SRC)/file.cpp \
$(RAW_SRC)/mixer.cpp \
$(RAW_SRC)/parts.cpp \
$(RAW_SRC)/resource.cpp \
$(RAW_SRC)/serializer.cpp \
$(RAW_SRC)/sfxplayer.cpp \
$(RAW_SRC)/staticres.cpp \
$(RAW_SRC)/util.cpp \
$(RAW_SRC)/video.cpp \
$(RAW_SRC)/vm.cpp \
$(AW_PORT)/aw_core.cpp

CORE_CXX_SOURCES := $(RAW_CXX_SOURCES)

# Game & Watch side: heap, asset pack, retro-go glue
CORE_C_SOURCES := \
$(AW_PORT)/aw_alloc.c \
$(AW_PORT)/aw_pack.c \
$(AW_PORT)/aw_main.c

CORE_C_INCLUDES := -I$(AW_PORT) -I$(RAW_SRC)

CORE_C_DEFS := -DPROJECT_KIND_HOMEBREW=1 -DAW_DEVICE=1

OPT := -O2

GNW_CORE_SDK ?= sdk
BUILD_DIR ?= build/$(PROJECT_KIND)

include $(GNW_CORE_SDK)/Makefile

#######################################
# raw specific flags
#######################################
# The engine gets -DGNW and the glue header that redirects malloc/free to
# the game heap.
RAW_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(RAW_CXX_SOURCES:.cpp=.o)))
RAW_FLAGS := -DGNW -include $(AW_PORT)/gnw_aw_glue.h -std=gnu++11 \
	-Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
	-Wno-multichar -Wno-class-memaccess
$(RAW_OBJECTS): CXXFLAGS += $(RAW_FLAGS)
$(RAW_OBJECTS) $(C_OBJECTS): Makefile

#######################################
# Packed header version
#######################################
CORE_VERSION ?= $(shell git describe --tags --dirty 2>/dev/null || echo NOTAG)

#######################################
# Pack (GWHB)
#######################################
PACK_HOMEBREW := $(GNW_CORE_SDK)/tools/pack_homebrew.py
MAKE_COVER    := scripts/make_cover.py
COVER_SRC     := src/assets/cover.png
COVER_JPG     := $(BUILD_DIR)/cover.jpg

.PHONY: pack cover
cover: $(COVER_JPG)

# The cover art (src/assets/cover.png, 128x96: the game's box and a scene).
# Must stay <= 186x100 and <= 10 KiB (firmware cover cache).
$(COVER_JPG): $(MAKE_COVER) $(COVER_SRC) | $(BUILD_DIR)
	$(V)$(ECHO) "[ COVER ] $(COVER_JPG)"
	$(V)python3 $(MAKE_COVER) --in $(COVER_SRC) --out $(COVER_JPG)

pack: $(TARGET_BIN) $(COVER_JPG)
	$(V)$(ECHO) [ PACK GWHB ] $(PACKED_BIN) version=$(CORE_VERSION)
	$(V)python3 $(PACK_HOMEBREW) \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--name "$(HB_NAME)" --version "$(CORE_VERSION)" \
		--cover $(COVER_JPG) \
		--out $(PACKED_BIN)

all: pack

clean::
	$(V)rm -f $(PACKED_BIN) $(COVER_JPG)

# Read-only helpers for CI / scripts (make print-PACKED_BIN, etc.).
.PHONY: print-PROJECT_KIND print-PACKED_BIN print-CORE_NAME print-DOCKER_IMAGE \
	print-TARGET_ELF print-TARGET_MAP print-CORE_VERSION
print-PROJECT_KIND:
	@echo $(PROJECT_KIND)
print-PACKED_BIN:
	@echo $(PACKED_BIN)
print-CORE_NAME:
	@echo $(CORE_NAME)
print-DOCKER_IMAGE:
	@echo $(DOCKER_IMAGE)
print-TARGET_ELF:
	@echo $(TARGET_ELF)
print-TARGET_MAP:
	@echo $(BUILD_DIR)/$(CORE_NAME)_core.map
print-CORE_VERSION:
	@echo $(CORE_VERSION)

#######################################
# Headless Linux harness (same game + port code, virtual clock)
#######################################
.PHONY: harness
harness:
	$(MAKE) -C harness

#######################################
# Docker (same image as the firmware repo)
#######################################
.PHONY: docker docker_pull docker_shell

RELEASE_VERSION ?= v1.5
DOCKER_REPOSITORY ?= sylverb/retro-go-sd-builder
DOCKER_IMAGE ?= $(DOCKER_REPOSITORY):$(RELEASE_VERSION)

DOCKER_TTY_FLAG := $(shell if [ -t 0 ]; then echo -it; else echo; fi)
DOCKER_USER := $(shell id -u):$(shell id -g)
DOCKER_RUN := docker run --rm $(DOCKER_TTY_FLAG) \
	--user $(DOCKER_USER) \
	-v "$(CURDIR):/opt/workdir" \
	-w /opt/workdir \
	$(DOCKER_IMAGE)

docker:
	$(V)$(ECHO) "[ DOCKER ]" $(DOCKER_IMAGE)
	$(V)$(DOCKER_RUN) make --no-print-directory -j$$(nproc)

docker_pull:
	$(V)docker pull $(DOCKER_IMAGE)

docker_shell:
	$(DOCKER_RUN) bash
