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
 * 7152..7161), the ply counter of the search (DS:7076), and the copies of
 * its board to the game board (DS:28AC).
 */

namespace psion {

    static const unsigned int BOARD_OFFSET = 0x5B00;
    static const unsigned int BOARD_SIZE = 0x100;

    static const unsigned int PV_OFFSET = 0x7150;
    static const unsigned int PV_LAST = PV_OFFSET + 2 + 7 * 2; // last move
    static const unsigned int ITERATION_OFFSET = 0x5A67;
    static const unsigned int PLY_OFFSET = 0x7076; // ply * 2
    static const unsigned int GAME_OFFSET = 0x28AC;
    static const unsigned int GAME_SIZE = 64;

    static unsigned int board_lo = 0, board_hi = 0; // linear, [lo, hi)
    static unsigned int pv_base = 0, pv_last = 0;    // linear
    static unsigned int iteration_addr = 0, ply_addr = 0; // linear
    static unsigned int max_ply = 0;
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
        if (pv_size < PV_QUEUE) {
            pv_size++;
        } else {
            pv_head = (pv_head + 1) % PV_QUEUE; // the oldest is lost
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
        if (addr <= ply_addr && addr + size > ply_addr) {
            unsigned int ply = host_readb(MemBase + ply_addr) / 2;
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
        MEM_SetPageHandler(ply_addr / MEM_PAGESIZE, 1, &board_handler);
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
    }

    bool Nodes_BoardCopying() {
        return game_copying;
    }

    unsigned long long Nodes_Count() {
        return zero_writes / 2;
    }

} // namespace psion
