#ifndef NODES_H
#define NODES_H

namespace psion {

    // Counts the nodes of Psion's search by watching the board it searches
    // on (see docs/search-internals.md). Install once Psion's data segment
    // is known; the count only grows, so a search takes the difference.
    void Nodes_Install(unsigned int data_segment);
    unsigned long long Nodes_Count();

} // namespace psion

#endif
