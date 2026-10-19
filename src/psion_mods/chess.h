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
	int wtime, btime;    // -1 if not given
	int winc, binc;
	int movestogo;       // 0 if not given
	int movetime;        // 0 if not given
	bool infinite;       // "infinite" or "ponder"
    };
    GoParams ParseGo(const std::vector<std::string> &tokens);

    // How long to think (ms) before pressing MOVE-NOW, allowing latency ms
    // for the move to be detected. 0 means until "stop".
    unsigned int ThinkTime(const GoParams &go, bool white,
			   unsigned int latency);

    struct PositionRequest {
	bool supported;
	std::vector<std::string> moves;   // from the start position
	std::string error;                // why it is not supported
    };
    PositionRequest ParsePosition(const std::vector<std::string> &tokens);

    enum SyncAction {
	SYNC_DONE,         // game equals target
	SYNC_SEND_MOVE,    // enter target[game.size()]
	SYNC_TAKEBACK,     // take back the last move
	SYNC_NEW_GAME      // start a new game, then replay the target
    };
    // The next step to get from the game Psion has played to the target.
    SyncAction PlanSync(const std::vector<std::string> &game,
			const std::vector<std::string> &target);

}

#endif
