#include <stdlib.h>

#include "dosbox.h"
#include "mem.h"
#include "paging.h"
#include "nodes.h"

/*
 * Node counter.
 *
 * Psion searches on its own board, a 0x88 array of words at DS:5B00. Every
 * move made in the search lifts the piece from its square by writing 0000
 * there, and taking the move back clears the square it went to, so two
 * writes of 0000 to that board are one node. (A few other writes of 0000,
 * such as clearing the board at the start of a search, make the count
 * about 0.2% high.) The pages that hold the board get a handler that does
 * the writes itself and counts them; reads still go straight to memory.
 *
 * The same handler watches the copy of the principal variation that
 * Psion makes for its analysis display (score at DS:7150, moves at
 * 7152..7161), the ply counter of the search (DS:7076), the root alpha
 * (DS:713A, set to 0 by a fail low), and the copies of its board to the
 * game board (DS:28AC). The root node is ply 1: when the ply counter goes
 * to ply 2, a root move has been made, and DS:5A6E holds it.
 */

namespace psion {

    static const unsigned int BOARD_OFFSET = 0x5B00;
    static const unsigned int BOARD_SIZE = 0x100;

    static const unsigned int PV_OFFSET = 0x7150;
    static const unsigned int PV_LAST = PV_OFFSET + 2 + 7 * 2; // last move
    static const unsigned int ITERATION_OFFSET = 0x5A67;
    static const unsigned int PLY_OFFSET = 0x7076; // ply * 2
    static const unsigned int ALPHA_OFFSET = 0x713A; // root alpha
    static const unsigned int MOVE_OFFSET = 0x5A6E;  // move made last
    static const unsigned int GAME_OFFSET = 0x28AC;
    static const unsigned int GAME_SIZE = 64;

    static unsigned int board_lo = 0, board_hi = 0; // linear, [lo, hi)
    static unsigned int pv_base = 0, pv_last = 0;    // linear
    static unsigned int iteration_addr = 0, ply_addr = 0; // linear
    static unsigned int alpha_addr = 0, move_addr = 0;    // linear
    static unsigned int max_ply = 0, ply = 0;
    static unsigned char iteration = 0;
    // For time management: the score of the last line (units, from the
    // bias), the iteration it came in, the drop of the current
    // iteration's first line from the line before it, and whether a fail
    // low has not been followed by a line yet.
    static bool have_line = false, drop_known = false;
    static int line_value = 0, drop = 0;
    static unsigned char line_iteration = 0;
    static bool fail_low_open = false;
    // The root moves made in the current iteration, each once, and
    // whether its first line has come. A fail low starts them again: all
    // root moves are then searched again, the old best first.
    static const int MAX_ROOT_MOVES = 256;
    static unsigned short root_moves[MAX_ROOT_MOVES];
    static int root_move_count = 0;
    static bool line_seen = false;

    static bool is_mate(int value) {
        return abs(value) / 24 >= 256; // as in MemoryAnalysis()
    }
    static unsigned int game_lo = 0, game_hi = 0;    // linear, [lo, hi)
    static bool game_copying = false;
    static unsigned long long zero_writes = 0;

    static const int PV_QUEUE = 16;
    static MemoryPv pv_queue[PV_QUEUE];
    static int pv_head = 0, pv_size = 0;

    static void queue_pv() {
        MemoryPv &pv = pv_queue[(pv_head + pv_size) % PV_QUEUE];
        pv.score = host_readw(MemBase + pv_base);
        for (int i = 0; i < 8; i++) {
            pv.moves[i] = host_readw(MemBase + pv_base + 2 + 2 * i);
        }
        pv.iteration = host_readb(MemBase + iteration_addr);
        pv.max_ply = (unsigned char)max_ply;
        int value = (int)pv.score - 0x8000;
        if (pv.iteration != line_iteration) {
            // The first line of an iteration: the previous iteration's
            // best move, searched again.
            drop_known = have_line && !is_mate(line_value) && !is_mate(value);
            drop = line_value - value;
            line_iteration = pv.iteration;
        }
        have_line = true;
        line_value = value;
        fail_low_open = false;
        line_seen = true;
        if (pv_size < PV_QUEUE) {
            pv_size++;
        } else {
            pv_head = (pv_head + 1) % PV_QUEUE; // the oldest is lost
        }
    }

    static void note_root_move(unsigned short move) {
        for (int i = 0; i < root_move_count; i++) {
            if (root_moves[i] == move) {
                return;
            }
        }
        if (root_move_count < MAX_ROOT_MOVES) {
            root_moves[root_move_count++] = move;
        }
    }

    // Called after a write of size bytes at addr.
    static inline void count(PhysPt addr, Bitu val, unsigned int size) {
        if (val == 0 && addr >= board_lo && addr < board_hi) {
            zero_writes++;
        }
        if (addr <= pv_last + 1 && addr + size > pv_last + 1) {
            queue_pv();
        }
        if (addr <= iteration_addr && addr + size > iteration_addr) {
            unsigned char it = host_readb(MemBase + iteration_addr);
            if (it != iteration) {
                drop_known = false;
                fail_low_open = false;
                root_move_count = 0;
                line_seen = false;
            }
            iteration = it;
        }
        if (addr <= alpha_addr + 1 && addr + size > alpha_addr &&
            host_readw(MemBase + alpha_addr) == 0) {
            if (!fail_low_open) {
                root_move_count = 0;
                line_seen = false;
            }
            fail_low_open = true;
        }
        if (addr <= ply_addr && addr + size > ply_addr) {
            unsigned int new_ply = host_readb(MemBase + ply_addr) / 2;
            if (new_ply == 2 && ply == 1) {
                note_root_move(host_readw(MemBase + move_addr));
            }
            ply = new_ply;
            if (ply > max_ply) {
                max_ply = ply;
            }
        }
        if (addr < game_hi && addr + size > game_lo) {
            game_copying = addr + size < game_hi;
        }
    }

    class BoardPageHandler : public PageHandler {
    public:
        BoardPageHandler() {
            flags = PFLAG_READABLE;
        }
        HostPt GetHostReadPt(Bitu phys_page) {
            return MemBase + phys_page * MEM_PAGESIZE;
        }
        void writeb(PhysPt addr, Bitu val) {
            host_writeb(MemBase + addr, (Bit8u)val);
            count(addr, val, 1);
        }
        void writew(PhysPt addr, Bitu val) {
            host_writew(MemBase + addr, (Bit16u)val);
            count(addr, val, 2);
        }
        void writed(PhysPt addr, Bitu val) {
            host_writed(MemBase + addr, (Bit32u)val);
            count(addr, val, 4);
        }
    };

    static BoardPageHandler board_handler;

    void Nodes_Install(unsigned int data_segment) {
        if (board_hi) {
            return;
        }
        board_lo = data_segment * 16 + BOARD_OFFSET;
        board_hi = board_lo + BOARD_SIZE;
        Bitu first = board_lo / MEM_PAGESIZE, last = (board_hi - 1) / MEM_PAGESIZE;
        MEM_SetPageHandler(first, last - first + 1, &board_handler);
        pv_base = data_segment * 16 + PV_OFFSET;
        pv_last = data_segment * 16 + PV_LAST;
        iteration_addr = data_segment * 16 + ITERATION_OFFSET;
        ply_addr = data_segment * 16 + PLY_OFFSET;
        alpha_addr = data_segment * 16 + ALPHA_OFFSET;
        move_addr = data_segment * 16 + MOVE_OFFSET;
        MEM_SetPageHandler(alpha_addr / MEM_PAGESIZE, 1, &board_handler);
        MEM_SetPageHandler(ply_addr / MEM_PAGESIZE, 1, &board_handler);
        MEM_SetPageHandler(iteration_addr / MEM_PAGESIZE, 1, &board_handler);
        MEM_SetPageHandler(pv_base / MEM_PAGESIZE,
                           (pv_last + 1) / MEM_PAGESIZE - pv_base / MEM_PAGESIZE + 1,
                           &board_handler);
        game_lo = data_segment * 16 + GAME_OFFSET;
        game_hi = game_lo + GAME_SIZE;
        MEM_SetPageHandler(game_lo / MEM_PAGESIZE,
                           (game_hi - 1) / MEM_PAGESIZE - game_lo / MEM_PAGESIZE + 1,
                           &board_handler);
        PAGING_ClearTLB();
    }

    bool Nodes_TakePv(MemoryPv &pv) {
        if (!pv_size) {
            return false;
        }
        pv = pv_queue[pv_head];
        pv_head = (pv_head + 1) % PV_QUEUE;
        pv_size--;
        return true;
    }

    void Nodes_ClearPv() {
        pv_size = 0;
        max_ply = 0;
        iteration = 0;
        have_line = false;
        drop_known = false;
        line_iteration = 0;
        fail_low_open = false;
        root_move_count = 0;
        line_seen = false;
    }

    bool Nodes_FirstLineDrop(int &d) {
        d = drop;
        return drop_known;
    }

    bool Nodes_FailLowOpen() {
        return fail_low_open;
    }

    int Nodes_RootMovesDone() {
        // all but the one being searched; the first is done once its line
        // has come, even if the next one has not been made yet
        int done = root_move_count > 0 ? root_move_count - 1 : 0;
        return line_seen && done < 1 ? 1 : done;
    }

    bool Nodes_HaveLine() {
        return line_seen;
    }

    unsigned char Nodes_Iteration() {
        return iteration;
    }

    bool Nodes_BoardCopying() {
        return game_copying;
    }

    unsigned long long Nodes_Count() {
        return zero_writes / 2;
    }

} // namespace psion
