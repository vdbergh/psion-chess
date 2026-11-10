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
    // At the start of a search: forgets the lines queued, the deepest ply
    // and the iteration counter.
    void Nodes_ClearPv();
    // The iteration counter (5A67) as Psion last wrote it since
    // Nodes_ClearPv(); 0 before that.
    unsigned char Nodes_Iteration();

    // For time management (docs/time-management.md). Once the current
    // iteration has shown its first line (the previous best move searched
    // again): true, with drop the score of the previous iteration's last
    // line minus that of this one (units, 24 per pawn), unless one of
    // them is a mate.
    bool Nodes_FirstLineDrop(int &drop);
    // Whether the root search failed low (DS:713A, alpha, set to 0) and
    // has not shown a line since: Psion would still play the move that
    // failed low.
    bool Nodes_FailLowOpen();

    // Whether Psion is copying its board to the game board (DS:28AC, 64
    // bytes, copied whole when a move is played): a word of it has been
    // written, but not yet the last one.
    bool Nodes_BoardCopying();

} // namespace psion

#endif
