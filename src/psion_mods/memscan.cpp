#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>

#include "dosbox.h"
#include "mem.h"
#include "regs.h"
#include "memscan.h"

/*
 * Board finder.
 *
 * Every time the adapter sees a stable position (fully recognized OCR board)
 * it calls MemScan_Snapshot(). The first snapshot scans all of conventional
 * memory for every plausible board layout:
 *
 *   address(file,rank) = base + file*file_step + rank*rank_step
 *
 * with byte or word elements, rank or file major, either orientation, and
 * row lengths 8 (plain), 10/12 (mailbox) and 16 (0x88). A layout is accepted
 * if all empty squares hold the same byte and every byte value always denotes
 * the same piece. We do not require a piece to have a single code, so boards
 * that store piece-list indices (or "has moved" bits) are found too.
 *
 * Later snapshots filter the candidates. A candidate may miss a few times
 * since the internal board can be momentarily out of step with the screen.
 */

namespace psion {

    static const unsigned int SCAN_SIZE = 0xA0000; // conventional memory
    static const unsigned int MAX_CANDIDATES = 20000;
    static const int MAX_MISSES = 3;
    static const int REPORT_NUM = 10;

    struct Candidate {
        unsigned int base; // linear address of a1
        int file_step;
        int rank_step;
        int hits;
        int misses;
        unsigned char empty;
        char v2c[256]; // byte value -> piece char, 0 if not seen yet
    };

    static std::vector<Candidate> candidates;
    static std::vector<unsigned char> prev_snapshot;
    static int snapshot_count = 0;
    static bool overflow = false;

    bool MemScan_Enabled() {
        static int enabled = -1;
        if (enabled < 0) {
            enabled = getenv("PSION_MEMSCAN") != NULL;
        }
        return enabled != 0;
    }

    static unsigned int scan_size() {
        unsigned int size = MEM_TotalPages() * 4096;
        return size < SCAN_SIZE ? size : SCAN_SIZE;
    }

    // Checks board against memory for the given layout. On success v2c and
    // empty are updated. v2c/empty are only touched on success.
    static bool match(const unsigned char *mem, char board[8][8],
                      unsigned int base, int file_step, int rank_step,
                      char v2c[256], unsigned char &empty, bool have_empty) {
        char v2c_new[256];
        unsigned char empty_new = empty;
        bool have_empty_new = have_empty;
        int f, r;
        memcpy(v2c_new, v2c, 256);
        for (r = 0; r < 8; r++) {
            for (f = 0; f < 8; f++) {
                unsigned char v = mem[base + f * file_step + r * rank_step];
                char c = board[f][r];
                if (c == '.') {
                    if (!have_empty_new) {
                        empty_new = v;
                        have_empty_new = true;
                    } else if (v != empty_new) {
                        return false;
                    }
                }
                if (v2c_new[v] == 0) {
                    v2c_new[v] = c;
                } else if (v2c_new[v] != c) {
                    return false;
                }
            }
        }
        memcpy(v2c, v2c_new, 256);
        empty = empty_new;
        return true;
    }

    static void initial_scan(const unsigned char *mem, unsigned int size,
                             char board[8][8]) {
        static const int row_lengths[] = {8, 10, 12, 16};
        int e, i, s1, s2, major;
        for (e = 1; e <= 2; e++) { // element size
            for (i = 0; i < 4; i++) {
                for (major = 0; major < 2; major++) {
                    for (s1 = -1; s1 <= 1; s1 += 2) {
                        for (s2 = -1; s2 <= 1; s2 += 2) {
                            int file_step, rank_step;
                            if (major == 0) { // rank major: files adjacent
                                file_step = s1 * e;
                                rank_step = s2 * e * row_lengths[i];
                            } else { // file major: ranks adjacent
                                rank_step = s2 * e;
                                file_step = s1 * e * row_lengths[i];
                            }
                            // range of offsets relative to a1
                            int lo = std::min(0, 7 * file_step) +
                                     std::min(0, 7 * rank_step);
                            int hi = std::max(0, 7 * file_step) +
                                     std::max(0, 7 * rank_step);
                            if ((int)size <= hi - lo) {
                                continue;
                            }
                            unsigned int base;
                            for (base = -lo; base < size - hi; base++) {
                                Candidate c;
                                memset(c.v2c, 0, 256);
                                c.empty = 0;
                                if (!match(mem, board, base, file_step,
                                           rank_step, c.v2c, c.empty, false)) {
                                    continue;
                                }
                                if (candidates.size() >= MAX_CANDIDATES) {
                                    overflow = true;
                                    return;
                                }
                                c.base = base;
                                c.file_step = file_step;
                                c.rank_step = rank_step;
                                c.hits = 1;
                                c.misses = 0;
                                candidates.push_back(c);
                            }
                        }
                    }
                }
            }
        }
    }

    static void filter(const unsigned char *mem, char board[8][8]) {
        std::vector<Candidate> kept;
        size_t i;
        for (i = 0; i < candidates.size(); i++) {
            Candidate &c = candidates[i];
            if (match(mem, board, c.base, c.file_step, c.rank_step, c.v2c,
                      c.empty, true)) {
                c.hits++;
            } else {
                c.misses++;
            }
            if (c.misses <= MAX_MISSES) {
                kept.push_back(c);
            }
        }
        candidates.swap(kept);
    }

    static bool better(const Candidate &a, const Candidate &b) {
        if (a.misses != b.misses) {
            return a.misses < b.misses;
        }
        return a.base < b.base;
    }

    static const char *layout_name(const Candidate &c) {
        int e = (abs(c.file_step) == 2 || abs(c.rank_step) == 2) ? 2 : 1;
        int row = std::max(abs(c.file_step), abs(c.rank_step)) / e;
        switch (row) {
        case 8:
            return "8x8";
        case 10:
            return "10-wide mailbox";
        case 12:
            return "12-wide mailbox";
        case 16:
            return "0x88";
        }
        return "?";
    }

    static void report(const unsigned char *mem) {
        unsigned int ds_seg = SegValue(ds);
        size_t n = std::min((size_t)REPORT_NUM, candidates.size());
        size_t i;
        std::sort(candidates.begin(), candidates.end(), better);
        fprintf(stderr, "memscan: snapshot %d: %u candidates%s\n",
                snapshot_count, (unsigned int)candidates.size(),
                overflow ? " (initial scan overflowed, list incomplete)" : "");
        for (i = 0; i < n; i++) {
            const Candidate &c = candidates[i];
            const char *pieces = "PNBRQKpnbrqk";
            int p, v, f, r;
            fprintf(stderr, "  a1=0x%05X", c.base);
            if (c.base >= ds_seg * 16 && c.base < ds_seg * 16 + 0x10000) {
                fprintf(stderr, " (DS:%04X, DS=%04X)", c.base - ds_seg * 16,
                        ds_seg);
            }
            fprintf(stderr,
                    " %s file_step=%+d rank_step=%+d %s hits=%d misses=%d\n",
                    layout_name(c), c.file_step, c.rank_step,
                    (abs(c.file_step) == 2 || abs(c.rank_step) == 2) ? "words"
                                                                     : "bytes",
                    c.hits, c.misses);
            fprintf(stderr, "    empty=%02X", c.empty);
            for (p = 0; pieces[p]; p++) {
                fprintf(stderr, " %c=", pieces[p]);
                bool first = true;
                for (v = 0; v < 256; v++) {
                    if (c.v2c[v] == pieces[p]) {
                        fprintf(stderr, first ? "%02X" : ",%02X", v);
                        first = false;
                    }
                }
                if (first) {
                    fprintf(stderr, "?");
                }
            }
            fprintf(stderr, "\n");
            if (i == 0) {
                for (r = 7; r >= 0; r--) {
                    fprintf(stderr, "    %d ", r + 1);
                    for (f = 0; f < 8; f++) {
                        fprintf(
                            stderr, " %02X",
                            mem[c.base + f * c.file_step + r * c.rank_step]);
                    }
                    fprintf(stderr, "\n");
                }
                fprintf(stderr, "        a  b  c  d  e  f  g  h\n");
            }
        }
    }

    static void dump(const unsigned char *mem, unsigned int size,
                     char board[8][8]) {
        const char *dir = getenv("PSION_MEMDUMP");
        char path[1024];
        FILE *fp;
        int f, r;
        if (!dir) {
            return;
        }
        snprintf(path, sizeof(path), "%s/mem_%03d.bin", dir, snapshot_count);
        fp = fopen(path, "wb");
        if (!fp) {
            fprintf(stderr, "memscan: cannot write %s\n", path);
            return;
        }
        fwrite(mem, 1, size, fp);
        fclose(fp);
        snprintf(path, sizeof(path), "%s/board_%03d.txt", dir, snapshot_count);
        fp = fopen(path, "w");
        if (!fp) {
            return;
        }
        for (r = 7; r >= 0; r--) {
            for (f = 0; f < 8; f++) {
                fputc(board[f][r], fp);
            }
            fputc('\n', fp);
        }
        fprintf(fp, "DS=%04X CS=%04X\n", (unsigned int)SegValue(ds),
                (unsigned int)SegValue(cs));
        fclose(fp);
    }

    void MemScan_Snapshot(char board[8][8]) {
        unsigned int size = scan_size();
        const unsigned char *mem = (const unsigned char *)MemBase;
        int f, r;
        if (!MemScan_Enabled()) {
            return;
        }
        for (f = 0; f < 8; f++) {
            for (r = 0; r < 8; r++) {
                if (board[f][r] == '*') { // OCR failed, cursor on square etc.
                    return;
                }
            }
        }
        snapshot_count++;
        dump(mem, size, board);
        if (!prev_snapshot.empty()) {
            unsigned int i, changed = 0;
            for (i = 0; i < size; i++) {
                changed += prev_snapshot[i] != mem[i];
            }
            fprintf(stderr,
                    "memscan: %u bytes changed since previous snapshot\n",
                    changed);
        }
        prev_snapshot.assign(mem, mem + size);
        if (snapshot_count == 1) {
            initial_scan(mem, size, board);
        } else {
            filter(mem, board);
        }
        report(mem);
    }

} // namespace psion
