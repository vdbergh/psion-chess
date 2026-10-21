#ifndef PSION_CHESS_H
#define PSION_CHESS_H

#include <string>
#include <vector>

// Chess and UCI logic that does not depend on the emulator, so that it can
// be unit tested (see tests/).

namespace psion {

    // board[file][rank], 'a1' is board[0][0]. "PNBRQK" for White, lower
    // case for Black, '.' for empty.
    typedef char Board[8][8];

    // Decodes a square of Psion's board array; '?' for unknown codes.
    // Promoted pieces in particular get codes that are not known in
    // advance; LearnSquareCode() records what such a code stands for.
    char DecodeSquare(unsigned char v);
    void LearnSquareCode(unsigned char v, char piece);

    bool IsStartPosition(const Board board);

    // Derives the move (UCI notation) by which the side given by white
    // turns before into after.
    bool DeriveMove(const Board before, const Board after, bool white,
                    std::string &move);

    bool IsMoveString(const std::string &m);

    std::vector<std::string> Tokenize(const char *line);

    struct GoParams {
        int wtime, btime; // -1 if not given
        int winc, binc;
        int movestogo; // 0 if not given
        int movetime;  // 0 if not given
        bool infinite; // "infinite" or "ponder"
    };
    GoParams ParseGo(const std::vector<std::string> &tokens);

    // How long to think (ms) before pressing MOVE-NOW, allowing latency ms
    // for the move to be detected. 0 means until "stop".
    unsigned int ThinkTime(const GoParams &go, bool white,
                           unsigned int latency);

    struct PositionRequest {
        bool supported;
        std::vector<std::string> moves; // from the start position
        std::string error;              // why it is not supported
    };
    PositionRequest ParsePosition(const std::vector<std::string> &tokens);

    // Psion's analysis display (bottom row, in analysis mode): a score in
    // pawns from the side to move's point of view, then the principal
    // variation, e.g. "-00.4 E2-E4 D7-D5 B1-C3 D5xE4". A mate is shown as
    // 99.9 (or -99.9 when the side to move is mated).
    struct Analysis {
        bool mate;
        int score; // centipawns, or moves to mate (< 0: being mated)
        std::vector<std::string> pv; // UCI notation
        bool complete;               // all of the line was understood
    };
    // white: the side to move. False if the row is not an analysis line.
    bool ParseAnalysis(const std::string &row, bool white, Analysis &a);

    // Plays pv on board (white to move) to check it. False if a move is
    // not by a piece of the side to move, captures one of its own pieces,
    // is not the way the piece moves, or leaves the own king in check.
    // Psion does not show the piece promoted to; the first of q, n, r, b
    // with which the rest of the line is possible is added then.
    bool CheckPv(const Board board, bool white, std::vector<std::string> &pv);

    enum SyncAction {
        SYNC_DONE,      // game equals target
        SYNC_SEND_MOVE, // enter target[game.size()]
        SYNC_TAKEBACK,  // take back the last move
        SYNC_NEW_GAME   // start a new game, then replay the target
    };
    // The next step to get from the game Psion has played to the target.
    SyncAction PlanSync(const std::vector<std::string> &game,
                        const std::vector<std::string> &target);

} // namespace psion

#endif
