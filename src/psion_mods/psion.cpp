#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#ifndef _WIN32
#include <sys/time.h>
#endif
#include <time.h>
#include <queue>
#include <vector>
#include <algorithm>

#include "shell.h"
#include "keyboard.h"
#include "timer.h"
#include "psion.h"
#include "ocr.h"
#include "memscan.h"
#include "chess.h"
#include "psionfiles.h"
#include "mem.h"
#include "regs.h"
#include "cpu.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <fcntl.h>
#include <io.h>
#endif

unsigned int GetTicks() {
    static bool firsttime = true;
#ifndef _WIN32
    double static msec_first;
    double msec;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    msec = tv.tv_sec * 1000 + (tv.tv_usec + 500) / 1000.0;
    if (firsttime) {
        msec_first = msec;
        firsttime = false;
    }
    return (unsigned int)(msec - msec_first);
#else
    // GetTickCount() is too coarse (10-16 ms) for the timing here.
    static LARGE_INTEGER frequency, first;
    LARGE_INTEGER now;
    if (firsttime) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&first);
        firsttime = false;
    }
    QueryPerformanceCounter(&now);
    return (unsigned int)((now.QuadPart - first.QuadPart) * 1000 /
                          frequency.QuadPart);
#endif
}

void Delay(int msec) {
#ifndef _WIN32
    struct timespec tm;
    tm.tv_sec = msec / 1000;
    tm.tv_nsec = 1000000 * (msec % 1000);
    nanosleep(&tm, NULL);
#else
    Sleep(msec);
#endif
}

namespace psion {

    static bool uci_mode = false;
    static bool verbose = false;

    void SetVerbose(bool on) {
        verbose = on;
    }

    bool Verbose() {
        return verbose;
    }

    // Writes complete lines only, each with a single write, so that a GUI
    // that merges stderr with stdout never splits a line of either.
    void WriteLines(FILE *out, std::string &pending, const char *text) {
        pending += text;
        size_t end = pending.rfind('\n');
        if (end == std::string::npos) {
            return;
        }
        fwrite(pending.data(), 1, end + 1, out);
        fflush(out);
        pending.erase(0, end + 1);
    }

    // Diagnostics, only with --verbose (or "debug on"). They go to stdout
    // in the interactive modes; in UCI mode, where stdout is reserved for
    // the protocol, to stderr.
    static void say(const char *format, ...) {
        static std::string pending;
        if (!verbose) {
            return;
        }
        char buf[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        WriteLines(uci_mode ? stderr : stdout, pending, buf);
    }

    class GameState {
      public:
        StartPosition start;            // the position the game started from
        int base_ply;                   // Psion's ply counter there
        std::vector<std::string> moves; // from start
        bool thinking;
        GameState();
    };

    GameState::GameState() {
        start = StandardStart();
        base_ply = 0;
        thinking = false;
    }

    GameState game_state;

    // Keys for Psion. A key (with its modifier, if any) is passed on once
    // Psion has taken the previous one from the BIOS keyboard buffer, so
    // keys go in as fast as Psion reads them and the buffer never fills.
    class KeyBoardBuffer {
        struct KeyEvent {
            KBD_KEYS key;
            bool pressed;
        };
        std::queue<std::vector<KeyEvent> > strokes;
        bool waiting;       // for Psion to take the last key
        bool arrived;       // the last key is in the BIOS buffer
        unsigned int tail;  // BIOS buffer tail when it was sent
        unsigned int since; // when it was sent
        void Push(std::vector<KeyEvent> &stroke, KBD_KEYS k, bool pressed);

      public:
        KeyBoardBuffer();
        void SendKey(KBD_KEYS k);
        void SendKey(KBD_KEYS modifier, KBD_KEYS k);
        void ProcessKeys();
        bool Empty();
    };

    // The BIOS keyboard buffer: head and tail offsets in segment 0040.
    const unsigned int BIOS_KBD_HEAD = 0x41A;
    const unsigned int BIOS_KBD_TAIL = 0x41C;
    // A key that has not reached the BIOS buffer by then (e.g. a key
    // without a character) is taken to be handled.
    const unsigned int KEY_ARRIVAL_MS = 50;

    KeyBoardBuffer::KeyBoardBuffer() {
        waiting = false;
        arrived = false;
        tail = 0;
        since = 0;
    }
    void KeyBoardBuffer::Push(std::vector<KeyEvent> &stroke, KBD_KEYS k,
                              bool pressed) {
        KeyEvent e;
        e.key = k;
        e.pressed = pressed;
        stroke.push_back(e);
    }
    void KeyBoardBuffer::SendKey(KBD_KEYS k) {
        std::vector<KeyEvent> stroke;
        Push(stroke, k, true);
        Push(stroke, k, false);
        strokes.push(stroke);
    }
    // Key combination such as shift-4 ('$') or ctrl-L.
    void KeyBoardBuffer::SendKey(KBD_KEYS modifier, KBD_KEYS k) {
        std::vector<KeyEvent> stroke;
        Push(stroke, modifier, true);
        Push(stroke, k, true);
        Push(stroke, k, false);
        Push(stroke, modifier, false);
        strokes.push(stroke);
    }
    // True once all keys have been taken by Psion.
    bool KeyBoardBuffer::Empty() {
        return strokes.empty() && !waiting;
    }
    void KeyBoardBuffer::ProcessKeys() {
        unsigned int head = mem_readw(BIOS_KBD_HEAD);
        unsigned int now_tail = mem_readw(BIOS_KBD_TAIL);
        if (waiting) {
            if (!arrived) {
                if (now_tail != tail) {
                    arrived = true;
                } else if (GetTicks() - since >= KEY_ARRIVAL_MS) {
                    waiting = false;
                }
            }
            if (arrived && head == now_tail) {
                waiting = false;
            }
            if (waiting) {
                return;
            }
        }
        if (strokes.empty()) {
            return;
        }
        const std::vector<KeyEvent> &stroke = strokes.front();
        for (size_t i = 0; i < stroke.size(); i++) {
            KEYBOARD_AddKey(stroke[i].key, stroke[i].pressed);
        }
        strokes.pop();
        waiting = true;
        arrived = false;
        tail = now_tail;
        since = GetTicks();
    }

    KeyBoardBuffer keyboard_buffer;

    void ResetCursor() {
        keyboard_buffer.SendKey(KBD_f3);
        keyboard_buffer.SendKey(KBD_f3);
    }

    void SendMove(bool white, int file1, int row1, int file2, int row2) {
        int i;
        int file0, row0;
        ResetCursor();
        file0 = 0;
        if (white) {
            row0 = 1;
        } else {
            row0 = 6;
        }
        for (i = file0; i < file1; i++) {
            keyboard_buffer.SendKey(KBD_right);
        }
        if (row1 >= row0) {
            for (i = row0; i < row1; i++) {
                keyboard_buffer.SendKey(KBD_up);
            }
        } else {
            for (i = row0; i > row1; i--) {
                keyboard_buffer.SendKey(KBD_down);
            }
        }
        keyboard_buffer.SendKey(KBD_enter);
        if (file1 <= file2) {
            for (i = file1; i < file2; i++) {
                keyboard_buffer.SendKey(KBD_right);
            }
        } else {
            for (i = file1; i > file2; i--) {
                keyboard_buffer.SendKey(KBD_left);
            }
        }
        if (row1 <= row2) {
            for (i = row1; i < row2; i++) {
                keyboard_buffer.SendKey(KBD_up);
            }
        } else {
            for (i = row1; i > row2; i--) {
                keyboard_buffer.SendKey(KBD_down);
            }
        }
        keyboard_buffer.SendKey(KBD_enter);
    }

    // Whether dir contains Chess.exe (in any case on Linux).
    static bool has_psion_files(const std::string &dir) {
        static const char *names[] = {"Chess.exe", "CHESS.EXE", "chess.exe"};
        size_t i;
        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            FILE *f = fopen((dir + "/" + names[i]).c_str(), "rb");
            if (f) {
                fclose(f);
                return true;
            }
        }
        return false;
    }

    bool Psion_Init(DOS_Shell *d) {
        char input_line[CMD_MAXLINE] = {0};
        char args[] = ""; // Chess.exe's command line
        std::string dir = PsionDataDir();
        if (!has_psion_files(dir)) {
            fprintf(
                stderr,
                "Psion Chess (Chess.exe etc.) not found in %s.\n"
                "Run \"psion -d\" to download it, or see data/README.md.\n",
                dir.c_str());
            exit(1);
        }
        // Only the known files, to avoid running unverified software.
        std::string error;
        if (!CheckFiles(dir, PSION_FILES, PSION_FILE_COUNT, error)) {
            fprintf(stderr,
                    "Psion Chess in %s is not the version from "
                    "psion213.zip:\n%sSee data/README.md. Not running it.\n",
                    dir.c_str(), error.c_str());
            exit(1);
        }
        init_ocr();
        snprintf(input_line, CMD_MAXLINE, "MOUNT C \"%s\"", dir.c_str());
        d->DoCommand(input_line);
        strcpy(input_line, "C:");
        d->DoCommand(input_line);
        strcpy(input_line, "CHESS.EXE");
        // Typed ahead: 'C' answers the title screen's question for the
        // graphics card (IBM colour card), '2' then sets 2-player mode.
        keyboard_buffer.SendKey(KBD_c);
        keyboard_buffer.SendKey(KBD_2);
        if (!(d->Execute(input_line, args))) {
            return false;
        }
        strcpy(input_line, "EXIT");
        d->DoCommand(input_line);
        return true;
    }

    static void start_input();

    void IO_Init() {
        setvbuf(stdin, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
        timeBeginPeriod(1); // makes Sleep() precise to 1 ms
        // Lines end in "\n", not "\r\n": GUIs on Windows accept that, and
        // GUIs elsewhere (e.g. running the engine under Wine) need it.
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
#endif
        start_input();
    }

#ifdef _WIN32
    // On Windows a thread reads stdin, so that the emulator never blocks
    // on input. (Checking a pipe for input without reading from it, as
    // the POSIX code below does, is unreliable there, e.g. under Wine.)
    static CRITICAL_SECTION input_lock;
    static std::queue<std::string> input_lines;
    static bool input_eof = false;
    static HANDLE input_event; // set when a line or the end is queued

    static void add_input_line(const std::string &line) {
        EnterCriticalSection(&input_lock);
        input_lines.push(line);
        LeaveCriticalSection(&input_lock);
        SetEvent(input_event);
    }

    // A console echoes what is typed relative to where the cursor was when
    // the read started, so output written while a read is pending (always,
    // with this thread) would make the echo jump back. Hence a console is
    // read key by key, and the thread echoes the keys itself.
    static void read_console(HANDLE in) {
        HANDLE out = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, 0, NULL);
        std::string line;
        for (;;) {
            INPUT_RECORD event;
            DWORD n;
            if (!ReadConsoleInputA(in, &event, 1, &n)) {
                return;
            }
            if (n == 0 || event.EventType != KEY_EVENT ||
                !event.Event.KeyEvent.bKeyDown) {
                continue;
            }
            char c = event.Event.KeyEvent.uChar.AsciiChar;
            const char *echo = NULL;
            std::string typed;
            if (event.Event.KeyEvent.wVirtualKeyCode == VK_RETURN ||
                c == '\r' || c == '\n') {
                add_input_line(line + "\n");
                line.clear();
                echo = "\r\n";
            } else if (c == '\b' || c == 127) {
                if (line.empty()) {
                    continue;
                }
                line.erase(line.size() - 1);
                echo = "\b \b";
            } else if (c == 26) { // Ctrl-Z: end of input
                return;
            } else if ((unsigned char)c >= 32) {
                line += c;
                typed = std::string(1, c);
                echo = typed.c_str();
            } else {
                continue;
            }
            if (out != INVALID_HANDLE_VALUE) {
                DWORD written;
                WriteConsoleA(out, echo, strlen(echo), &written, NULL);
            }
        }
    }

    static DWORD WINAPI input_thread(LPVOID) {
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        if (GetConsoleMode(in, &mode)) {
            read_console(in);
        } else {
            char line[65536];
            while (fgets(line, sizeof(line), stdin)) {
                add_input_line(line);
            }
        }
        EnterCriticalSection(&input_lock);
        input_eof = true;
        LeaveCriticalSection(&input_lock);
        SetEvent(input_event);
        return 0;
    }

    static void start_input() {
        InitializeCriticalSection(&input_lock);
        input_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        CreateThread(NULL, 0, input_thread, NULL, 0, NULL);
    }

    // Returns true and a line of input if one is available; sets eof at
    // the end of the input.
    static bool read_input_line(std::string &line, bool &eof) {
        bool ok = false;
        EnterCriticalSection(&input_lock);
        if (!input_lines.empty()) {
            line = input_lines.front();
            input_lines.pop();
            ok = true;
        }
        eof = !ok && input_eof;
        LeaveCriticalSection(&input_lock);
        return ok;
    }

    // Blocks until a line of input (or the end of the input) is available.
    static void wait_for_input() {
        for (;;) {
            EnterCriticalSection(&input_lock);
            bool ready = !input_lines.empty() || input_eof;
            LeaveCriticalSection(&input_lock);
            if (ready) {
                return;
            }
            WaitForSingleObject(input_event, INFINITE);
        }
    }
#else
    static void start_input() {}

    /* Taken from fruit 2.1 source code */
    static bool input_available() {
        int val;
        fd_set set[1];
        struct timeval time_val[1];

        FD_ZERO(set);
        FD_SET(STDIN_FILENO, set);

        time_val->tv_sec = 0;
        time_val->tv_usec = 0;

        val = select(STDIN_FILENO + 1, set, NULL, NULL, time_val);
        return val > 0;
    }

    // Returns true and a line of input if one is available; sets eof at
    // the end of the input.
    static bool read_input_line(std::string &line, bool &eof) {
        char buf[65536];
        eof = false;
        if (!input_available()) {
            return false;
        }
        if (fgets(buf, sizeof(buf), stdin)) {
            line = buf;
            return true;
        }
        eof = feof(stdin) != 0;
        return false;
    }

    // Blocks until input (or the end of the input) is available.
    static void wait_for_input() {
        fd_set set[1];
        do {
            FD_ZERO(set);
            FD_SET(STDIN_FILENO, set);
        } while (select(STDIN_FILENO + 1, set, NULL, NULL, NULL) < 0 &&
                 errno == EINTR);
    }
#endif

    static bool illegal_move_seen = false;

    // Debug mode: stay in 2-player mode and read moves for both sides
    // from stdin, so the engine never thinks.
    static bool two_player() {
        return getenv("PSION_TWOPLAYER") != NULL;
    }

    // Offsets in Psion's data segment, found with memscan and find_state.py.
    const unsigned int MEM_BOARD = 0x28AC; // 64 bytes, a8..h8, a7..h7, ...
    const unsigned int MEM_SIDE = 0x725B;  // 00 White, FF Black to move
    const unsigned int MEM_PLY = 0x7260;   // word, counts the ponder move

    static bool read_state_at(unsigned int segment, PsionState &state) {
        unsigned int base = segment * 16;
        int f, r, white_kings = 0, black_kings = 0, unknown = 0;
        unsigned char side;
        if (base + 0x10000 > MEM_TotalPages() * 4096) {
            return false;
        }
        for (r = 0; r < 8; r++) {
            for (f = 0; f < 8; f++) {
                unsigned char v = mem_readb(base + MEM_BOARD + (7 - r) * 8 + f);
                char c = DecodeSquare(v);
                if ((c == 'P' || c == 'p') && (r == 0 || r == 7)) {
                    return false;
                }
                state.codes[f][r] = v;
                unknown += c == '?';
                white_kings += c == 'K';
                black_kings += c == 'k';
                state.board[f][r] = c;
            }
        }
        // A handful of unknown codes (promoted pieces) is resolved later;
        // many mean this is not Psion's board.
        if (white_kings != 1 || black_kings != 1 || unknown > 2) {
            return false;
        }
        state.unknown = unknown;
        side = mem_readb(base + MEM_SIDE);
        if (side != 0x00 && side != 0xFF) {
            return false;
        }
        state.white_to_move = side == 0x00;
        state.ply = mem_readw(base + MEM_PLY);
        if (state.ply > 2000) {
            return false;
        }
        // While pondering, the predicted reply is included in the ply
        // counter but not played on the board.
        state.pondering = (state.ply % 2 == 0) != state.white_to_move;
        if (state.pondering) {
            state.ply--;
        }
        state.segment = segment;
        return true;
    }

    // Psion's data segment is located once, while Psion shows the start
    // position (it always does when the adapter starts it), and is never
    // changed after that: memory holds other board-like data that a later
    // search could mistake for the game.
    static bool is_initial_state(unsigned int segment, PsionState &state) {
        return read_state_at(segment, state) && IsStartPosition(state.board) &&
               state.ply == 0;
    }

    bool ReadGameState(PsionState &state) {
        static unsigned int segment = 0;
        unsigned int s;
        if (segment) {
            return read_state_at(segment, state);
        }
        // DS is normally Psion's data segment when the screen is updated.
        s = SegValue(ds);
        if (s && is_initial_state(s, state)) {
            segment = s;
        } else {
            // The full scan is expensive, so do not repeat it too often.
            static unsigned int last_scan = 0;
            static bool scanned = false;
            if (scanned && GetTicks() - last_scan < 500) {
                return false;
            }
            scanned = true;
            last_scan = GetTicks();
            for (s = 1; s < 0xA000 && !segment; s++) {
                if (is_initial_state(s, state)) {
                    segment = s;
                }
            }
            if (!segment) {
                return false;
            }
        }
        say("Psion's data segment is %04X\n", segment);
        return true;
    }

    // A memory state is accepted once it has been unchanged for this long
    // if no single move explains it (a new game, set-up mode, a resync).
    const unsigned int STABLE_MS = 200;
    // ... or this long if it differs from the last one by a complete move.
    // (Psion's board does not change during a search, but while Psion
    // plays a move it passes through states such as a piece lifted.)
    const unsigned int MOVE_STABLE_MS = 20;
    // ... or this long if it looks like Psion is still playing a move: only
    // the board changed, not the ply or the side to move, or pieces were
    // only removed (a piece is lifted). This takes longer when the
    // emulation runs slowly.
    const unsigned int MID_MOVE_STABLE_MS = 3000;

    static PsionState known; // last accepted state
    static bool have_known = false;
    static bool settled = false; // memory showed the known state when
                                 // last read successfully

    static bool same_state(const PsionState &a, const PsionState &b) {
        return a.ply == b.ply && a.white_to_move == b.white_to_move &&
               memcmp(a.board, b.board, sizeof(a.board)) == 0;
    }

    // Whether after is before with some pieces removed and nothing else
    // changed. No move does that: the moving piece lands somewhere.
    static bool pieces_lifted(const char before[8][8], const char after[8][8]) {
        int f, r;
        bool removed = false;
        for (f = 0; f < 8; f++) {
            for (r = 0; r < 8; r++) {
                if (after[f][r] == before[f][r]) {
                    continue;
                }
                if (after[f][r] != '.') {
                    return false;
                }
                removed = true;
            }
        }
        return removed;
    }

    static char ocr_board_buf[8][8]; // last OCR'd board

    // The piece typed at the last promotion prompt.
    static char last_promotion = 0;
    static unsigned int last_promotion_ticks = 0;
    static bool promotion_in_progress();

    // Guesses the pieces on squares with unknown codes (promoted pieces):
    // the piece just typed at a promotion prompt, or what the screen shows.
    static bool resolve_unknown_squares(PsionState &cur) {
        int f, r;
        if (promotion_in_progress()) {
            return false; // the board is in flux until a piece is chosen
        }
        for (f = 0; f < 8; f++) {
            for (r = 0; r < 8; r++) {
                if (cur.board[f][r] != '?') {
                    continue;
                }
                bool white = !(cur.codes[f][r] & 0x80);
                char c = ocr_board_buf[f][r];
                if (last_promotion &&
                    GetTicks() - last_promotion_ticks < 5000 &&
                    (r == 7 || r == 0)) {
                    c = white ? toupper(last_promotion) : last_promotion;
                }
                if (!c || !strchr("PNBRQKpnbrqk", c) ||
                    (isupper(c) != 0) != white) {
                    return false;
                }
                cur.board[f][r] = c;
            }
        }
        return true;
    }

    static void learn_unknown_squares(const PsionState &cur) {
        int f, r;
        for (f = 0; f < 8; f++) {
            for (r = 0; r < 8; r++) {
                if (DecodeSquare(cur.codes[f][r]) == '?') {
                    say("Learned square code %02X: %c\n", cur.codes[f][r],
                        cur.board[f][r]);
                    LearnSquareCode(cur.codes[f][r], cur.board[f][r]);
                }
            }
        }
    }

    // Set while takebacks are on their way: the number of moves left then.
    static int takeback_to = -1;
    static unsigned int takeback_since;

    // Set while the adapter sets up setup_start in Psion's set-up mode.
    static bool setting_up = false;
    static StartPosition setup_start;
    static bool in_setup_mode();
    static bool game_over_shown();

    // Track the game through Psion's memory. Returns true and sets move
    // when a new move has been played.
    static bool PollGameState(std::string &move) {
        static PsionState pending;
        static bool have_pending = false;
        static unsigned int pending_since = 0;
        PsionState cur;
        if (!ReadGameState(cur)) {
            // Psion flashes the kings to announce a draw or mate, which makes
            // every other read fail. So ignore failed reads: the state read in
            // between still has to stay unchanged for STABLE_MS (and settled
            // is kept, so that the emulation can be stopped after a mate).
            return false;
        }
        settled = have_known && same_state(cur, known);
        if (!have_pending || !same_state(cur, pending)) {
            pending = cur;
            pending_since = GetTicks();
            have_pending = true;
            return false;
        }
        if (setting_up) {
            // Only the position set up counts, once Psion has left set-up
            // mode. (Until then the side byte is the colour being set up.)
            if (GetTicks() - pending_since < STABLE_MS || in_setup_mode() ||
                cur.unknown || cur.white_to_move != setup_start.white ||
                memcmp(cur.board, setup_start.board, sizeof(cur.board))) {
                return false;
            }
            say("Position set up at ply %d\n", cur.ply);
            setting_up = false;
            game_state.start = setup_start;
            game_state.base_ply = cur.ply;
            game_state.moves.clear();
            known = cur;
            have_known = true;
            settled = true;
            return false;
        }
        std::string candidate;
        bool one_move =
            have_known && !cur.unknown &&
            DeriveMove(known.board, cur.board, known.white_to_move, candidate);
        bool mid_move =
            have_known && ((cur.ply == known.ply &&
                            cur.white_to_move == known.white_to_move) ||
                           pieces_lifted(known.board, cur.board));
        // The position the takebacks lead to counts at once, like a move.
        if (takeback_to >= 0 && !one_move && !cur.unknown &&
            !game_state.thinking &&
            cur.ply == game_state.base_ply + takeback_to) {
            Board b;
            BoardAfter(game_state.start, game_state.moves, takeback_to, b);
            one_move = memcmp(b, cur.board, sizeof(b)) == 0;
            mid_move = mid_move && !one_move;
        }
        if (GetTicks() - pending_since < (one_move   ? MOVE_STABLE_MS
                                          : mid_move ? MID_MOVE_STABLE_MS
                                                     : STABLE_MS)) {
            return false;
        }
        if (cur.unknown && !resolve_unknown_squares(cur)) {
            return false;
        }
        // While the engine searches its own move, the side byte (and hence
        // the corrected ply) is unreliable but the board is not. So track
        // the game through board changes, counting plies from the last state
        // read while the engine was idle.
        // Once Psion has announced the end of the game, its ply counter goes
        // back one ply (with the side to move) on the same board.
        if (have_known &&
            memcmp(cur.board, known.board, sizeof(cur.board)) == 0 &&
            (game_state.thinking || game_over_shown() ||
             (cur.ply == known.ply &&
              cur.white_to_move == known.white_to_move))) {
            return false;
        }
        bool ok = have_known &&
                  DeriveMove(known.board, cur.board, known.white_to_move, move);
        if (!ok && cur.unknown) {
            return false; // only trust a guessed piece if it explains a move
        }
        if (ok && cur.unknown) {
            learn_unknown_squares(cur);
        }
        if (ok) {
            cur.ply = known.ply + 1;
            cur.white_to_move = !known.white_to_move;
        } else if (have_known && game_state.thinking) {
            return false; // transient state during the search
        } else {
            say("Synchronized at ply %d, %s to move\n", cur.ply,
                cur.white_to_move ? "White" : "Black");
            int n = cur.ply - game_state.base_ply;
            if (IsStartPosition(cur.board) && cur.ply == 0) {
                // A new game
                game_state.start = StandardStart();
                game_state.base_ply = 0;
                game_state.moves.clear();
            } else if (n >= 0 && n <= (int)game_state.moves.size()) {
                game_state.moves.resize(n);
            } else {
                game_state.moves.clear();
                if (!game_state.start.standard) {
                    // Where the game started is not known now: set up the
                    // position again.
                    game_state.start.board[0][0] = '?';
                }
            }
        }
        known = cur;
        have_known = true;
        return ok;
    }

    static bool white_to_move() {
        return have_known ? known.white_to_move
                          : game_state.moves.size() % 2 == 0;
    }

    // Last non-blank contents of each screen row (some rows blink).
    static std::string screen_rows[TEXT_HEIGHT];

    // Row 20: "SET-UP POSITION  WHITE" (or BLACK) in set-up mode.
    static bool in_setup_mode() {
        return screen_rows[20].find("SET-UP POSITION") != std::string::npos;
    }

    static bool game_over_text(const std::string &r) {
        return r.compare(0, 5, "DRAW ") == 0 ||
               r.compare(0, 10, "CHECKMATE ") == 0 ||
               r.compare(0, 10, "STALEMATE ") == 0;
    }

    // The board in memory when Psion showed the result: the text stays on
    // the screen a little after a takeback, so it counts only as long as
    // the board is the same. Psion flashes the kings then, which makes
    // every other read fail: the board is read once one works, unless a
    // key was pressed since the text appeared.
    static Board game_over_board;
    static bool game_over_board_set = false;
    static bool game_over_board_wanted = false;
    static unsigned int game_over_since = 0;
    static unsigned int last_key_ticks();

    static void read_game_over_board() {
        PsionState cur;
        if (!game_over_board_wanted) {
            return;
        }
        if (last_key_ticks() > game_over_since) {
            game_over_board_wanted = false;
        } else if (ReadGameState(cur) && !cur.unknown) {
            memcpy(game_over_board, cur.board, sizeof(Board));
            game_over_board_set = true;
            game_over_board_wanted = false;
        }
    }

    // "DRAW", "CHECKMATE" or "STALEMATE".
    static std::string game_over_result() {
        return screen_rows[20].substr(0, screen_rows[20].find(' '));
    }

    // Psion considers the game over in the position on its board: it
    // shows the result instead of "YOUR MOVE", and does not search.
    static bool game_over_shown() {
        read_game_over_board();
        return game_over_text(screen_rows[20]) && game_over_board_set &&
               have_known &&
               memcmp(known.board, game_over_board, sizeof(Board)) == 0;
    }

    // Row 6 names the players: "PLAYER PLAYER" in 2-player mode.
    static bool two_player_mode() {
        const std::string &r = screen_rows[6];
        return r.find("PLAYER") != std::string::npos &&
               r.find("PROGRAM") == std::string::npos;
    }

    // The level used: 4 minutes per move, longer than any search the
    // adapter asks for (MOVE-NOW ends it). Not "Infinite": at that level
    // Psion stops reading the keyboard once its search reaches the
    // maximum depth, which happens at once in a locked pawn ending.
    // "LEVEL 11  4 Min", or "LEVEL E 11  4 Min" in easy mode.
    static bool level_ok() {
        const std::string &r = screen_rows[22];
        return r.compare(0, 6, "LEVEL ") == 0 &&
               r.find(" 11  4 Min") != std::string::npos;
    }

    // In easy mode Psion does not ponder. (While pondering, it can hang
    // and stop reading the keyboard, e.g. in a locked pawn ending.)
    static bool easy_mode() {
        return screen_rows[22].compare(0, 8, "LEVEL E ") == 0;
    }

    // In analysis mode Psion shows its score and principal variation on
    // the bottom row while it thinks.
    static bool analysis_on() {
        return screen_rows[22].find("ANALYSIS ON") != std::string::npos;
    }

    // The bottom row as last drawn (screen_rows keeps old contents), and
    // since when. It is used once it has not changed for a while: when
    // Psion redraws it, the characters that change are erased first.
    static std::string analysis_row;
    static unsigned int analysis_row_since;
    const unsigned int ANALYSIS_STABLE_MS = 50;

    // The piece to promote to, typed once Psion asks for it. Typed any
    // earlier, the letter would be taken as a command (Q is QUIT).
    static char pending_promotion = 0;
    static unsigned int promotion_since = 0;
    static bool promotion_prompt = false; // "PROMOTE TO Q,R,B,N ?" shown
    const unsigned int PROMOTION_TIMEOUT_MS = 3000;

    static void SendUciMove(const std::string &m) {
        illegal_move_seen = false;
        SendMove(white_to_move(), m[0] - 'a', m[1] - '1', m[2] - 'a',
                 m[3] - '1');
        pending_promotion = m.size() == 5 ? m[4] : 0;
        promotion_since = GetTicks();
    }

    static bool promotion_in_progress() {
        return pending_promotion || promotion_prompt;
    }

    static void SendPromotion() {
        if (!pending_promotion || !keyboard_buffer.Empty()) {
            return;
        }
        if (!promotion_prompt) {
            if (GetTicks() - promotion_since > PROMOTION_TIMEOUT_MS) {
                say("No promotion prompt\n");
                pending_promotion = 0;
            }
            return;
        }
        last_promotion = pending_promotion;
        last_promotion_ticks = GetTicks();
        switch (pending_promotion) {
        case 'q':
            keyboard_buffer.SendKey(KBD_q);
            break;
        case 'r':
            keyboard_buffer.SendKey(KBD_r);
            break;
        case 'b':
            keyboard_buffer.SendKey(KBD_b);
            break;
        case 'n':
            keyboard_buffer.SendKey(KBD_n);
            break;
        }
        pending_promotion = 0;
        promotion_prompt = false;
    }

    static void say_board(char board[8][8]) {
        int f, r;
        for (r = 7; r >= 0; r--) {
            for (f = 0; f < 8; f++) {
                say("%c", board[f][r]);
            }
            say("\n");
        }
    }

    /* UCI */

    // The "Move Overhead" option: the time a move costs on the clock
    // besides Psion's thinking (key delays, MOVE_STABLE_MS and polling, the
    // GUI, the network). The default is about the median of what a move
    // costs with the engine alone, on an i5-1135G7: 32 ms.
    const unsigned int DEFAULT_MOVE_OVERHEAD_MS = 30;
    const unsigned int MAX_MOVE_OVERHEAD_MS = 5000;
    static unsigned int move_overhead_ms = DEFAULT_MOVE_OVERHEAD_MS;
    // The "Cycles" option: "max" (as fast as the host allows) or a fixed
    // number of emulated instructions per millisecond, as with --cycles.
    // (DOSBox's "cycles=max limit N" does not work here: its auto-adjust
    // then lowers the cycles far below N and does not raise them again.)
    // Higher fixed values than the host can reach slow the emulated clock
    // down; far higher ones (10 million) also hide the analysis.
    const unsigned int MAX_CYCLES = 1000000;

    // The speed of the emulated PC (0 for "max"). A fixed speed applies
    // only from the 'c' that starts a search until MOVE-NOW: setting up
    // positions, entering moves and taking back run at full speed. (The
    // screen shows the search too late: it would run partly at full
    // speed.) (Set directly rather than through the CPU section,
    // whose Change_Config would also reset the CPU core.)
    static unsigned int search_cycles = 0;
    static bool search_speed_on = false;
    static Bit32s auto_cycles = 0; // where the auto-adjust was

    static void dosbox_cycles(unsigned int cycles) {
        CPU_CycleLimit = -1;
        if (cycles == 0) {
            if (!CPU_CycleAutoAdjust && auto_cycles > 0) {
                // Go on from where the auto-adjust was, not from the
                // fixed speed, from which it would take a while to rise.
                CPU_CycleMax = auto_cycles;
            }
            CPU_CycleAutoAdjust = true;
        } else {
            if (CPU_CycleAutoAdjust) {
                auto_cycles = CPU_CycleMax;
            }
            CPU_CycleAutoAdjust = false;
            CPU_CycleMax = (Bit32s)cycles;
        }
        // DOSBox goes on measuring the load at a fixed speed. Back to auto,
        // the idle time at a low speed would multiply the cycles (by up to
        // 10 after each search), until a millisecond of emulation takes
        // seconds.
        DOSBOX_ResetTicks();
    }

    // on: Psion has started a search; off: it is told to move, or done.
    static void search_speed(bool on) {
        if (on != search_speed_on) {
            say("Speed switch to %s at %d cycles (%s)\n",
                on ? "search" : "full", (int)CPU_CycleMax,
                CPU_CycleAutoAdjust ? "auto" : "fixed");
            search_speed_on = on;
            dosbox_cycles(on ? search_cycles : 0);
        }
    }

    // UCI_LimitStrength and UCI_Elo: a fixed speed for the strength asked
    // for. Switched off, the speed asked for with Cycles (or --cycles)
    // applies again.
    static bool limit_strength = false;
    static int uci_elo = DEFAULT_ELO;
    static unsigned int base_cycles = 0; // 0 for "max"

    // In UCI mode: the speed of --cycles becomes the search speed.
    static void init_speed() {
        base_cycles = CPU_CycleAutoAdjust ? 0 : (unsigned int)CPU_CycleMax;
        search_cycles = base_cycles;
        search_speed_on = false;
        dosbox_cycles(0);
    }

    static void apply_speed() {
        search_cycles = limit_strength ? EloCycles(uci_elo) : base_cycles;
        if (limit_strength) {
            say("UCI_Elo %d: ", uci_elo);
        }
        if (search_cycles) {
            say("Cycles: %u per ms while searching\n", search_cycles);
        } else {
            say("Cycles: max\n");
        }
        if (search_speed_on) {
            dosbox_cycles(search_cycles);
        }
    }
    // Pause after a key action before the screen is consulted again.
    const unsigned int ACTION_GAP_MS = 300;
    const unsigned int MOVE_TIMEOUT_MS = 5000;
    // Without a sign of thinking this long after 'c', a game over shown is
    // taken to be the reason.
    const unsigned int GAME_OVER_MS = 300;
    const int MAX_RETRIES = 3;
    const int MAX_TWO_PLAYER_PRESSES = 5;
    const unsigned int SYNC_TIMEOUT_MS = 20000;
    const unsigned int SETUP_TIMEOUT_MS = 10000;

    enum SearchState { SEARCH_NONE, SEARCH_RUNNING, SEARCH_HOLDING };
    enum SetupStage { SETUP_NONE, SETUP_ENTERING, SETUP_LEAVING };

    static struct {
        bool has_target;  // a position has been given
        bool unsupported; // ... but we cannot set it up
        StartPosition target_start;
        std::vector<std::string> target; // moves from target_start
        SetupStage setup;
        unsigned int setup_since;
        bool setup_keys_done; // Psion has taken the set-up keys
        bool new_game;        // ucinewgame: press '$' before setting
                              // up the next position
        bool move_added;      // Psion's move was added to the target
        bool readyok_pending;
        bool go_pending;
        SearchState search;
        bool infinite; // wait for "stop" (or "ponderhit") before bestmove
        bool ponder;   // "go ponder", until "ponderhit" or "stop"
        bool stop_received;
        unsigned int think_ms;       // 0: until "stop"
        unsigned int ponderhit_ms;   // think_ms from "ponderhit" on
        unsigned int search_start;   // when "go" was received
        unsigned int thinking_start; // when Psion started to think
        bool movenow_sent;
        unsigned int movenow_ticks;
        std::string held_move; // found before "stop" in infinite mode
        unsigned int last_action;
        bool action_done;        // its effect seen: no need to wait
        bool two_player_pending; // '2' pressed after the engine moved
        bool saw_program;        // the screen showed Psion thinking
        Board search_board;      // the position searched
        bool search_white;
        std::string reported_move;   // played, to report after the last
        unsigned int reported_since; // analysis line
        bool inflight;               // a position move has been sent
        size_t inflight_size;
        unsigned int inflight_since;
        int retries;
        bool level_set;
        int two_player_presses;    // '2' pressed without effect
        int analysis_presses;      // 'a' pressed without effect
        int easy_presses;          // 'e' pressed without effect
        std::string last_analysis; // analysis row last seen
        std::string info_move;     // first move of the last info pv
        std::string info_ponder;   // ... and the second, to ponder on
        std::vector<std::string> info_pv; // ... and all of it
        bool info_mate;            // the score of the last info pv:
        int info_score;            // mate in moves or centipawns
        size_t info_depth;         // longest pv in this search
        size_t sync_progress_size; // moves played when last checked
        unsigned int sync_progress_ticks;
    } uci;

    static void uci_send(const char *format, ...) {
        va_list args;
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);
        fflush(stdout);
    }

    // How long after "go" (or "ponderhit") Psion started to think: 0 if
    // it was thinking already, as after a ponder hit.
    static unsigned int setup_time() {
        return uci.thinking_start > uci.search_start
                   ? uci.thinking_start - uci.search_start
                   : 0;
    }

    static unsigned int last_key_ticks() {
        return uci.last_action;
    }

    static void press(KBD_KEYS k) {
        keyboard_buffer.SendKey(k);
        uci.last_action = GetTicks();
        uci.action_done = false;
    }

    static void press(KBD_KEYS modifier, KBD_KEYS k) {
        keyboard_buffer.SendKey(modifier, k);
        uci.last_action = GetTicks();
        uci.action_done = false;
    }

    static bool uci_synced() {
        return !uci.has_target || uci.unsupported ||
               (SameStart(game_state.start, uci.target_start) &&
                game_state.moves == uci.target);
    }

    static void finish_search(const std::string &move) {
        search_speed(false);
        uci.search = SEARCH_NONE;
        game_state.thinking = false;
        if (!uci.info_move.empty() && move != uci.info_move && move != "0000") {
            std::string info;
            char buf[80];
            snprintf(buf, sizeof(buf), "info depth %u time %u score %s %d",
                     (unsigned int)uci.info_depth,
                     GetTicks() - uci.thinking_start,
                     uci.info_mate ? "mate" : "cp", uci.info_score);
            info = buf;
            std::vector<std::string> pv = uci.info_pv;
            if (move.size() == 5 && uci.info_move.size() == 5 &&
                move.compare(0, 4, uci.info_move, 0, 4) == 0 &&
                CorrectPromotion(uci.search_board, uci.search_white, pv,
                                 move)) {
                // Psion shows a promotion without the piece, and the one
                // guessed was not the one played: the same line and score,
                // with the right piece, if all of the line still works.
                info += " pv";
                for (size_t i = 0; i < pv.size(); i++) {
                    info += " " + pv[i];
                }
                uci.info_move = move;
                uci.info_ponder = pv.size() > 1 ? pv[1] : "";
            } else {
                // Stopped by MOVE-NOW, Psion plays the best move found so
                // far, which it may not have displayed yet (also when the
                // line shown does not work with the piece it promotes to).
                // Each iteration starts by showing the previous best move,
                // and a new best one beats the last score shown: a lower
                // bound.
                info += " lowerbound pv " + move;
            }
            uci_send("%s\n", info.c_str());
        }
        if (move == uci.info_move && !uci.info_ponder.empty()) {
            uci_send("bestmove %s ponder %s\n", move.c_str(),
                     uci.info_ponder.c_str());
        } else {
            uci_send("bestmove %s\n", move.c_str());
        }
    }

    static void report_move(const std::string move) {
        uci.reported_move.clear();
        if (uci.infinite && !uci.stop_received) {
            uci.held_move = move;
            uci.search = SEARCH_HOLDING;
        } else {
            finish_search(move);
        }
    }

    // Called when Psion plays a move during a search.
    static void UciEngineMoved(const std::string &move) {
        if (uci.search != SEARCH_RUNNING) {
            return;
        }
        game_state.thinking = false;
        press(KBD_2); // back to 2-player mode
        // That it worked shows on the screen, unless Psion moved before
        // the screen showed it thinking.
        uci.two_player_pending = uci.saw_program;
        if (uci.has_target && !uci.unsupported) {
            uci.target.push_back(move);
            uci.move_added = true;
        }
        // Psion may have just drawn the analysis line for this move (e.g.
        // a mate it found at once): report that first.
        if (analysis_row != uci.last_analysis &&
            GetTicks() - analysis_row_since < ANALYSIS_STABLE_MS) {
            uci.reported_move = move;
            uci.reported_since = GetTicks();
            return;
        }
        report_move(move);
    }

    static void UciGo(const std::vector<std::string> &tokens) {
        GoParams go = ParseGo(tokens);
        if (!uci.has_target) {
            // No "position" yet: the start position.
            uci.has_target = true;
            uci.unsupported = false;
            uci.target_start = StandardStart();
            uci.target.clear();
            uci.sync_progress_size = game_state.moves.size();
            uci.sync_progress_ticks = GetTicks();
        }
        if (uci.move_added) {
            // A "go" without a new "position": the GUI's position is still
            // the one before Psion's last move, which is taken back.
            uci.move_added = false;
            uci.target.pop_back();
            uci.sync_progress_size = game_state.moves.size();
            uci.sync_progress_ticks = GetTicks();
        }
        if (uci.has_target && uci.unsupported) {
            uci_send("bestmove 0000\n");
            return;
        }
        // The side to move once the position has been set up.
        const StartPosition &start =
            uci.has_target ? uci.target_start : game_state.start;
        size_t plies =
            uci.has_target ? uci.target.size() : game_state.moves.size();
        bool white = start.white == (plies % 2 == 0);
        // A ponder search ends like an infinite one, unless "ponderhit"
        // comes first: then the time for the move counts from there.
        uci.infinite = go.infinite || go.ponder;
        uci.ponder = go.ponder;
        uci.ponderhit_ms = ThinkTime(go, white, move_overhead_ms);
        uci.think_ms = go.ponder ? 0 : uci.ponderhit_ms;
        uci.stop_received = false;
        uci.go_pending = true;
        // The GUI's clock runs from now, including the time it takes to set
        // up the position.
        uci.search_start = GetTicks();
    }

    static void UciPosition(const std::vector<std::string> &tokens) {
        PositionRequest req = ParsePosition(tokens);
        uci.sync_progress_size = game_state.moves.size();
        uci.sync_progress_ticks = GetTicks();
        uci.has_target = true;
        uci.unsupported = !req.supported;
        uci.target_start = req.start;
        uci.target = req.moves;
        uci.move_added = false;
        uci.retries = 0;
        if (!req.supported) {
            uci_send("info string %s\n", req.error.c_str());
        }
    }

    // Commands kept back until the search has ended. A GUI may send the
    // next "position" and "go" right after "stop" (Polyglot does, after a
    // ponder miss), before the stopped search has sent its bestmove. The
    // commands after them wait too, to keep their order, except "stop" and
    // "ponderhit" while no "go" waits: they are for the current search.
    static std::vector<std::string> deferred_commands;

    static bool go_deferred() {
        for (size_t i = 0; i < deferred_commands.size(); i++) {
            std::vector<std::string> tokens =
                Tokenize(deferred_commands[i].c_str());
            if (!tokens.empty() && tokens[0] == "go") {
                return true;
            }
        }
        return false;
    }

    // A "position", "go" or "ucinewgame" waits while a search is running
    // or about to start.
    static bool must_wait(const std::vector<std::string> &tokens) {
        return !tokens.empty() &&
               (tokens[0] == "position" || tokens[0] == "go" ||
                tokens[0] == "ucinewgame") &&
               (uci.search != SEARCH_NONE || uci.go_pending);
    }

    // from_queue: a command from deferred_commands, now due.
    static void UciCommand(const char *line, bool from_queue = false) {
        std::vector<std::string> tokens = Tokenize(line);
        if (tokens.empty()) {
            return;
        }
        const std::string &cmd = tokens[0];
        bool for_search = (cmd == "stop" || cmd == "ponderhit") &&
                          !go_deferred();
        if (!from_queue && cmd != "quit" && !for_search &&
            (!deferred_commands.empty() || must_wait(tokens))) {
            deferred_commands.push_back(line);
            return;
        }
        if (cmd == "uci") {
            if (!uci_mode) {
                init_speed();
            }
            uci_mode = true;
            uci_send("id name Psion Chess 1985\n");
            uci_send("id author Richard Lang\n");
            uci_send("option name Ponder type check default false\n");
            uci_send("option name Move Overhead type spin default %u min 0 "
                     "max %u\n",
                     DEFAULT_MOVE_OVERHEAD_MS, MAX_MOVE_OVERHEAD_MS);
            uci_send("option name Cycles type string default max\n");
            uci_send("option name UCI_LimitStrength type check default false\n");
            uci_send("option name UCI_Elo type spin default %d min %d max %d\n",
                     DEFAULT_ELO, MIN_ELO, MAX_ELO);
            uci_send("uciok\n");
        } else if (cmd == "isready") {
            if (uci.search != SEARCH_NONE) {
                uci_send("readyok\n");
            } else {
                uci.readyok_pending = true;
            }
        } else if (cmd == "ucinewgame") {
            // A new game in Psion ('$'), in the start position, which is
            // then also the position a "go" searches.
            uci.new_game = true;
            uci.sync_progress_size = game_state.moves.size();
            uci.sync_progress_ticks = GetTicks();
            uci.has_target = true;
            uci.unsupported = false;
            uci.target_start = StandardStart();
            uci.target.clear();
            uci.move_added = false;
            uci.retries = 0;
        } else if (cmd == "position") {
            if (uci.search == SEARCH_NONE) {
                UciPosition(tokens);
            }
        } else if (cmd == "go") {
            if (uci.search == SEARCH_NONE) {
                UciGo(tokens);
            }
        } else if (cmd == "ponderhit") {
            // The opponent played the move pondered on: a normal search
            // from now on (Psion goes on with the search it is doing).
            if (!uci.ponder) {
                return;
            }
            uci.ponder = false;
            uci.infinite = false;
            uci.think_ms = uci.ponderhit_ms;
            uci.search_start = GetTicks();
            if (uci.search == SEARCH_HOLDING) {
                finish_search(uci.held_move);
            }
        } else if (cmd == "stop") {
            uci.stop_received = true;
            if (uci.go_pending) {
                // The search has not started yet (the position is still
                // being set up). After "go ponder" the GUI ignores the
                // move; otherwise Psion searches briefly once it can.
                if (uci.ponder) {
                    uci.go_pending = false;
                    uci.ponder = false;
                    uci_send("bestmove 0000\n");
                } else {
                    uci.infinite = false;
                    uci.think_ms = 1;
                }
                return;
            }
            uci.ponder = false;
            if (uci.search == SEARCH_HOLDING) {
                finish_search(uci.held_move);
            } else if (uci.search == SEARCH_RUNNING && !uci.movenow_sent &&
                       uci.reported_move.empty()) {
                search_speed(false);
                press(KBD_m);
                uci.movenow_sent = true;
                uci.movenow_ticks = GetTicks();
            }
        } else if (cmd == "setoption") {
            std::string name, value;
            if (!ParseSetOption(tokens, name, value)) {
                return;
            }
            if (SameOptionName(name, "Move Overhead")) {
                char *end;
                long v = strtol(value.c_str(), &end, 10);
                if (value.empty() || *end || v < 0 ||
                    v > (long)MAX_MOVE_OVERHEAD_MS) {
                    uci_send("info string Move Overhead must be 0 to %u\n",
                             MAX_MOVE_OVERHEAD_MS);
                } else {
                    move_overhead_ms = (unsigned int)v;
                }
            } else if (SameOptionName(name, "Cycles")) {
                char *end;
                long v = strtol(value.c_str(), &end, 10);
                if (SameOptionName(value, "max")) {
                    base_cycles = 0;
                    apply_speed();
                } else if (value.empty() || *end || v < 1 ||
                           v > (long)MAX_CYCLES) {
                    uci_send("info string Cycles must be max or 1 to %u\n",
                             MAX_CYCLES);
                } else {
                    base_cycles = (unsigned int)v;
                    apply_speed();
                }
            } else if (SameOptionName(name, "UCI_LimitStrength")) {
                if (SameOptionName(value, "true") ||
                    SameOptionName(value, "false")) {
                    limit_strength = SameOptionName(value, "true");
                    apply_speed();
                } else {
                    uci_send("info string UCI_LimitStrength must be true or "
                             "false\n");
                }
            } else if (SameOptionName(name, "UCI_Elo")) {
                char *end;
                long v = strtol(value.c_str(), &end, 10);
                if (value.empty() || *end || v < MIN_ELO || v > MAX_ELO) {
                    uci_send("info string UCI_Elo must be %d to %d\n",
                             MIN_ELO, MAX_ELO);
                } else {
                    uci_elo = (int)v;
                    apply_speed();
                }
            }
        } else if (cmd == "debug") {
            if (tokens.size() > 1) {
                verbose = tokens[1] == "on";
            }
        } else if (cmd == "quit") {
            exit(0);
        }
    }

    // Sets the level (see level_ok()).
    // Returns true once that is done.
    static bool level_set_check() {
        if (uci.level_set) {
            return true;
        }
        if (level_ok()) {
            uci.level_set = true;
            return true;
        }
        if (screen_rows[22].find("LEVEL") != std::string::npos) {
            press(KBD_l);
        }
        return false;
    }

    // Switches easy mode on. Returns true once that is done, or when it
    // does not work.
    const int MAX_EASY_PRESSES = 4;
    static bool easy_check() {
        if (easy_mode()) {
            uci.easy_presses = 0;
            return true;
        }
        if (uci.easy_presses >= MAX_EASY_PRESSES) {
            return true;
        }
        uci.easy_presses++;
        press(KBD_e);
        return false;
    }

    // Switches analysis mode on (for info lines). Returns true once that
    // is done, or when it does not work: the engine plays without.
    const int MAX_ANALYSIS_PRESSES = 4;
    static bool analysis_check() {
        if (analysis_on()) {
            uci.analysis_presses = 0;
            return true;
        }
        if (uci.analysis_presses >= MAX_ANALYSIS_PRESSES) {
            return true;
        }
        uci.analysis_presses++;
        press(KBD_a);
        return false;
    }

    // Sends an info line for each new line on Psion's analysis display
    // during a search.
    static void UciAnalysis() {
        if (uci.search != SEARCH_RUNNING || analysis_row == uci.last_analysis ||
            GetTicks() - analysis_row_since < ANALYSIS_STABLE_MS) {
            return;
        }
        uci.last_analysis = analysis_row;
        // The position searched: Psion may have played its move already.
        bool white = uci.search_white;
        Analysis a;
        if (!ParseAnalysis(analysis_row, white, a)) {
            return;
        }
        // Only complete lines for this position.
        if (!a.complete || !CheckPv(uci.search_board, white, a.pv)) {
            say("Analysis line not used: %s\n", analysis_row.c_str());
            return;
        }
        // Psion does not show the depth; the length of the longest line
        // seen comes close (and GUIs such as Polyglot need a depth).
        uci.info_depth = std::max(uci.info_depth, a.pv.size());
        std::string info;
        char buf[80];
        snprintf(buf, sizeof(buf), "info depth %u time %u score %s %d pv",
                 (unsigned int)uci.info_depth, GetTicks() - uci.thinking_start,
                 a.mate ? "mate" : "cp", a.score);
        info = buf;
        for (size_t i = 0; i < a.pv.size(); i++) {
            info += " " + a.pv[i];
        }
        uci_send("%s\n", info.c_str());
        uci.info_move = a.pv[0];
        uci.info_ponder = a.pv.size() > 1 ? a.pv[1] : "";
        uci.info_pv = a.pv;
        uci.info_mate = a.mate;
        uci.info_score = a.score;
    }

    // Sets up uci.target_start in Psion's set-up mode: F3, then the keys
    // from SetupKeys() once the screen shows set-up mode, then F3 again to
    // leave it. PollGameState() sees when that has worked.
    static void begin_setup() {
        say("Setting up the position\n");
        uci.setup = SETUP_ENTERING;
        uci.setup_since = GetTicks();
        press(KBD_f3);
    }

    // Starts the game at uci.target_start: a new game, and then the set-up.
    // Set-up mode is only entered from a new game: SetupKeys() starts from
    // the start position, entered with Black to move Psion leaves it with
    // a side to move byte that is neither 00 nor FF, and in a game from an
    // earlier set-up it can fail.
    static void start_target_game() {
        if (uci.target_start.standard || !game_state.start.standard ||
            !game_state.moves.empty() || known.ply != 0) {
            press(KBD_leftshift, KBD_4); // '$': new game
        } else {
            begin_setup();
        }
    }

    static void setup_failed() {
        say("Set-up failed\n");
        uci_send("info string Cannot set up the position\n");
        uci.unsupported = true;
        uci.setup = SETUP_NONE;
        setting_up = false;
        press(KBD_leftshift, KBD_4); // leaves set-up mode: a new game
    }

    static void step_setup() {
        unsigned int now = GetTicks();
        if (uci.setup == SETUP_ENTERING) {
            if (!in_setup_mode()) {
                if (now - uci.setup_since > SETUP_TIMEOUT_MS) {
                    setup_failed();
                }
                return;
            }
            bool colour_white =
                screen_rows[20].find("WHITE") != std::string::npos;
            std::string keys = SetupKeys(uci.target_start, colour_white);
            for (size_t i = 0; i < keys.size(); i++) {
                static const char *chars = "UDLRce0kqrbnp";
                static const KBD_KEYS codes[] = {
                    KBD_up, KBD_down, KBD_left, KBD_right, KBD_c, KBD_e, KBD_0,
                    KBD_k,  KBD_q,    KBD_r,    KBD_b,     KBD_n, KBD_p};
                press(codes[strchr(chars, keys[i]) - chars]);
            }
            press(KBD_f3);
            setup_start = uci.target_start;
            setting_up = true;
            uci.setup = SETUP_LEAVING;
            uci.setup_since = now;
            uci.setup_keys_done = false;
        } else if (!setting_up) {
            uci.setup = SETUP_NONE; // done
        } else if (!uci.setup_keys_done) {
            // Called once the keys have been taken: the timeout runs from now.
            uci.setup_keys_done = true;
            uci.setup_since = now;
        } else if (analysis_row.find("ILLEGAL POSITION") != std::string::npos ||
                   now - uci.setup_since > SETUP_TIMEOUT_MS) {
            setup_failed();
        }
    }

    // Takes back the moves Psion has played beyond those of the target, all
    // at once (as after a ponder miss: the move pondered on and Psion's
    // reply).
    static void take_back() {
        std::vector<std::string> &moves = game_state.moves;
        size_t n = moves.size() - CommonMoves(moves, uci.target);
        takeback_to = (int)(moves.size() - n);
        takeback_since = GetTicks();
        for (size_t i = 0; i < n; i++) {
            press(KBD_t);
        }
    }

    // Set by UciStep when Psion is where the GUI wants it and has nothing
    // to do until the next command.
    static bool uci_done = false;

    // Drives Psion towards the state the GUI asked for. Called every tick.
    static void UciStep() {
        unsigned int now = GetTicks();
        std::vector<std::string> &moves = game_state.moves;
        // The pause after an action ends early once its effect is seen: a
        // move entered appears in memory, '2' on the screen.
        if (uci.inflight && moves.size() != uci.inflight_size) {
            uci.inflight = false;
            uci.retries = 0;
            uci.action_done = true;
        }
        if (takeback_to >= 0 && (int)moves.size() <= takeback_to) {
            takeback_to = -1;
            uci.action_done = true;
        }
        if (uci.two_player_pending && two_player_mode()) {
            uci.two_player_pending = false;
            uci.action_done = true;
        }
        bool idle = keyboard_buffer.Empty() &&
                    (uci.action_done || now - uci.last_action >= ACTION_GAP_MS);
        uci_done = false;
        if (uci.search == SEARCH_RUNNING) {
            if (!uci.reported_move.empty()) {
                // UciAnalysis has had its chance to send the line.
                if (now - analysis_row_since >= ANALYSIS_STABLE_MS ||
                    now - uci.reported_since >= 2 * ANALYSIS_STABLE_MS) {
                    report_move(uci.reported_move);
                }
                return;
            }
            if (!two_player_mode()) {
                uci.saw_program = true;
            }
            if (!uci.saw_program && game_over_shown() &&
                now - uci.thinking_start >= GAME_OVER_MS) {
                // The result was shown too late for UciStep to see it
                // before the search: Psion ignored the 'c'.
                uci_send("info string Game over (Psion shows %s)\n",
                         game_over_result().c_str());
                report_move("0000");
            } else if (!uci.movenow_sent && uci.think_ms &&
                       now - uci.search_start >=
                           MoveNowTime(uci.think_ms, setup_time())) {
                search_speed(false);
                press(KBD_m);
                uci.movenow_sent = true;
                uci.movenow_ticks = now;
            } else if (uci.movenow_sent &&
                       now - uci.movenow_ticks > MOVE_TIMEOUT_MS) {
                say("Psion did not move\n");
                press(KBD_2);
                finish_search("0000");
            }
            return;
        }
        if (uci.search == SEARCH_HOLDING || !have_known || !idle ||
            pending_promotion) {
            return;
        }
        if (uci.setup != SETUP_NONE) {
            // Before anything else: in set-up mode most keys place pieces.
            uci.sync_progress_ticks = now;
            step_setup();
            return;
        }
        if (!level_set_check() || !easy_check() || !analysis_check()) {
            return;
        }
        if (!uci_synced()) {
            // Give up if setting up the position makes no progress, rather
            // than leave the GUI waiting for readyok.
            if (moves.size() != uci.sync_progress_size) {
                uci.sync_progress_size = moves.size();
                uci.sync_progress_ticks = now;
            } else if (now - uci.sync_progress_ticks > SYNC_TIMEOUT_MS) {
                uci_send("info string Cannot set up the position\n");
                uci.unsupported = true;
                return;
            }
            if (uci.new_game) {
                uci.new_game = false;
                if (!moves.empty()) {
                    press(KBD_leftshift, KBD_4); // '$': new game
                    return;
                }
            }
            if (!two_player_mode() && takeback_to < 0 &&
                SameStart(game_state.start, uci.target_start) &&
                PlanSync(moves, uci.target) == SYNC_TAKEBACK) {
                // Psion takes back moves also when it considers the game
                // over and ignores '2', e.g. after a mate it played while
                // pondering.
                take_back();
                return;
            }
            if (!two_player_mode()) {
                // Once Psion considers the game over (e.g. it announced a
                // draw) it ignores '2'; only a new game helps then.
                if (++uci.two_player_presses > MAX_TWO_PLAYER_PRESSES) {
                    say("Psion does not switch to 2-player mode, starting a new game\n");
                    uci.two_player_presses = 0;
                    press(KBD_leftshift, KBD_4);
                } else {
                    press(KBD_2);
                }
                return;
            }
            uci.two_player_presses = 0;
            if (!SameStart(game_state.start, uci.target_start)) {
                start_target_game();
                return;
            }
            if (uci.inflight) {
                if (moves.size() != uci.inflight_size) {
                    uci.inflight = false;
                    uci.retries = 0;
                } else if (illegal_move_seen ||
                           now - uci.inflight_since > MOVE_TIMEOUT_MS) {
                    uci.inflight = false;
                    if (++uci.retries > MAX_RETRIES) {
                        uci_send("info string Psion does not accept %s\n",
                                 uci.target[moves.size()].c_str());
                        uci.unsupported = true;
                    }
                }
                return;
            }
            switch (PlanSync(moves, uci.target)) {
            case SYNC_SEND_MOVE:
                SendUciMove(uci.target[moves.size()]);
                uci.last_action = now;
                uci.action_done = false;
                uci.inflight = true;
                uci.inflight_size = moves.size();
                uci.inflight_since = now;
                break;
            case SYNC_TAKEBACK:
                // Not again while the last takeback is under way: at a low
                // speed it can take longer than ACTION_GAP_MS to show, and
                // more plies would go.
                if (takeback_to < 0 ||
                    now - takeback_since > MOVE_TIMEOUT_MS) {
                    take_back();
                }
                break;
            case SYNC_NEW_GAME:
                if (uci.target_start.standard) {
                    press(KBD_leftshift, KBD_4);
                } else {
                    start_target_game();
                }
                break;
            case SYNC_DONE:
                break;
            }
            return;
        }
        if (uci.readyok_pending) {
            uci.readyok_pending = false;
            uci_send("readyok\n");
        }
        if (uci.go_pending && uci.has_target && uci.unsupported) {
            // The position could not be set up after "go" was received.
            uci.go_pending = false;
            uci_send("bestmove 0000\n");
        }
        if (uci.go_pending && game_over_shown()) {
            // No move to play: answered at once (or once "stop" comes)
            // instead of after a search Psion would not do.
            uci.go_pending = false;
            uci.info_move.clear();
            uci.info_ponder.clear();
            uci.info_pv.clear();
            uci_send("info string Game over (Psion shows %s)\n",
                     game_over_result().c_str());
            report_move("0000");
        }
        if (uci.go_pending) {
            uci.go_pending = false;
            search_speed(true);
            press(KBD_c);
            uci.thinking_start = GetTicks();
            game_state.thinking = true;
            uci.search = SEARCH_RUNNING;
            uci.last_analysis = analysis_row; // from an earlier search
            uci.info_move.clear();
            uci.info_ponder.clear();
            uci.info_pv.clear();
            uci.info_depth = 0;
            uci.movenow_sent = false;
            uci.saw_program = false;
            memcpy(uci.search_board, known.board, sizeof(uci.search_board));
            uci.search_white = white_to_move();
        }
        uci_done = uci.search == SEARCH_NONE && !uci.readyok_pending &&
                   deferred_commands.empty();
    }

    // While Psion waits for the GUI (it is not searching), the emulation is
    // stopped until the next command, which saves a CPU core. Psion's own
    // clock stands still meanwhile, which does not matter: the adapter
    // keeps the time. Psion is only idle once ACTION_GAP_MS has passed
    // since the last key, enough to handle it; then memory must show the
    // known state for a few frames more, so that the screen is up to date.
    const unsigned int FREEZE_DELAY_MS = 50;

    static void freeze_when_idle() {
        static unsigned int quiet_since = 0;
        static bool quiet = false;
        // Not before ACTION_GAP_MS after a key, even if its effect has been
        // seen: Psion may still be updating the screen.
        if (!uci_done || !settled || pending_promotion ||
            !keyboard_buffer.Empty() ||
            GetTicks() - uci.last_action < ACTION_GAP_MS) {
            quiet = false;
            return;
        }
        if (!quiet) {
            quiet = true;
            quiet_since = GetTicks();
            return;
        }
        if (GetTicks() - quiet_since < FREEZE_DELAY_MS) {
            return;
        }
        wait_for_input();
        DOSBOX_ResetTicks();
        quiet = false;
    }

    void ProcessScreen(
        unsigned char pixels[SCREEN_BPP * SCREEN_WIDTH * SCREEN_HEIGHT]) {
        char(&board)[8][8] = ocr_board_buf;
        char txt_buf[TEXT_HEIGHT][TEXT_WIDTH];
        unsigned int i;
        ocr(txt_buf, (unsigned char *)pixels);
        for (i = 0; i < (unsigned int)TEXT_HEIGHT; i++) {
            std::string row(txt_buf[i], TEXT_WIDTH);
            if (row.find_first_not_of(' ') != std::string::npos) {
                if (i == 20 && row != screen_rows[i]) {
                    game_over_board_set = false;
                    game_over_board_wanted = game_over_text(row);
                    game_over_since = GetTicks();
                }
                screen_rows[i] = row;
            }
        }
        std::string row(txt_buf[24], TEXT_WIDTH);
        if (row != analysis_row) {
            analysis_row = row;
            analysis_row_since = GetTicks();
        }
        ocr_board(board, (unsigned char *)pixels);
        if (MemScan_Enabled()) {
            // memscan uses the screen as ground truth
            static bool start_snapshot_taken = false;
            if (!start_snapshot_taken && game_state.moves.empty() &&
                IsStartPosition(board)) {
                start_snapshot_taken = true;
                MemScan_Snapshot(board);
            }
        }
        // The bottom row also shows prompts and errors.
        promotion_prompt = row.find("PROMOTE TO") != std::string::npos;
        if (!illegal_move_seen && row.find("ILLEGAL") != std::string::npos) {
            say("Illegal move!\n");
            illegal_move_seen = true;
            ResetCursor();
        }
    }

    // Polls Psion's memory for new moves. The screen is not redrawn when
    // nothing changes, so this runs from the event loop instead.
    static void CheckGameState() {
        static unsigned int last_poll = 0;
        std::string move;
        unsigned int i;
        if (GetTicks() - last_poll < 10) {
            return;
        }
        last_poll = GetTicks();
        read_game_over_board();
        if (!PollGameState(move)) {
            return;
        }
        illegal_move_seen = false;
        game_state.moves.push_back(move);
        for (i = 0; i < game_state.moves.size(); i++) {
            say("%s ", game_state.moves[i].c_str());
        }
        say("\n");
        say_board(known.board);
        say("Ply %d, %s to move%s\n", known.ply,
            known.white_to_move ? "White" : "Black",
            known.pondering ? ", pondering" : "");
        if (MemScan_Enabled()) {
            MemScan_Snapshot(ocr_board_buf);
        }
        if (uci_mode) {
            UciEngineMoved(move);
            return;
        }
        if (two_player()) {
            return;
        }
        // The user plays White.
        game_state.thinking = !known.white_to_move;
        if (game_state.thinking) {
            say("Thinking!\n");
            keyboard_buffer.SendKey(KBD_c);
        } else {
            say("Stopped thinking!\n");
            keyboard_buffer.SendKey(KBD_2);
        }
    }

    // In the interactive mode, lines read while Psion thinks: they are
    // carried out on the user's turn (except "quit").
    static std::vector<std::string> held_lines;

    static void interactive_line(const std::string &line) {
        std::string move(line);
        move.erase(move.find_last_not_of(" \r\n") + 1);
        if (line.compare(0, 3, "uci") == 0) {
            UciCommand(line.c_str());
        } else if (IsMoveString(move)) {
            SendUciMove(move);
        }
    }

    void ProcessConsole() {
        std::string line;
        bool eof = false;
        if (uci_mode && !deferred_commands.empty() &&
            !must_wait(Tokenize(deferred_commands.front().c_str()))) {
            line = deferred_commands.front();
            deferred_commands.erase(deferred_commands.begin());
            UciCommand(line.c_str(), true);
        } else if (!uci_mode && !game_state.thinking && !held_lines.empty()) {
            line = held_lines.front();
            held_lines.erase(held_lines.begin());
            interactive_line(line);
        } else if (read_input_line(line, eof)) {
            std::vector<std::string> tokens = Tokenize(line.c_str());
            if (uci_mode) {
                UciCommand(line.c_str());
            } else if (tokens.size() == 1 && tokens[0] == "quit") {
                exit(0);
            } else if (game_state.thinking) {
                held_lines.push_back(line);
            } else {
                interactive_line(line);
            }
        } else if (eof && uci_mode) {
            exit(0);
        }
        CheckGameState();
        SendPromotion();
        if (uci_mode) {
            UciAnalysis();
            UciStep();
        }
        keyboard_buffer.ProcessKeys();
        if (uci_mode) {
            freeze_when_idle();
        }
    }

} // namespace psion
