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
 * 7152..7161).
 */

namespace psion {

    static const unsigned int BOARD_OFFSET = 0x5B00;
    static const unsigned int BOARD_SIZE = 0x100;

    static const unsigned int PV_OFFSET = 0x7150;
    static const unsigned int PV_LAST = PV_OFFSET + 2 + 7 * 2; // last move

    static unsigned int board_lo = 0, board_hi = 0; // linear, [lo, hi)
    static unsigned int pv_base = 0, pv_last = 0;    // linear
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
        MEM_SetPageHandler(pv_base / MEM_PAGESIZE,
                           (pv_last + 1) / MEM_PAGESIZE - pv_base / MEM_PAGESIZE + 1,
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
    }

    unsigned long long Nodes_Count() {
        return zero_writes / 2;
    }

} // namespace psion
