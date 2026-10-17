# Another World (raw) — headless Linux harness
#
#   make            → build/aw_host
#   make SAN=1      → with AddressSanitizer + UBSan
#
#   ./build/aw_host AnotherWorld.aw -t 20000 -e 1000 -o /tmp/aw
#
# Runs the same engine, core, heap and asset-pack code as the G&W homebrew
# with a virtual clock, scripted buttons, and PPM/WAV dumps (see aw_host.c).

BUILD_DIR = build
TARGET = $(BUILD_DIR)/aw_host

RAW = ../src/raw
PORT = ../src/port

RAW_SOURCES = \
	$(RAW)/engine.cpp \
	$(RAW)/file.cpp \
	$(RAW)/mixer.cpp \
	$(RAW)/parts.cpp \
	$(RAW)/resource.cpp \
	$(RAW)/serializer.cpp \
	$(RAW)/sfxplayer.cpp \
	$(RAW)/staticres.cpp \
	$(RAW)/util.cpp \
	$(RAW)/video.cpp \
	$(RAW)/vm.cpp \
	$(PORT)/aw_core.cpp

PORT_SOURCES = \
	$(PORT)/aw_alloc.c \
	$(PORT)/aw_pack.c \
	aw_host.c

SAN ?= 0
OPT ?= -O2 -g
ifeq ($(SAN),1)
OPT += -fsanitize=address,undefined -fno-omit-frame-pointer -DAW_HOST_SYSTEM_MALLOC
LDFLAGS += -fsanitize=address,undefined
endif

INCLUDES = -I$(PORT) -I$(RAW)
WARN = -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
CFLAGS = $(OPT) -std=gnu11 -DGNW $(WARN) $(INCLUDES)
CXXFLAGS = $(OPT) -std=gnu++11 -DGNW $(WARN) -Wno-multichar -Wno-class-memaccess \
	-fno-exceptions -fno-rtti $(INCLUDES) -include $(PORT)/gnw_aw_glue.h

RAW_OBJECTS = $(addprefix $(BUILD_DIR)/raw_,$(notdir $(RAW_SOURCES:.cpp=.o)))
PORT_OBJECTS = $(addprefix $(BUILD_DIR)/port_,$(notdir $(PORT_SOURCES:.c=.o)))

vpath %.cpp $(sort $(dir $(RAW_SOURCES)))
vpath %.c $(sort $(dir $(PORT_SOURCES)))

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/raw_%.o: %.cpp | $(BUILD_DIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/port_%.o: %.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(TARGET): $(RAW_OBJECTS) $(PORT_OBJECTS)
	$(CXX) $^ $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
