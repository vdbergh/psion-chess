#ifndef PSION_H
#define PSION_H

#include <cstdio>
#include <queue>
#include <string>

#include "shell.h"
#include "keyboard.h"

namespace psion {
    const int SCREEN_WIDTH=640;
    const int SCREEN_HEIGHT=400;
    const int TEXT_WIDTH=80;
    const int TEXT_HEIGHT=25;
    const int SCREEN_BPP=4;

    // Game state read directly from Psion's memory. board uses the same
    // convention as ocr_board(): board[file][rank], 'a1' is board[0][0],
    // "PNBRQK" for White, lower case for Black, '.' for empty.
    struct PsionState {
	char board[8][8];         // '?' where the code is not known
	unsigned char codes[8][8];  // Psion's square codes
	int unknown;              // number of '?' squares
	bool white_to_move;
	int ply;                  // half-moves played since the start
	bool pondering;           // engine is searching its predicted reply
	unsigned int segment;     // Psion's data segment
    };
    // Reliable while the engine is idle or pondering. While it searches
    // its own move the board is still correct, but the side to move (and
    // so ply and pondering) is not. Returns false if the data segment
    // cannot be found.
    bool ReadGameState(PsionState &state);

    void ProcessConsole();
    void ProcessScreen(unsigned char pixels[SCREEN_BPP*SCREEN_WIDTH*SCREEN_HEIGHT]);

    // Diagnostics on stderr in UCI mode (--verbose, or "debug on").
    void SetVerbose(bool on);
    bool Verbose();
    // Appends text to pending and writes the complete lines in it to out.
    void WriteLines(FILE *out, std::string &pending, const char *text);

    bool Psion_Init(DOS_Shell *d);
    void IO_Init();
    void ocr(char txt_buf[TEXT_HEIGHT][TEXT_WIDTH],unsigned char* buf);
    void print_pixels(unsigned char * buf, int hoffset);
    void print_txt(char txt_buf[TEXT_HEIGHT][TEXT_WIDTH]);
    void ocr_board(char board[8][8], unsigned char *buf);
    void print_board(char board[8][8]);
    void print_pixels(unsigned char * buf, int hoffset);

}

#endif
