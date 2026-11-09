#ifndef NODES_H
#define NODES_H

namespace psion {

    // Counts the nodes of Psion's search by watching the board it searches
    // on (see docs/search-internals.md). Install once Psion's data segment
    // is known; the count only grows, so a search takes the difference.
    void Nodes_Install(unsigned int data_segment);
    unsigned long long Nodes_Count();

    // Psion's principal variation, as 043E copies it for the analysis
    // display: the score (bias 8000, 24 per pawn, for the side to move)
    // and up to 8 moves (high byte the from square, low byte the to
    // square, as offsets on the search board; 0 ends the line). Each copy
    // is queued when its last word is written, with the iteration counter
    // (5A67) and the deepest ply searched since Nodes_ClearPv(), from the
    // ply counter (7076); Nodes_TakePv() returns the oldest one not yet
    // taken.
    struct MemoryPv {
        unsigned short score;
        unsigned short moves[8];
        unsigned char iteration;
        unsigned char max_ply;
    };
    bool Nodes_TakePv(MemoryPv &pv);
    // At the start of a search: forgets the lines queued and the deepest
    // ply.
    void Nodes_ClearPv();

    // Whether Psion is copying its board to the game board (DS:28AC, 64
    // bytes, copied whole when a move is played): a word of it has been
    // written, but not yet the last one.
    bool Nodes_BoardCopying();

} // namespace psion

#endif
