# Psion Chess (1985) as a UCI engine, running on a headless DOSBox.
#
#   make              build ./psion
#   make check        unit tests
#   make integration  UCI tests against ./psion (about two minutes)
#   make windows      build ./psion.exe for Windows (needs MinGW-w64);
#                     "make check TARGET=windows" etc. test it under Wine
#   make install      download Psion's files to $(PREFIX)/share/psion-chess
#                     (after asking) and install psion in $(PREFIX)/bin
#   make uninstall    remove all of that again

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

# The adapter's version for --version: the git tag, plus the commits since
# it, e.g. v1.18-3-g2d7a9cd. version.h is rewritten only when it changes.
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo unknown)
VERSION_H = $(BUILD)/version.h

# For make install. PSION_DATADIR is compiled in: where an installed psion
# looks for Psion's files. datadir.stamp is rewritten only when it changes,
# so that another PREFIX rebuilds psionfiles.o.
PREFIX ?= /usr/local
DATADIR = $(PREFIX)/share/psion-chess
DOCDIR = $(PREFIX)/share/doc/psion-chess
DATADIR_STAMP = $(BUILD)/datadir.stamp

.PHONY: all windows check integration install uninstall clean FORCE

all: $(EXE)

windows:
	$(MAKE) TARGET=windows

$(EXE): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(VERSION_H): FORCE
	@mkdir -p $(dir $@)
	@echo '#define PSION_VERSION "$(VERSION)"' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(BUILD)/src/psion_mods/psionmain.o: $(VERSION_H)
$(BUILD)/src/psion_mods/psionmain.o: override CPPFLAGS += -I$(BUILD)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

ifneq ($(TARGET),windows)
$(DATADIR_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(DATADIR)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(BUILD)/src/psion_mods/psionfiles.o: $(DATADIR_STAMP)
$(BUILD)/src/psion_mods/psionfiles.o: override CPPFLAGS += \
	-DPSION_DATADIR='"$(DATADIR)"'
endif

-include $(OBJS:.o=.d)

check:
	$(MAKE) -C tests check TARGET=$(TARGET)

integration: $(EXE)
	$(MAKE) -C tests integration TARGET=$(TARGET)

# First Psion's files, with "psion -d": if they are declined (or the
# download fails), nothing is installed. With DESTDIR (staging, e.g. for a
# package) they are not downloaded: a package should not contain them.
install: $(EXE)
ifeq ($(TARGET),windows)
	@echo "make install is for Linux only." >&2; exit 1
else
ifeq ($(DESTDIR),)
	install -d $(dir $(DATADIR))
	@PSION_DIR='$(DATADIR)' ./$(EXE) -d || \
	  { echo "Nothing installed."; exit 1; }
endif
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(DATADIR) \
		$(DESTDIR)$(DOCDIR)/docs $(DESTDIR)$(DOCDIR)/data
	install -m 755 $(EXE) $(DESTDIR)$(PREFIX)/bin/$(EXE)
	install -m 644 README.md COPYING AUTHORS $(DESTDIR)$(DOCDIR)/
	install -m 644 docs/*.md $(DESTDIR)$(DOCDIR)/docs/
	install -m 644 data/README.md data/LICENCE.md $(DESTDIR)$(DOCDIR)/data/
endif

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(EXE)
	rm -rf $(DESTDIR)$(DATADIR) $(DESTDIR)$(DOCDIR)

clean:
	rm -rf build build-windows psion psion.exe
	$(MAKE) -C tests clean
