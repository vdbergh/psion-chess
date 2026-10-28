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
        // All values are clamped to 0 .. INT_MAX: a negative clock (the
        // time used up) counts as 0.
        bool wtime_given, btime_given;
        int wtime, btime;
        int winc, binc;
        int movestogo; // 0 if not given
        bool movetime_given;
        int movetime;
        bool infinite; // until "stop"
        bool ponder;   // until "ponderhit" or "stop"
    };
    GoParams ParseGo(const std::vector<std::string> &tokens);

    // How long to think (ms) before pressing MOVE-NOW, allowing overhead
    // ms per move for everything else (the move being detected, the GUI,
    // the network). 0 means until "stop".
    unsigned int ThinkTime(const GoParams &go, bool white,
                           unsigned int overhead);

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

    // The keys that set up start in Psion's set-up mode, once it is in
    // that mode in a new game: 'e' (empty board), '0' (empty the square),
    // the cursor keys 'U', 'D', 'L', 'R', a piece letter ("kqrbnp") to
    // place a piece of the colour being set up, 'c' to change that colour.
    // colour_white: the colour at the start, with the cursor on a2 (White)
    // or a7. A king or rook that is not typed keeps its castling rights.
    std::string SetupKeys(const StartPosition &start, bool colour_white);

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
