# Tests for the Psion adapter.
#
#   make check        unit tests of the chess and UCI logic (fast)
#   make integration  UCI tests against the real program (about a minute)

CXX ?= g++
CXXFLAGS ?= -g -O2 -Wall
SRC = ../src/psion_mods
PSION ?= ../psion

.PHONY: check integration clean

check: test_chess
	./test_chess

test_chess: test_chess.cpp $(SRC)/chess.cpp $(SRC)/chess.h
	$(CXX) $(CXXFLAGS) -I$(SRC) -o $@ test_chess.cpp $(SRC)/chess.cpp

integration:
	python3 -I uci_test.py $(abspath $(PSION))

clean:
	rm -f test_chess uci_test.log
