# Psion Chess (1985) as a UCI engine, running on a headless DOSBox.
#
#   make              build ./psion
#   make check        unit tests
#   make integration  UCI tests against ./psion (about two minutes)
#   make windows      build ./psion.exe for Windows (needs MinGW-w64);
#                     "make check TARGET=windows" etc. test it under Wine

# Linux, or "windows" for a 32-bit Windows executable built with MinGW-w64.
# (The 64-bit recompiler of this DOSBox version only supports the Linux
# calling convention; the 32-bit one works on Windows.)
TARGET ?= linux

ifeq ($(TARGET),windows)
CXX = i686-w64-mingw32-g++
BUILD = build-windows
EXE = psion.exe
CONFIG_DIR = windows
# Statically linked, so that no MinGW DLLs are needed.
override LDFLAGS += -static
LIBS = -lwinmm
else
BUILD = build
EXE = psion
CONFIG_DIR = .
# The 64-bit recompiler needs a non-PIE executable.
override LDFLAGS += -no-pie
LIBS =
endif

# The DOSBox sources predate C++11. These flags are kept even when CXXFLAGS
# etc. are given on the command line, e.g. "make LDFLAGS=-static" for an
# executable that runs on any x86-64 Linux.
CXXFLAGS ?= -g -O2
override CXXFLAGS += -std=gnu++98 -mno-ms-bitfields
override CPPFLAGS += -DHAVE_CONFIG_H -I$(CONFIG_DIR) -Iinclude

SRCS := src/dosbox.cpp $(wildcard src/*/*.cpp)
OBJS := $(SRCS:%.cpp=$(BUILD)/%.o)

.PHONY: all windows check integration clean

all: $(EXE)

windows:
	$(MAKE) TARGET=windows

$(EXE): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

-include $(OBJS:.o=.d)

check:
	$(MAKE) -C tests check TARGET=$(TARGET)

integration: $(EXE)
	$(MAKE) -C tests integration TARGET=$(TARGET)

clean:
	rm -rf build build-windows psion psion.exe
	$(MAKE) -C tests clean
