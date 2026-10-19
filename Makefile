# Psion Chess (1985) as a UCI engine, running on a headless DOSBox.
#
#   make              build ./psion
#   make check        unit tests
#   make integration  UCI tests against ./psion (about a minute)

CXX ?= g++
# The DOSBox sources predate C++11, and its 64-bit recompiler needs a
# non-PIE executable. These flags are kept even when CXXFLAGS etc. are
# given on the command line, e.g. "make LDFLAGS=-static" for an executable
# that runs on any x86-64 Linux.
CXXFLAGS ?= -g -O2
override CXXFLAGS += -std=gnu++98 -mno-ms-bitfields
override CPPFLAGS += -DHAVE_CONFIG_H -I. -Iinclude
override LDFLAGS += -no-pie

SRCS := src/dosbox.cpp $(wildcard src/*/*.cpp)
OBJS := $(SRCS:%.cpp=build/%.o)

.PHONY: all check integration clean

all: psion

psion: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS)

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

-include $(OBJS:.o=.d)

check:
	$(MAKE) -C tests check

integration: psion
	$(MAKE) -C tests integration

clean:
	rm -rf build psion
	$(MAKE) -C tests clean
