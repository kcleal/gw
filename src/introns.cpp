#include "introns.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

#include "htslib/sam.h"

#include "ankerl_unordered_dense.h"

namespace Intron {

    // A single raw N-gap observed in one read.
    struct RawIntron {
        int32_t donor;     // first intronic ref base (0-based)
        int32_t acceptor;  // first exonic ref base after the intron
        int8_t  strand;    // 1 fwd, 2 rev, 0 unknown
    };

    // Strand-aware key for canonical-intron aggregation after clustering.
    struct ClusterKey {
        int32_t donorCid;
        int32_t acceptorCid;
        int8_t  strand;
        bool operator==(const ClusterKey& o) const noexcept {
            return donorCid == o.donorCid
                && acceptorCid == o.acceptorCid
                && strand == o.strand;
        }
    };

    struct ClusterKeyHash {
        size_t operator()(const ClusterKey& k) const noexcept {
            uint64_t x = (uint64_t)(uint32_t)k.donorCid * 0x9E3779B185EBCA87ULL
                       ^ (uint64_t)(uint32_t)k.acceptorCid * 0xC2B2AE3D27D4EB4FULL
                       ^ (uint64_t)(uint8_t)k.strand;
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdULL;
            x ^= x >> 33;
            return (size_t)x;
        }
    };

    // A cluster of 1D positions: assigns a cluster id to each input index.
    // Standard 1D DBSCAN-style linear scan: positions sorted ascending; consecutive
    // positions within `eps` bp belong to the same cluster. Deterministic, O(n log n)
    // due to the initial sort.
    //
    // Fills `cidByInputIdx` with one cluster id per input index, and `centroids` with
    // the mean position of each cluster (used as the canonical coordinate).
    static void cluster1D(const std::vector<int32_t>& positions,
                          int eps,
                          std::vector<int32_t>& cidByInputIdx,
                          std::vector<int32_t>& centroids)
    {
        const int n = (int)positions.size();
        cidByInputIdx.assign(n, -1);
        centroids.clear();
        if (n == 0) return;

        // Sort input indices by position.
        std::vector<int> order(n);
        for (int i = 0; i < n; ++i) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](int a, int b){ return positions[a] < positions[b]; });

        const int effEps = std::max(1, eps);
        int32_t curCid = -1;
        int64_t curSum = 0;
        int     curCnt = 0;
        int32_t prev   = 0;
        for (int k = 0; k < n; ++k) {
            int idx = order[k];
            int32_t p = positions[idx];
            if (curCid < 0 || (p - prev) > effEps) {
                // flush previous cluster centroid
                if (curCid >= 0) {
                    centroids.push_back((int32_t)(curSum / curCnt));
                }
                ++curCid;
                curSum = 0;
                curCnt = 0;
            }
            cidByInputIdx[idx] = curCid;
            curSum += p;
            curCnt += 1;
            prev = p;
        }
        if (curCid >= 0) {
            centroids.push_back((int32_t)(curSum / curCnt));
        }
    }

    // Map a STAR jM splice-motif code to a splice strand.
    //   -1        : no motif reported             -> 0 (unknown)
    //    0        : non-canonical junction        -> 0 (unknown)
    //    1,3,5    : GT/AG, GC/AG, AT/AC (donor +) -> 1 (forward)
    //    2,4,6    : CT/AC, CT/GC, GT/AT (donor -) -> 2 (reverse)
    // STAR adds +20 to the code when the junction is annotated in the splice
    // database; strip that offset with % 20 before interpreting parity.
    static inline int8_t strandFromJMotif(int motif) {
        if (motif < 0) return 0;
        int base = motif % 20;
        if (base == 0) return 0;
        return (base & 1) ? 1 : 2;
    }

    // Fetch the k-th value of STAR's per-junction jM motif array, or INT_MIN if the
    // tag is absent / not an array / index out of range. jM entries are in CIGAR
    // order, matching the order N-ops are encountered along the read.
    static int jMotifAt(const bam1_t* b, int k) {
        uint8_t* aux = bam_aux_get(b, "jM");
        if (!aux || *aux != 'B') return INT_MIN;
        uint32_t n = bam_auxB_len(aux);
        if (k < 0 || (uint32_t)k >= n) return INT_MIN;
        return (int)bam_auxB2i(aux, (uint32_t)k);
    }

    // Infer splice strand for one junction (the k-th N-op in the read). Priority:
    //   1. XS tag (per-read; Tophat/STAR --outSAMstrandField intronMotif)
    //   2. STAR jM splice-motif tag (per-junction) when it resolves a strand
    //   3. alignment flag fallback so we still get buckets on unstranded data
    static int8_t spliceStrandForJunction(const bam1_t* b, int junctionIdx) {
        if (uint8_t* xs = bam_aux_get(b, "XS")) {
            char c = bam_aux2A(xs);
            if (c == '+') return 1;
            if (c == '-') return 2;
        }
        int motif = jMotifAt(b, junctionIdx);
        if (motif != INT_MIN) {
            int8_t s = strandFromJMotif(motif);
            if (s != 0) return s;
        }
        return bam_is_rev(b) ? 2 : 1;
    }

    void extractCanonicalIntrons(const Segs::ReadCollection& rc,
                                 int eps,
                                 int minReads,
                                 std::vector<Utils::TrackBlock>& out)
    {
        out.clear();
        if (rc.region == nullptr || rc.readQueue.empty()) {
            return;
        }
        const int rgnStart = rc.region->start;
        const int rgnEnd   = rc.region->end;
        const std::string& chrom = rc.region->chrom;

        // Stage 1: collect every raw intron that overlaps the visible region.
        std::vector<RawIntron> raw;
        raw.reserve(256);
        for (const auto& a : rc.readQueue) {
            const bam1_t* b = a.delegate;
            if (!b) continue;
            const uint32_t* cig = bam_get_cigar(b);
            const int nCig = b->core.n_cigar;
            int refPos = b->core.pos;
            int junctionIdx = 0;  // N-op counter, kept aligned with the jM array

            for (int i = 0; i < nCig; ++i) {
                const int op  = bam_cigar_op(cig[i]);
                const int len = (int)bam_cigar_oplen(cig[i]);
                if (op == BAM_CREF_SKIP) {
                    int32_t donor    = (int32_t)refPos;
                    int32_t acceptor = (int32_t)(refPos + len);
                    if (donor < rgnEnd && acceptor > rgnStart) {
                        int8_t strand = spliceStrandForJunction(b, junctionIdx);
                        raw.push_back({donor, acceptor, strand});
                    }
                    // Advance for every N-op, even off-screen ones, so junctionIdx
                    // stays aligned with STAR's per-junction jM array ordering.
                    ++junctionIdx;
                }
                if (bam_cigar_type(op) & 2) {
                    refPos += len;
                }
            }
        }
        if (raw.empty()) return;

        // Stage 2: 1D DBSCAN per strand on donor and acceptor positions independently.
        // We partition raw introns into two strand groups so clusters never span strands.
        std::vector<int> idxFwd, idxRev;
        idxFwd.reserve(raw.size());
        idxRev.reserve(raw.size());
        for (int i = 0; i < (int)raw.size(); ++i) {
            (raw[i].strand == 2 ? idxRev : idxFwd).push_back(i);
        }

        // Global cluster id per raw intron for donors and acceptors. We keep them in
        // disjoint id spaces per strand by offsetting the reverse-strand ids.
        std::vector<int32_t> donorCid(raw.size(), -1), acceptorCid(raw.size(), -1);
        std::vector<int32_t> donorCentroids, acceptorCentroids;

        auto clusterBucket = [&](const std::vector<int>& idxs) {
            if (idxs.empty()) return;
            std::vector<int32_t> donors(idxs.size()), acceptors(idxs.size());
            for (size_t k = 0; k < idxs.size(); ++k) {
                donors[k]    = raw[idxs[k]].donor;
                acceptors[k] = raw[idxs[k]].acceptor;
            }
            std::vector<int32_t> dCid, aCid, dCent, aCent;
            cluster1D(donors,    eps, dCid, dCent);
            cluster1D(acceptors, eps, aCid, aCent);
            // Each bucket (fwd / rev) gets its own disjoint id range per axis.
            const int dOff = (int)donorCentroids.size();
            const int aOff = (int)acceptorCentroids.size();
            for (size_t k = 0; k < idxs.size(); ++k) {
                donorCid[idxs[k]]    = dCid[k] + dOff;
                acceptorCid[idxs[k]] = aCid[k] + aOff;
            }
            donorCentroids.insert(donorCentroids.end(),       dCent.begin(), dCent.end());
            acceptorCentroids.insert(acceptorCentroids.end(), aCent.begin(), aCent.end());
        };

        clusterBucket(idxFwd);
        clusterBucket(idxRev);

        // Stage 3: aggregate raw introns by (donorCid, acceptorCid, strand) → count.
        ankerl::unordered_dense::map<ClusterKey, int, ClusterKeyHash> counts;
        counts.reserve(raw.size());
        for (size_t ri = 0; ri < raw.size(); ++ri) {
            ClusterKey k{donorCid[ri], acceptorCid[ri], raw[ri].strand};
            ++counts[k];
        }

        // Stage 4: emit one TrackBlock per canonical intron passing minReads.
        out.reserve(counts.size());
        for (const auto& kv : counts) {
            if (kv.second < minReads) continue;
            int32_t cDonor    = donorCentroids[kv.first.donorCid];
            int32_t cAcceptor = acceptorCentroids[kv.first.acceptorCid];
            if (cAcceptor <= cDonor) continue;  // degenerate, skip

            Utils::TrackBlock tb;
            tb.chrom     = chrom;
            tb.start     = cDonor;
            tb.end       = cAcceptor;
            tb.strand    = kv.first.strand;
            tb.name      = std::to_string(kv.second);
            tb.value     = (float)kv.second;
            tb.anyToDraw = true;
            out.emplace_back(std::move(tb));
        }
        // Deterministic order so findTrackY produces a resonably stable layout each frame.
        std::sort(out.begin(), out.end(),
                  [](const Utils::TrackBlock& a, const Utils::TrackBlock& b){
                      if (a.start != b.start) return a.start < b.start;
                      if (a.end   != b.end)   return a.end   < b.end;
                      return a.value > b.value;
                  });
    }

}  // namespace Intron
