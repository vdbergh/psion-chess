# Tests for the Psion adapter.
#
#   make check        unit tests of the chess and UCI logic (fast)
#   make integration  UCI tests against the real program (about two minutes)
#
# With TARGET=windows, the tests are built for Windows and run under Wine.

TARGET ?= linux
CXXFLAGS ?= -g -O2 -Wall
SRC = ../src/psion_mods

ifeq ($(TARGET),windows)
CXX = i686-w64-mingw32-g++
TEST_CHESS = test_chess.exe
RUN = wine
PSION ?= wine $(abspath ../psion.exe)
override LDFLAGS += -static
else
TEST_CHESS = test_chess
RUN =
PSION ?= $(abspath ../psion)
endif

.PHONY: check integration clean

check: $(TEST_CHESS)
	$(RUN) ./$(TEST_CHESS)

$(TEST_CHESS): test_chess.cpp $(SRC)/chess.cpp $(SRC)/chess.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -I$(SRC) -o $@ test_chess.cpp $(SRC)/chess.cpp

integration:
	python3 -I uci_test.py "$(PSION)"

clean:
	rm -f test_chess test_chess.exe uci_test.log
