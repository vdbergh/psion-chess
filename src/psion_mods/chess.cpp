#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "chess.h"

namespace psion {

    // More plies than this to take back and a new game is used instead.
    // Psion takes back many plies at once in a fraction of a second (100
    // in a 142-ply game in 0.5 s), while a new game replays every move
    // (about 0.35 s a ply).
    const size_t MAX_TAKEBACKS = 100;

    // Square codes: bit 0x80 is set for Black. Bit 0x40 (and 0x04 for
    // rooks and kings) is some flag that does not change the piece.
    char DecodeSquare(unsigned char v) {
        char c;
        if (v == 0) {
            return '.';
        }
        // Make gives a promoted piece the White code with bit 04 toggled
        // and 80 added for Black (1C5C..1D59 in docs/search-internals.md).
        // Bit 04 is set again once the piece moves, which gives the usual
        // codes.
        if (v == 0xD3) {
            return 'q';
        }
        if (v == 0xE0) {
            return 'n';
        }
        switch (v & 0x3F) {
        case 0x2C:
            c = 'P';
            break;
        case 0x24:
            c = 'N';
            break;
        case 0x1C:
            c = 'B';
            break;
        case 0x0A:
        case 0x0E:
            c = 'R';
            break;
        case 0x17:
            c = 'Q';
            break;
        case 0x01:
        case 0x05:
            c = 'K';
            break;
        default:
            return '?';
        }
        return (v & 0x80) ? c - 'A' + 'a' : c;
    }

    char DecodeSearchSquare(unsigned char v) {
        return v == 0x98 ? 'b' : DecodeSquare(v);
    }

    bool IsStartPosition(const Board board) {
        const char *rank1 = "RNBQKBNR";
        const char *rank8 = "rnbqkbnr";
        int f, r;
        for (f = 0; f < 8; f++) {
            if (board[f][0] != rank1[f] || board[f][1] != 'P' ||
                board[f][6] != 'p' || board[f][7] != rank8[f]) {
                return false;
            }
            for (r = 2; r < 6; r++) {
                if (board[f][r] != '.') {
                    return false;
                }
            }
        }
        return true;
    }

    static bool own_piece(char c, bool white) {
        return c != '.' && (isupper(c) != 0) == white;
    }

    // Plays move (UCI notation) on b: castling also moves the rook, en
    // passant removes the pawn taken.
    static void make_move(Board b, const std::string &move) {
        int ff = move[0] - 'a', fr = move[1] - '1', tf = move[2] - 'a',
            tr = move[3] - '1';
        char p = b[ff][fr];
        bool white = isupper(p) != 0;
        if (toupper(p) == 'K' && abs(tf - ff) == 2) {
            int rf = tf > ff ? 7 : 0, rt = tf > ff ? 5 : 3;
            b[rt][fr] = b[rf][fr];
            b[rf][fr] = '.';
        }
        if (toupper(p) == 'P' && ff != tf && b[tf][tr] == '.') {
            b[tf][fr] = '.';
        }
        if (move.size() == 5) {
            p = white ? toupper(move[4]) : move[4];
        }
        b[ff][fr] = '.';
        b[tf][tr] = p;
    }

    bool DeriveMove(const Board before, const Board after, bool white,
                    std::string &move) {
        int from[2], to[2], nfrom = 0, nto = 0, other = 0, f, r, i;
        int ffrom, rfrom, fto, rto;
        for (f = 0; f < 8; f++) {
            for (r = 0; r < 8; r++) {
                char b = before[f][r], a = after[f][r];
                if (a == b) {
                    continue;
                }
                if (own_piece(b, white)) {
                    if (nfrom == 2) {
                        return false;
                    }
                    from[nfrom++] = 8 * f + r;
                }
                if (own_piece(a, white)) {
                    if (nto == 2) {
                        return false;
                    }
                    to[nto++] = 8 * f + r;
                }
                if (own_piece(a, !white)) {
                    return false; // opponent piece appeared or changed
                }
                if (own_piece(b, !white) && a == '.') {
                    other++; // captured en passant
                }
            }
        }
        if (nfrom == 2 && nto == 2) { // castling: report the king move
            char king = white ? 'K' : 'k';
            bool king_from = false, king_to = false;
            for (i = 0; i < 2; i++) {
                if (before[from[i] / 8][from[i] % 8] == king) {
                    from[0] = from[i];
                    king_from = true;
                }
                if (after[to[i] / 8][to[i] % 8] == king) {
                    to[0] = to[i];
                    king_to = true;
                }
            }
            if (!king_from || !king_to) {
                return false;
            }
        } else if (nfrom != 1 || nto != 1) {
            return false;
        }
        ffrom = from[0] / 8;
        rfrom = from[0] % 8;
        fto = to[0] / 8;
        rto = to[0] % 8;
        char piece = before[ffrom][rfrom];
        if (other > 1 || (other == 1 && toupper(piece) != 'P')) {
            return false;
        }
        move = "";
        move += (char)('a' + ffrom);
        move += (char)('1' + rfrom);
        move += (char)('a' + fto);
        move += (char)('1' + rto);
        if (toupper(piece) == 'P' && toupper(after[fto][rto]) != 'P') {
            move += (char)tolower(after[fto][rto]);
        }
        // Only a complete move: not, e.g., a castling king whose rook has
        // not moved yet, while Psion updates its board.
        Board b;
        memcpy(b, before, sizeof(b));
        make_move(b, move);
        return memcmp(b, after, sizeof(b)) == 0;
    }

    bool IsMoveString(const std::string &m) {
        if (m.size() != 4 && m.size() != 5) {
            return false;
        }
        if (m[0] < 'a' || m[0] > 'h' || m[1] < '1' || m[1] > '8' ||
            m[2] < 'a' || m[2] > 'h' || m[3] < '1' || m[3] > '8') {
            return false;
        }
        return m.size() == 4 || (m[4] != 0 && strchr("qrbn", m[4]) != NULL);
    }

    // A square of Psion's search board (0x88, two bytes per square, a8
    // first); bit 0 holds a flag.
    static std::string search_square(unsigned int b) {
        std::string s;
        s += (char)('a' + ((b & 0x0E) >> 1));
        s += (char)('8' - ((b & 0xE0) >> 5));
        return s;
    }

    int IterationDepth(unsigned char counter) {
        return 2 * ((counter + 1) / 4) + 1;
    }

    bool DepthReached(int depth, unsigned char counter) {
        return IterationDepth(counter) > std::max(depth | 1, 3);
    }

    bool MemoryAnalysis(unsigned short score, const unsigned short moves[8],
                        Analysis &a) {
        a.mate = false;
        a.score = 0;
        a.pv.clear();
        a.depth = 0;
        a.seldepth = 0;
        for (int i = 0; i < 8 && moves[i]; i++) {
            a.pv.push_back(search_square(moves[i] >> 8) +
                           search_square(moves[i] & 0xFF));
        }
        if (a.pv.empty()) {
            return false;
        }
        int value = (int)score - 0x8000;
        int units = abs(value);
        if (units / 24 >= 256) {
            // A mate (shown as 99.9): 2 units per ply to the mate, from
            // 10000 down when the side to move mates, from 0 up when it is
            // mated (see docs/search-internals.md, "Mate scores").
            a.mate = true;
            if (value > 0) {
                int plies = (0x10000 - score) / 2;
                a.score = std::max(1, (plies + 1) / 2);
            } else {
                int plies = score / 2;
                a.score = -std::max(1, plies / 2);
            }
        } else {
            ScoreCentipawns(score, a.score);
        }
        return true;
    }

    bool ScoreCentipawns(unsigned short score, int &cp) {
        int value = (int)score - 0x8000;
        int units = abs(value);
        if (units / 24 >= 256) {
            return false;
        }
        cp = (value < 0 ? -1 : 1) * ((units * 100 + 12) / 24);
        return true;
    }

    static bool attacked(const Board b, int f, int r, bool by_white);

    // Whether the piece on (ff,fr) can move to (tf,tr) by the way it
    // moves: the path of a sliding piece is clear, a pawn moves forward
    // to an empty square or captures diagonally (also en passant), the
    // king does not castle out of or through check. Whether the move
    // leaves the own king in check is not considered here, nor castling
    // rights.
    static bool piece_can_move(const Board b, int ff, int fr, int tf, int tr) {
        char p = b[ff][fr];
        bool white = isupper(p) != 0;
        int df = tf - ff, dr = tr - fr, adf = abs(df), adr = abs(dr);
        switch (toupper(p)) {
        case 'N':
            return (adf == 1 && adr == 2) || (adf == 2 && adr == 1);
        case 'K':
            if (adf == 2 && dr == 0) {
                // Castling: from the home square, towards a rook, through
                // empty squares.
                int rf = df > 0 ? 7 : 0, f;
                if (ff != 4 || fr != (white ? 0 : 7) ||
                    b[rf][fr] != (white ? 'R' : 'r')) {
                    return false;
                }
                for (f = std::min(ff, rf) + 1; f < std::max(ff, rf); f++) {
                    if (b[f][fr] != '.') {
                        return false;
                    }
                }
                return !attacked(b, ff, fr, !white) &&
                       !attacked(b, ff + df / 2, fr, !white);
            }
            return adf <= 1 && adr <= 1 && (adf || adr);
        case 'P': {
            int dir = white ? 1 : -1;
            char t = b[tf][tr];
            if (df == 0) {
                if (t != '.') {
                    return false;
                }
                if (dr == dir) {
                    return true;
                }
                return dr == 2 * dir && fr == (white ? 1 : 6) &&
                       b[ff][fr + dir] == '.';
            }
            if (adf != 1 || dr != dir) {
                return false;
            }
            if (t != '.') {
                return true;
            }
            // En passant: next to an enemy pawn on its fifth rank.
            return fr == (white ? 4 : 3) && b[tf][fr] == (white ? 'p' : 'P');
        }
        case 'B':
            if (adf != adr)
                return false;
            break;
        case 'R':
            if (df && dr)
                return false;
            break;
        case 'Q':
            if (adf != adr && df && dr)
                return false;
            break;
        default:
            return false;
        }
        if (!adf && !adr) {
            return false;
        }
        int sf = (df > 0) - (df < 0), sr = (dr > 0) - (dr < 0), f = ff + sf,
            r = fr + sr;
        for (; f != tf || r != tr; f += sf, r += sr) {
            if (b[f][r] != '.') {
                return false;
            }
        }
        return true;
    }

    // Whether a piece of by_white attacks (f,r).
    static bool attacked(const Board b, int f, int r, bool by_white) {
        int af, ar;
        for (af = 0; af < 8; af++) {
            for (ar = 0; ar < 8; ar++) {
                char p = b[af][ar];
                if (p == '.' || p == '?' || (isupper(p) != 0) != by_white ||
                    (af == f && ar == r)) {
                    continue;
                }
                int adf = abs(f - af), dr = r - ar;
                switch (toupper(p)) {
                case 'P':
                    if (adf == 1 && dr == (by_white ? 1 : -1)) {
                        return true;
                    }
                    break;
                case 'K':
                    if (adf <= 1 && abs(dr) <= 1) {
                        return true;
                    }
                    break;
                default:
                    if (piece_can_move(b, af, ar, f, r)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static bool in_check(const Board b, bool white) {
        int f, r;
        for (f = 0; f < 8; f++) {
            for (r = 0; r < 8; r++) {
                if (b[f][r] == (white ? 'K' : 'k')) {
                    return attacked(b, f, r, !white);
                }
            }
        }
        return false;
    }

    // Checks pv from move i on, with board b (white to move).
    static bool check_pv_from(const Board board, bool white,
                              std::vector<std::string> &pv, size_t i) {
        Board b;
        memcpy(b, board, sizeof(b));
        for (; i < pv.size(); i++) {
            std::string &m = pv[i];
            int ff = m[0] - 'a', fr = m[1] - '1', tf = m[2] - 'a',
                tr = m[3] - '1';
            char p = b[ff][fr], t = b[tf][tr];
            if (p == '.' || p == '?' || (isupper(p) != 0) != white) {
                return false;
            }
            if (t != '.' && (isupper(t) != 0) == white) {
                return false;
            }
            if (!piece_can_move(b, ff, fr, tf, tr)) {
                return false;
            }
            char kind = toupper(p);
            if (kind == 'K' && abs(tf - ff) == 2) {
                // Castling: the rook moves too.
                int rf = tf > ff ? 7 : 0, rt = tf > ff ? 5 : 3;
                b[rt][fr] = b[rf][fr];
                b[rf][fr] = '.';
            }
            if (kind == 'P' && ff != tf && t == '.') {
                b[tf][fr] = '.'; // en passant
            }
            b[ff][fr] = '.';
            if (kind == 'P' && (tr == 7 || tr == 0)) {
                if (m.size() == 4) {
                    // The first piece with which the rest of the line works.
                    const char *pieces = "qnrb";
                    for (const char *c = pieces; *c; c++) {
                        b[tf][tr] = white ? toupper(*c) : *c;
                        if (!in_check(b, white) &&
                            check_pv_from(b, !white, pv, i + 1)) {
                            m += *c;
                            return true;
                        }
                    }
                    return false;
                }
                p = white ? toupper(m[4]) : m[4];
            }
            b[tf][tr] = p;
            if (in_check(b, white)) {
                return false;
            }
            white = !white;
        }
        return true;
    }

    bool CheckPv(const Board board, bool white, std::vector<std::string> &pv) {
        return check_pv_from(board, white, pv, 0);
    }

    bool CorrectPromotion(const Board board, bool white,
                          std::vector<std::string> &pv,
                          const std::string &move) {
        std::vector<std::string> line(pv);
        line[0] = move;
        for (size_t i = 1; i < line.size(); i++) {
            line[i] = line[i].substr(0, 4); // the pieces were guessed
        }
        if (!CheckPv(board, white, line)) {
            return false;
        }
        pv = line;
        return true;
    }

    std::vector<std::string> Tokenize(const char *line) {
        std::vector<std::string> tokens;
        const char *c = line;
        while (*c) {
            while (*c && isspace(*c)) {
                c++;
            }
            const char *b = c;
            while (*c && !isspace(*c)) {
                c++;
            }
            if (c > b) {
                tokens.push_back(std::string(b, c - b));
            }
        }
        return tokens;
    }

    // A "go" value: 0 if missing or not a number, else clamped to
    // 0 .. INT_MAX.
    static int go_value(const std::vector<std::string> &tokens, size_t i) {
        if (i >= tokens.size()) {
            return 0;
        }
        long v = strtol(tokens[i].c_str(), NULL, 10); // saturates on overflow
        return (int)std::min(std::max(v, 0L), (long)INT_MAX);
    }

    GoParams ParseGo(const std::vector<std::string> &tokens) {
        GoParams go;
        size_t i;
        go.wtime_given = go.btime_given = go.movetime_given = false;
        go.wtime = go.btime = 0;
        go.winc = go.binc = go.movestogo = go.movetime = 0;
        go.infinite = go.ponder = false;
        go.depth = go.nodes = go.mate = 0;
        for (i = 1; i < tokens.size(); i++) {
            const std::string &t = tokens[i];
            int v = go_value(tokens, i + 1);
            if (t == "wtime") {
                go.wtime = v;
                go.wtime_given = true;
            } else if (t == "btime") {
                go.btime = v;
                go.btime_given = true;
            } else if (t == "winc") {
                go.winc = v;
            } else if (t == "binc") {
                go.binc = v;
            } else if (t == "movestogo") {
                go.movestogo = v;
            } else if (t == "movetime") {
                go.movetime = v;
                go.movetime_given = true;
            } else if (t == "infinite") {
                go.infinite = true;
            } else if (t == "ponder") {
                go.ponder = true;
            } else if (t == "depth") {
                go.depth = std::max(v, 1); // given: at least the smallest
            } else if (t == "nodes") {
                go.nodes = std::max(v, 1);
            } else if (t == "mate") {
                // Negative: being mated (in at most -mate moves).
                long m = i + 1 < tokens.size()
                             ? strtol(tokens[i + 1].c_str(), NULL, 10)
                             : 0;
                m = std::min(std::max(m, (long)-INT_MAX), (long)INT_MAX);
                go.mate = m == 0 ? 1 : (int)m;
            }
        }
        return go;
    }

    long long ClockTime(const GoParams &go, bool white) {
        // long long: 3 * INT_MAX must not overflow.
        long long time = white ? go.wtime : go.btime;
        long long inc = white ? go.winc : go.binc;
        bool clock = white ? go.wtime_given : go.btime_given;
        if (go.infinite || go.movetime_given || !clock) {
            return -1;
        }
        // In sudden death a smaller share: the overhead of a move is a
        // fixed cost, so the remaining time must last for many moves.
        long long n = go.movestogo > 0 ? go.movestogo : inc > 0 ? 30 : 60;
        return std::min(time / n + 3 * inc / 4, 3 * time / 4);
    }

    static unsigned int less_overhead(long long t, unsigned int overhead) {
        // Over the n moves this keeps overhead per move in reserve:
        // time/n - overhead = (time - n * overhead) / n.
        // At most INT_MAX, so it fits.
        return (unsigned int)std::max(t - (long long)overhead,
                                      (long long)MIN_THINK_MS);
    }

    unsigned int ThinkTime(const GoParams &go, bool white,
                           unsigned int overhead) {
        long long t = ClockTime(go, white);
        if (go.infinite) {
            return 0;
        }
        if (go.movetime_given) {
            t = go.movetime; // 0: as little as possible
        } else if (t >= 0) {
            // a clock
        } else if (go.depth > 0 || go.nodes > 0 || go.mate != 0) {
            return 0; // until the depth, the nodes or a mate is reached
        } else {
            t = 5000; // a go without any limit
        }
        return less_overhead(t, overhead);
    }

    TimeParams DefaultTimeParams() {
        TimeParams tp;
        tp.target = 100;
        tp.soft_stop = 40;
        tp.drop = 6;
        tp.drop_extension = 100;
        tp.max = 200;
        tp.max_share = 75;
        tp.min_moves = 4;
        tp.min_moves_window = 1;
        tp.fail_low_max = 1;
        tp.fail_low_cap = 400;
        tp.window_cap = 400;
        return tp;
    }

    unsigned int ScaledThinkTime(long long allocated, long long remaining,
                                 unsigned int overhead, const TimeParams &tp,
                                 int percent) {
        // allocated and remaining are at most INT_MAX, the percentages at
        // most a few thousand: no overflow.
        long long t = allocated * tp.target / 100 * percent / 100;
        return std::min(less_overhead(t, overhead),
                        MaxThinkTime(allocated, remaining, overhead, tp));
    }

    unsigned int MaxThinkTime(long long allocated, long long remaining,
                              unsigned int overhead, const TimeParams &tp) {
        return less_overhead(std::min(allocated * tp.max / 100,
                                      remaining * tp.max_share / 100),
                             overhead);
    }

    unsigned int FailLowMaxTime(long long allocated, long long remaining,
                                unsigned int overhead, const TimeParams &tp) {
        TimeParams t = tp;
        t.max = std::max(tp.max, tp.fail_low_cap);
        return MaxThinkTime(allocated, remaining, overhead, t);
    }

    unsigned int WindowMaxTime(long long allocated, long long remaining,
                               unsigned int overhead, const TimeParams &tp) {
        TimeParams t = tp;
        t.max = std::max(tp.max, tp.window_cap);
        return MaxThinkTime(allocated, remaining, overhead, t);
    }

    int ExtensionPercent(const TimeParams &tp, bool drop_known, int drop) {
        int e = 100;
        if (drop_known && drop >= tp.drop) {
            e = std::max(e, tp.drop_extension);
        }
        return e;
    }

    unsigned int MoveNowTime(unsigned int think_ms, unsigned int setup_ms) {
        long long setup = setup_ms;
        long long t =
            (long long)think_ms - std::min(setup, (long long)MAX_SETUP_MS);
        // The sum can exceed UINT_MAX (but not a long long).
        return (unsigned int)std::min(
            setup + std::max(t, (long long)MIN_THINK_MS), (long long)UINT_MAX);
    }

    unsigned int EloCycles(int elo) {
        // Fitted to matches at 120+1 at 2500, 16000 and 100000 cycles
        // against TSCP 1.83b and Sargon 1978 1.01b, anchored at their CCRL
        // Blitz ratings (raised by 8 for the i5-1135G7 against CCRL's
        // i7-4770K): 86 Elo per doubling, 1597 at 100000 cycles.
        elo = std::min(std::max(elo, MIN_ELO), MAX_ELO);
        return (unsigned int)(100000 * pow(2, (elo - 1597) / 86.0) + 0.5);
    }

    bool ParseSetOption(const std::vector<std::string> &tokens,
                        std::string &name, std::string &value) {
        name.clear();
        value.clear();
        std::string *part = NULL;
        for (size_t i = 1; i < tokens.size(); i++) {
            if (tokens[i] == "name" && part != &value) {
                part = &name;
            } else if (tokens[i] == "value" && part == &name) {
                part = &value;
            } else if (part) {
                if (!part->empty()) {
                    *part += ' ';
                }
                *part += tokens[i];
            }
        }
        return !name.empty();
    }

    bool SameOptionName(const std::string &a, const std::string &b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
                return false;
            }
        }
        return true;
    }

    StartPosition StandardStart() {
        StartPosition s;
        s.standard = true;
        memset(s.board, '.', sizeof(s.board));
        s.white = true;
        s.castling = "KQkq";
        return s;
    }

    bool SameStart(const StartPosition &a, const StartPosition &b) {
        if (a.standard || b.standard) {
            return a.standard == b.standard;
        }
        return a.white == b.white && a.castling == b.castling &&
               memcmp(a.board, b.board, sizeof(a.board)) == 0;
    }

    // Parses a FEN (the move counters are ignored) into req.start, and
    // req.moves for an en passant capture. False with req.error if Psion
    // cannot play from it.
    static bool parse_fen(const std::vector<std::string> &fields,
                          PositionRequest &req) {
        Board &b = req.start.board;
        int f = 0, r = 7, white_kings = 0, black_kings = 0;
        size_t i;
        req.error = "Invalid FEN";
        if (fields.size() < 4 || (fields[1] != "w" && fields[1] != "b")) {
            return false;
        }
        memset(b, '.', sizeof(b));
        for (i = 0; i < fields[0].size(); i++) {
            char c = fields[0][i];
            if (c == '/') {
                if (f != 8 || r == 0) {
                    return false;
                }
                f = 0;
                r--;
            } else if (c >= '1' && c <= '8') {
                f += c - '0';
                if (f > 8) {
                    return false;
                }
            } else if (strchr("PNBRQKpnbrqk", c) && f < 8) {
                if (toupper(c) == 'P' && (r == 0 || r == 7)) {
                    return false;
                }
                white_kings += c == 'K';
                black_kings += c == 'k';
                b[f++][r] = c;
            } else {
                return false;
            }
        }
        req.start.white = fields[1] == "w";
        if (f != 8 || r != 0 || white_kings != 1 || black_kings != 1 ||
            in_check(b, !req.start.white)) {
            return false;
        }
        const std::string &castling = fields[2], &ep = fields[3];
        if (castling.empty() ||
            (castling != "-" &&
             castling.find_first_not_of("KQkq") != std::string::npos)) {
            if (castling.find_first_not_of("ABCDEFGHabcdefgh") ==
                std::string::npos) {
                req.error = "Chess960 castling rights are not supported";
            }
            return false;
        }
        // In the order "KQkq", without rights whose king or rook is not
        // on its square (as python-chess does).
        req.start.castling = "";
        for (i = 0; i < 4; i++) {
            char c = "KQkq"[i];
            int rr = i < 2 ? 0 : 7, rf = i % 2 == 0 ? 7 : 0;
            if (castling.find(c) != std::string::npos &&
                b[4][rr] == (i < 2 ? 'K' : 'k') &&
                b[rf][rr] == (i < 2 ? 'R' : 'r')) {
                req.start.castling += c;
            }
        }
        if (req.start.castling.empty()) {
            req.start.castling = "-";
        }
        if (IsStartPosition(b) && req.start.white &&
            req.start.castling == "KQkq" && ep == "-") {
            req.start = StandardStart();
            return true;
        }
        req.start.standard = false;
        if (ep == "-") {
            return true;
        }
        // The pawn that moved two squares, and the squares it passed.
        bool white = req.start.white;
        int ef = ep.size() == 2 ? ep[0] - 'a' : -1;
        int er = ep.size() == 2 ? ep[1] - '1' : -1;
        int dir = white ? -1 : 1; // the direction the pawn moved in
        char pawn = white ? 'p' : 'P', own = white ? 'P' : 'p';
        if (ef < 0 || ef > 7 || er != (white ? 5 : 2) ||
            b[ef][er + dir] != pawn || b[ef][er] != '.' ||
            b[ef][er - dir] != '.') {
            return false;
        }
        if ((ef == 0 || b[ef - 1][er + dir] != own) &&
            (ef == 7 || b[ef + 1][er + dir] != own)) {
            return true; // no pawn can take it: as if not given
        }
        // Set up the position before that pawn move, and play it.
        b[ef][er + dir] = '.';
        b[ef][er - dir] = pawn;
        req.start.white = !white;
        if (in_check(b, white)) {
            req.error = "This en passant position cannot be set up";
            return false;
        }
        std::string m;
        m += (char)('a' + ef);
        m += (char)('1' + er - dir);
        m += (char)('a' + ef);
        m += (char)('1' + er + dir);
        req.moves.push_back(m);
        return true;
    }

    // Whether m is legal on board b (white to move), with the castling
    // rights left (a part of "KQkq") and en passant possible to the file
    // ep_file (-1: none).
    static bool legal_move(const Board b, bool white,
                           const std::string &castling, int ep_file,
                           const std::string &m) {
        int ff = m[0] - 'a', fr = m[1] - '1', tf = m[2] - 'a',
            tr = m[3] - '1';
        char p = b[ff][fr], t = b[tf][tr], kind = (char)toupper(p);
        Board after;
        if (p == '.' || (isupper(p) != 0) != white ||
            (t != '.' && (isupper(t) != 0) == white) ||
            !piece_can_move(b, ff, fr, tf, tr)) {
            return false;
        }
        if (kind == 'K' && abs(tf - ff) == 2) {
            char right = tf > ff ? 'K' : 'Q';
            if (castling.find(white ? right : (char)tolower(right)) ==
                std::string::npos) {
                return false;
            }
        }
        if (kind == 'P' && ff != tf && t == '.' && tf != ep_file) {
            return false;
        }
        if ((kind == 'P' && (tr == 0 || tr == 7)) != (m.size() == 5)) {
            return false; // a promotion needs its piece, others have none
        }
        memcpy(after, b, sizeof(after));
        make_move(after, m);
        return !in_check(after, white);
    }

    static bool has_legal_move(const Board b, bool white,
                               const std::string &castling, int ep_file) {
        int ff, fr, tf, tr;
        for (ff = 0; ff < 8; ff++) {
            for (fr = 0; fr < 8; fr++) {
                char p = b[ff][fr];
                if (p == '.' || (isupper(p) != 0) != white) {
                    continue;
                }
                for (tf = 0; tf < 8; tf++) {
                    for (tr = 0; tr < 8; tr++) {
                        std::string m;
                        m += (char)('a' + ff);
                        m += (char)('1' + fr);
                        m += (char)('a' + tf);
                        m += (char)('1' + tr);
                        if (toupper(p) == 'P' && (tr == 0 || tr == 7)) {
                            m += 'q';
                        }
                        if (legal_move(b, white, castling, ep_file, m)) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // Whether moves are legal from start. If not, error says which move
    // is not, and why when the game was already over. If they are, over
    // says whether the game is over after them: "CHECKMATE", "STALEMATE"
    // or "".
    static bool check_game(const StartPosition &start,
                           const std::vector<std::string> &moves,
                           std::string &error, std::string *over = NULL) {
        Board b;
        bool white = start.standard || start.white;
        std::string castling = start.standard ? "KQkq" : start.castling;
        int ep_file = -1;
        size_t i, j;
        BoardAfter(start, moves, 0, b);
        for (i = 0; i < moves.size(); i++) {
            const std::string &m = moves[i];
            if (!legal_move(b, white, castling, ep_file, m)) {
                error = "Illegal move " + m;
                if (!has_legal_move(b, white, castling, ep_file)) {
                    error += in_check(b, white) ? " (checkmate)"
                                                : " (stalemate)";
                }
                return false;
            }
            int ff = m[0] - 'a', fr = m[1] - '1', tr = m[3] - '1';
            ep_file = toupper(b[ff][fr]) == 'P' && abs(tr - fr) == 2 ? ff : -1;
            // A king or rook leaving its square, or a rook taken there,
            // ends the right.
            static const char *squares[4] = {"e1h1", "e1a1", "e8h8", "e8a8"};
            for (j = 0; j < 4; j++) {
                const char *sq = squares[j];
                if (m.compare(0, 2, sq, 2) == 0 ||
                    m.compare(0, 2, sq + 2, 2) == 0 ||
                    m.compare(2, 2, sq + 2, 2) == 0) {
                    size_t k = castling.find("KQkq"[j]);
                    if (k != std::string::npos) {
                        castling.erase(k, 1);
                    }
                }
            }
            make_move(b, m);
            white = !white;
        }
        if (over) {
            *over = has_legal_move(b, white, castling, ep_file) ? ""
                    : in_check(b, white)                       ? "CHECKMATE"
                                                               : "STALEMATE";
        }
        return true;
    }

    std::string GameOver(const StartPosition &start,
                         const std::vector<std::string> &moves) {
        std::string error, over;
        return check_game(start, moves, error, &over) ? over : "";
    }

    PositionRequest ParsePosition(const std::vector<std::string> &tokens) {
        PositionRequest req;
        size_t i = 2;
        req.supported = false;
        req.start = StandardStart();
        if (tokens.size() > 1 && tokens[1] == "fen") {
            std::vector<std::string> fields;
            for (i = 2; i < tokens.size() && tokens[i] != "moves"; i++) {
                fields.push_back(tokens[i]);
            }
            if (!parse_fen(fields, req)) {
                req.moves.clear();
                return req;
            }
        } else if (tokens.size() < 2 || tokens[1] != "startpos") {
            req.error = "Invalid position command";
            return req;
        }
        if (i < tokens.size() && tokens[i] == "moves") {
            for (i++; i < tokens.size(); i++) {
                if (!IsMoveString(tokens[i])) {
                    req.moves.clear();
                    req.start = StandardStart();
                    req.error = "Invalid move " + tokens[i];
                    return req;
                }
                req.moves.push_back(tokens[i]);
            }
        }
        if (!check_game(req.start, req.moves, req.error)) {
            req.moves.clear();
            req.start = StandardStart();
            return req;
        }
        req.supported = true;
        return req;
    }

    // Moves the cursor from (f,r) to (tf,tr).
    static void cursor_to(std::string &keys, int &f, int &r, int tf, int tr) {
        for (; f < tf; f++)
            keys += 'R';
        for (; f > tf; f--)
            keys += 'L';
        for (; r < tr; r++)
            keys += 'U';
        for (; r > tr; r--)
            keys += 'D';
    }

    // Places the pieces of one colour, kings or the others, on the squares
    // in todo.
    static void place(std::string &keys, int &f, int &r, const Board board,
                      const bool todo[8][8], bool white, bool kings) {
        int sf, sr;
        for (sr = 0; sr < 8; sr++) {
            for (sf = 0; sf < 8; sf++) {
                char p = board[sf][sr];
                if (!todo[sf][sr] || p == '.' || (isupper(p) != 0) != white ||
                    (toupper(p) == 'K') != kings) {
                    continue;
                }
                cursor_to(keys, f, r, sf, sr);
                keys += (char)tolower(p);
            }
        }
    }

    std::string SetupKeys(const StartPosition &start, bool colour_white) {
        const char *initial = "RNBQKBNR"
                              "PPPPPPPP"
                              "........"
                              "........"
                              "........"
                              "........"
                              "pppppppp"
                              "rnbqkbnr"; // a1..h1, ..., a8..h8
        std::string keys;
        int f = 0, r = colour_white ? 1 : 6, sf, sr;
        bool colour = colour_white, todo[8][8];
        if (start.castling == "-") {
            // From an empty board.
            keys = "e";
            for (sf = 0; sf < 8; sf++) {
                for (sr = 0; sr < 8; sr++) {
                    todo[sf][sr] = true;
                }
            }
        } else {
            // From the start position, which is on the board in a new
            // game: kings and rooks that are not typed keep their castling
            // rights. A rook is typed again to remove its right.
            for (sf = 0; sf < 8; sf++) {
                for (sr = 0; sr < 8; sr++) {
                    todo[sf][sr] = start.board[sf][sr] != initial[sr * 8 + sf];
                }
            }
            for (int i = 0; i < 4; i++) {
                int rr = i < 2 ? 0 : 7, rf = i % 2 == 0 ? 7 : 0;
                if (start.castling.find("KQkq"[i]) == std::string::npos) {
                    todo[rf][rr] = true;
                }
            }
            // First empty the squares whose piece changes (a rook that is
            // typed again is not removed), so that a king is not placed
            // while one of its colour is still on the board.
            for (sr = 0; sr < 8; sr++) {
                for (sf = 0; sf < 8; sf++) {
                    if (todo[sf][sr] && initial[sr * 8 + sf] != '.' &&
                        start.board[sf][sr] != initial[sr * 8 + sf]) {
                        cursor_to(keys, f, r, sf, sr);
                        keys += '0';
                    }
                }
            }
        }
        // The kings last: until both are on the board, Psion's board is not
        // taken for a position (see ReadGameState).
        place(keys, f, r, start.board, todo, colour, false);
        keys += 'c';
        place(keys, f, r, start.board, todo, !colour, false);
        place(keys, f, r, start.board, todo, !colour, true);
        keys += 'c';
        place(keys, f, r, start.board, todo, colour, true);
        // Psion leaves set-up mode with the colour being set up to move.
        if (colour != start.white) {
            keys += 'c';
        }
        return keys;
    }

    size_t CommonMoves(const std::vector<std::string> &game,
                       const std::vector<std::string> &target) {
        size_t p = 0;
        while (p < game.size() && p < target.size() && game[p] == target[p]) {
            p++;
        }
        return p;
    }

    void BoardAfter(const StartPosition &start,
                    const std::vector<std::string> &moves, size_t n,
                    Board board) {
        const char *initial = "RNBQKBNRPPPPPPPP................................"
                              "pppppppprnbqkbnr"; // a1..h1, ..., a8..h8
        int f, r;
        size_t i;
        for (f = 0; f < 8; f++) {
            for (r = 0; r < 8; r++) {
                board[f][r] =
                    start.standard ? initial[r * 8 + f] : start.board[f][r];
            }
        }
        for (i = 0; i < n && i < moves.size(); i++) {
            make_move(board, moves[i]);
        }
    }

    SyncAction PlanSync(const std::vector<std::string> &game,
                        const std::vector<std::string> &target) {
        size_t p;
        if (game == target) {
            return SYNC_DONE;
        }
        p = CommonMoves(game, target);
        if (p == game.size()) {
            return SYNC_SEND_MOVE;
        }
        if (game.size() - p <= MAX_TAKEBACKS) {
            return SYNC_TAKEBACK;
        }
        return SYNC_NEW_GAME;
    }

} // namespace psion
