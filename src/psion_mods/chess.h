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

    // The position a game starts from: the start position, or one set up
    // in Psion's set-up mode (which gives neither side castling rights).
    struct StartPosition {
        bool standard;
        Board board; // if not standard
        bool white;  // to move
    };
    StartPosition StandardStart();
    bool SameStart(const StartPosition &a, const StartPosition &b);

    struct PositionRequest {
        bool supported;
        StartPosition start;
        std::vector<std::string> moves; // from start
        std::string error;              // why it is not supported
    };
    // A FEN position with an en passant capture is set up before the
    // double pawn move, which is then the first of moves.
    PositionRequest ParsePosition(const std::vector<std::string> &tokens);

    // The keys that set up board in Psion's set-up mode, with white to
    // move, once it is in that mode: 'e' (empty board), the cursor keys
    // 'U', 'D', 'L', 'R', a piece letter ("kqrbnp") to place a piece of
    // the colour being set up, 'c' to change that colour. colour_white:
    // the colour at the start, with the cursor on a2 (White) or a7.
    std::string SetupKeys(const Board board, bool white, bool colour_white);

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
