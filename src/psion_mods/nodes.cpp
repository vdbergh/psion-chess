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
 */

namespace psion {

    static const unsigned int BOARD_OFFSET = 0x5B00;
    static const unsigned int BOARD_SIZE = 0x100;

    static unsigned int board_lo = 0, board_hi = 0; // linear, [lo, hi)
    static unsigned long long zero_writes = 0;

    static inline void count(PhysPt addr, Bitu val) {
        if (val == 0 && addr >= board_lo && addr < board_hi) {
            zero_writes++;
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
            count(addr, val);
        }
        void writew(PhysPt addr, Bitu val) {
            host_writew(MemBase + addr, (Bit16u)val);
            count(addr, val);
        }
        void writed(PhysPt addr, Bitu val) {
            host_writed(MemBase + addr, (Bit32u)val);
            count(addr, val);
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
        PAGING_ClearTLB();
    }

    unsigned long long Nodes_Count() {
        return zero_writes / 2;
    }

} // namespace psion
