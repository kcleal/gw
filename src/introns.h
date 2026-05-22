#pragma once

#include <vector>

#include "segments.h"
#include "utils.h"

namespace Intron {

    // Extract canonical introns from a ReadCollection by walking the CIGAR
    // of every read for BAM_CREF_SKIP (N) operations. Each cluster of
    // (donor, acceptor) positions becomes one TrackBlock whose `value` is
    // the supporting read count. Clustering is 1D DBSCAN per-strand on
    // donor and acceptor axes independently (eps in bp).
    //
    // Only introns whose support >= minReads are emitted. If the collection's
    // readQueue is empty (zoomed-out low-memory mode) the output is cleared
    // and the function returns — no stale introns linger.
    void extractCanonicalIntrons(const Segs::ReadCollection& rc,
                                 int eps,
                                 int minReads,
                                 std::vector<Utils::TrackBlock>& out);

}  // namespace Intron
