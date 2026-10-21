#ifndef MEMSCAN_H
#define MEMSCAN_H

namespace psion {

    // Locate Psion's internal board array by correlating snapshots of
    // conventional memory with the OCR'd board. Enabled by setting the
    // environment variable PSION_MEMSCAN. If PSION_MEMDUMP is set to a
    // directory, every snapshot is also written there for offline diffing.
    bool MemScan_Enabled();
    void MemScan_Snapshot(char board[8][8]);

} // namespace psion

#endif
