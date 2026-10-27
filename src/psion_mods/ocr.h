#ifndef OCR_H
#define OCR_H

#include "psion.h"

namespace psion {

    void init_ocr();
    // Reads the 80x25 text of a frame; rows that are no text read as spaces.
    void ocr(char txt_buf[TEXT_HEIGHT][TEXT_WIDTH], const unsigned char *frame);
    // Reads the pieces of a frame: board[file][rank] as in PsionState, '*'
    // for a square that shows nothing known.
    void ocr_board(char board[8][8], const unsigned char *frame);

} // namespace psion
#endif
