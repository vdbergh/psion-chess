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
    // Promoted Black pieces have codes of their own until they move
    // (queen D3, bishop 98, knight E0; a rook gets 8A). While Psion's
    // promotion prompt is up, it cycles the square through 08, 10, 18 and
    // 20 (+80 for Black), and the last of these can stay until the board
    // is redrawn. So 98 is '?' here: it can be either.
    char DecodeSquare(unsigned char v);
    // Decodes a square of Psion's search board (low byte of the word),
    // which has no prompt codes: 98 is a promoted Black bishop.
    char DecodeSearchSquare(unsigned char v);

    bool IsStartPosition(const Board board);

    // Derives the move (UCI notation) by which the side given by white
    // turns before into after.
    bool DeriveMove(const Board before, const Board after, bool white,
                    std::string &move);

    bool IsMoveString(const std::string &m);

    std::vector<std::string> Tokenize(const char *line);

    struct GoParams {
        // All values are clamped to 0 .. INT_MAX: a negative clock (the
        // time used up) counts as 0. depth and nodes, if given, are at
        // least 1 (0 means not given); mate is not 0 if given, and
        // negative for being mated.
        bool wtime_given, btime_given;
        int wtime, btime;
        int winc, binc;
        int movestogo; // 0 if not given
        bool movetime_given;
        int movetime;
        bool infinite; // until "stop"
        bool ponder;   // until "ponderhit" or "stop"
        int depth;     // 0 if not given
        int nodes;     // 0 if not given
        int mate;      // moves, < 0: being mated; 0 if not given
    };
    GoParams ParseGo(const std::vector<std::string> &tokens);

    // How long to think (ms) before pressing MOVE-NOW, allowing overhead
    // ms per move for everything else (the move being detected, the GUI,
    // the network). 0 means no limit in time: until "stop", or until the
    // depth, nodes or mate given are reached.
    unsigned int ThinkTime(const GoParams &go, bool white,
                           unsigned int overhead);

    // The time for the move with a clock, in ms, before the overhead is
    // taken off: remaining/n + 3/4 increment, at most 3/4 of the
    // remaining time. -1 without a clock, with movetime or infinite.
    long long ClockTime(const GoParams &go, bool white);

    // Time management with a clock (docs/time-management.md), tuned with
    // temporary UCI options. Shares in percent, the drop in Psion's units
    // (24 per pawn). The defaults leave ThinkTime() as it is.
    struct TimeParams {
        int target;             // c: target S = c * ClockTime()
        int soft_stop;          // x: no new iteration after x * S
        int drop;               // d: a drop of the first move's score of
                                // at least d units ...
        int drop_extension;     // e_drop: ... extends S by this
        int fail_low_extension; // e_low: a fail low not yet resolved
                                // extends S by this
        int max;                // m: at most m * ClockTime() ...
        int max_share;          // r: ... and r * the remaining time
        int min_moves;          // N: at the target, an iteration that has
                                // searched its first root move goes on
                                // until it has searched N (0: off)
        int min_moves_window;   // 1: ... and until a move has come back
                                // inside the window (0: off)
    };
    TimeParams DefaultTimeParams();

    // The think time (as from ThinkTime()) for percent of the target S,
    // capped at the maximum, both from allocated = ClockTime() and the
    // remaining time.
    unsigned int ScaledThinkTime(long long allocated, long long remaining,
                                 unsigned int overhead, const TimeParams &tp,
                                 int percent);

    // The maximum H (as from ThinkTime()): the cap of ScaledThinkTime().
    unsigned int MaxThinkTime(long long allocated, long long remaining,
                              unsigned int overhead, const TimeParams &tp);

    // The extension of S in percent (100: none): e_drop if the first
    // line of the current iteration came back at least d units below
    // the last of the previous one (drop_known: both lines are there
    // and neither is a mate), e_low while a fail low has not yet given
    // a line; the larger if both.
    int ExtensionPercent(const TimeParams &tp, bool drop_known, int drop,
                         bool fail_low_open);

    // Psion thinks at least this long, also when the time for the move
    // was used up before it could start (e.g. setting up the position).
    const unsigned int MIN_THINK_MS = 50;
    // At most this much of the time still needed after "go" to set up the
    // position comes out of the time for the move: a longer setup (a FEN,
    // a replay) would leave hardly any time to think.
    const unsigned int MAX_SETUP_MS = 200;

    // When to press MOVE-NOW, in ms from "go" (or "ponderhit"), with
    // think_ms from ThinkTime() and Psion thinking from setup_ms on:
    // setup + max(think - min(setup, MAX_SETUP_MS), MIN_THINK_MS).
    unsigned int MoveNowTime(unsigned int think_ms, unsigned int setup_ms);

    // UCI_Elo: the strength on the CCRL Blitz scale (2'+1"), from
    // MIN_ELO to MAX_ELO, and the fixed speed (cycles per ms) that gives
    // it.
    const int MIN_ELO = 1150;
    const int MAX_ELO = 1650;
    const int DEFAULT_ELO = 1400;
    unsigned int EloCycles(int elo);

    // "setoption name NAME [value VALUE]": name and value may contain
    // spaces. False if there is no name.
    bool ParseSetOption(const std::vector<std::string> &tokens,
                        std::string &name, std::string &value);

    // Case-insensitive, as UCI option names are.
    bool SameOptionName(const std::string &a, const std::string &b);

    // The position a game starts from: the start position, or one set up
    // in Psion's set-up mode.
    struct StartPosition {
        bool standard;
        Board board;          // if not standard
        bool white;           // to move
        std::string castling; // if not standard: "KQkq", a part, or "-"
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

    // "CHECKMATE" or "STALEMATE" if the game is over by the rules after
    // moves from start, else "" (also if a move is illegal).
    std::string GameOver(const StartPosition &start,
                         const std::vector<std::string> &moves);

    // The keys that set up start in Psion's set-up mode, once it is in
    // that mode in a new game: 'e' (empty board), '0' (empty the square),
    // the cursor keys 'U', 'D', 'L', 'R', a piece letter ("kqrbnp") to
    // place a piece of the colour being set up, 'c' to change that colour.
    // colour_white: the colour at the start, with the cursor on a2 (White)
    // or a7. A king or rook that is not typed keeps its castling rights.
    std::string SetupKeys(const StartPosition &start, bool colour_white);

    // A line of Psion's analysis: its score, from the side to move's point
    // of view, and principal variation.
    struct Analysis {
        bool mate;
        int score; // centipawns, or moves to mate (< 0: being mated)
        std::vector<std::string> pv; // UCI notation
        int depth;    // nominal depth of the iteration, in plies
        int seldepth; // deepest ply searched so far
    };
    // The line from Psion's memory (see Nodes_TakePv()): score with bias
    // 8000 and 24 per pawn, moves as from and to squares on the search
    // board, at most 8, ending at a 0. The score is not rounded to tenths
    // of a pawn as on Psion's analysis display. A mate score gives the
    // moves to the mate. False if there is no move.
    bool MemoryAnalysis(unsigned short score, const unsigned short moves[8],
                        Analysis &a);

    // The nominal depth of an iteration from Psion's iteration counter
    // (5A67): the ply at which it stops unless it extends (always odd, 15
    // at most; see docs/search-internals.md, "Iterative deepening").
    int IterationDepth(unsigned char counter);
    // Whether "go depth" depth is reached once Psion's iteration counter
    // shows counter: Psion has started an iteration deeper than depth
    // (rounded up to Psion's odd depths, at least 3), so the iteration
    // that reached it is complete.
    bool DepthReached(int depth, unsigned char counter);

    // Plays pv on board (white to move) to check it. False if a move is
    // not by a piece of the side to move, captures one of its own pieces,
    // is not the way the piece moves, or leaves the own king in check.
    // Psion does not show the piece promoted to; the first of q, n, r, b
    // with which the rest of the line is possible is added then.
    bool CheckPv(const Board board, bool white, std::vector<std::string> &pv);

    // Replaces the first move of pv (checked with CheckPv), a promotion, by
    // move: the same promotion to the piece Psion played. The later
    // promotions are guessed again. False (and pv unchanged) if the whole
    // line is no longer possible then.
    bool CorrectPromotion(const Board board, bool white,
                          std::vector<std::string> &pv,
                          const std::string &move);

    enum SyncAction {
        SYNC_DONE,      // game equals target
        SYNC_SEND_MOVE, // enter target[game.size()]
        SYNC_TAKEBACK,  // take back the last move
        SYNC_NEW_GAME   // start a new game, then replay the target
    };
    // The next step to get from the game Psion has played to the target.
    SyncAction PlanSync(const std::vector<std::string> &game,
                        const std::vector<std::string> &target);
    // The number of moves the two have in common at the start.
    size_t CommonMoves(const std::vector<std::string> &game,
                       const std::vector<std::string> &target);

    // The board after the first n of moves from start.
    void BoardAfter(const StartPosition &start,
                    const std::vector<std::string> &moves, size_t n,
                    Board board);

} // namespace psion

#endif
