#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "chess.h"

namespace psion {

    // More plies than this to take back and a new game is quicker.
    const size_t MAX_TAKEBACKS = 4;

    static char learned_codes[256];

    void LearnSquareCode(unsigned char v, char piece) {
        learned_codes[v] = piece;
    }

    // Square codes: bit 0x80 is set for Black. Bit 0x40 (and 0x04 for
    // rooks and kings) is some flag that does not change the piece.
    char DecodeSquare(unsigned char v) {
        char c;
        if (v == 0) {
            return '.';
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
            return learned_codes[v] ? learned_codes[v] : '?';
        }
        return (v & 0x80) ? c - 'A' + 'a' : c;
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

    // A move in Psion's notation ("E2-E4", "D5xE4", "O-O", ...) to UCI
    // notation; "" if it is not one.
    static std::string analysis_move(std::string m, bool white) {
        size_t i;
        for (i = 0; i < m.size(); i++) {
            m[i] = tolower((unsigned char)m[i]);
        }
        while (!m.empty() &&
               (m[m.size() - 1] == '+' || m[m.size() - 1] == '#')) {
            m.erase(m.size() - 1);
        }
        if (m == "o-o" || m == "0-0") {
            return white ? "e1g1" : "e8g8";
        }
        if (m == "o-o-o" || m == "0-0-0") {
            return white ? "e1c1" : "e8c8";
        }
        if (m.size() < 5 || (m[2] != '-' && m[2] != 'x')) {
            return "";
        }
        std::string uci = m.substr(0, 2) + m.substr(3, 2);
        std::string rest = m.substr(5);
        if (!rest.empty() && (rest[0] == '=' || rest[0] == '(')) {
            rest.erase(0, 1);
        }
        if (!rest.empty() && rest[rest.size() - 1] == ')') {
            rest.erase(rest.size() - 1);
        }
        if (rest.size() > 1) {
            return "";
        }
        uci += rest;
        return IsMoveString(uci) ? uci : "";
    }

    bool ParseAnalysis(const std::string &row, bool white, Analysis &a) {
        std::vector<std::string> tokens = Tokenize(row.c_str());
        a.mate = false;
        a.score = 0;
        a.pv.clear();
        a.complete = false;
        if (tokens.empty()) {
            return false;
        }
        // The score: [-]NN.N
        const std::string &s = tokens[0];
        size_t i = s[0] == '-' ? 1 : 0;
        size_t dot = s.find('.');
        if (dot == std::string::npos || dot == i || dot + 2 != s.size() ||
            !isdigit((unsigned char)s[dot + 1])) {
            return false;
        }
        int value = 0;
        for (; i < dot; i++) {
            if (!isdigit((unsigned char)s[i])) {
                return false;
            }
            value = 10 * value + s[i] - '0';
        }
        value = 10 * value + s[dot + 1] - '0'; // tenths of a pawn
        bool negative = s[0] == '-';
        bool side = white;
        for (i = 1; i < tokens.size(); i++) {
            std::string m = analysis_move(tokens[i], side);
            if (m.empty()) {
                break;
            }
            a.pv.push_back(m);
            side = !side;
        }
        if (a.pv.empty()) {
            return false;
        }
        a.complete = i == tokens.size();
        if (value >= 999) {
            // Moves to mate, from the length of the line (the side to move
            // mates with its last move, or is mated by the opponent's).
            int n = a.pv.size();
            a.mate = true;
            a.score = negative ? -std::max(1, n / 2) : (n + 1) / 2;
        } else {
            a.score = (negative ? -10 : 10) * value;
        }
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

    GoParams ParseGo(const std::vector<std::string> &tokens) {
        GoParams go;
        size_t i;
        go.wtime = go.btime = -1;
        go.winc = go.binc = go.movestogo = go.movetime = 0;
        go.infinite = false;
        for (i = 1; i < tokens.size(); i++) {
            const std::string &t = tokens[i];
            int v = i + 1 < tokens.size() ? atoi(tokens[i + 1].c_str()) : 0;
            if (t == "wtime")
                go.wtime = v;
            else if (t == "btime")
                go.btime = v;
            else if (t == "winc")
                go.winc = v;
            else if (t == "binc")
                go.binc = v;
            else if (t == "movestogo")
                go.movestogo = v;
            else if (t == "movetime")
                go.movetime = v;
            else if (t == "infinite" || t == "ponder")
                go.infinite = true;
        }
        return go;
    }

    unsigned int ThinkTime(const GoParams &go, bool white,
                           unsigned int latency) {
        int time = white ? go.wtime : go.btime;
        int inc = white ? go.winc : go.binc;
        int t;
        if (go.infinite) {
            return 0;
        }
        if (go.movetime > 0) {
            t = go.movetime;
        } else if (time >= 0) {
            t = time / (go.movestogo > 0 ? go.movestogo : 30) +
                3 * std::max(inc, 0) / 4;
            t = std::min(t, 3 * time / 4);
        } else {
            t = 5000; // depth, nodes, mate: not supported
        }
        return std::max(t - (int)latency, 50);
    }

    PositionRequest ParsePosition(const std::vector<std::string> &tokens) {
        static const char *start_fen =
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        PositionRequest req;
        size_t i = 2;
        req.supported = false;
        if (tokens.size() > 1 && tokens[1] == "fen") {
            std::string fen;
            for (i = 2; i < tokens.size() && tokens[i] != "moves"; i++) {
                fen += (fen.empty() ? "" : " ") + tokens[i];
            }
            if (fen != start_fen) {
                req.error = "FEN positions are not supported";
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
                    req.error = "Invalid move " + tokens[i];
                    return req;
                }
                req.moves.push_back(tokens[i]);
            }
        }
        req.supported = true;
        return req;
    }

    SyncAction PlanSync(const std::vector<std::string> &game,
                        const std::vector<std::string> &target) {
        size_t p = 0;
        if (game == target) {
            return SYNC_DONE;
        }
        while (p < game.size() && p < target.size() && game[p] == target[p]) {
            p++;
        }
        if (p == game.size()) {
            return SYNC_SEND_MOVE;
        }
        if (game.size() - p <= MAX_TAKEBACKS) {
            return SYNC_TAKEBACK;
        }
        return SYNC_NEW_GAME;
    }

} // namespace psion
