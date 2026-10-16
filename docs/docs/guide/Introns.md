---
title: Introns
layout: home
parent: User guide
nav_order: 8
---

# Intron (splice-junction) track

GW can render a lightweight splice-junction track directly from the alignments
already in view. For each BAM it extracts every `N` operation in the CIGAR of
every visible read, clusters the donor and acceptor positions on each strand,
and draws one bar per canonical intron — coloured and scaled by how many reads
support it.

This makes it a quick way to get a splice-graph style overview of an RNA-seq
BAM without pre-computing a BED, and it updates as you pan and zoom.

## Showing the track

From the Tools menu, tick **Toggle intron track** under **Introns**.

From the command prompt:

```text
:introns          # toggle for bam 0
:introns 1        # toggle for bam index 1
```

From the command line on startup:

```shell
gw hg38 -b rna.bam --introns
```

With multiple BAMs, `--introns` toggles the track on for every BAM that is
loaded.

## How junctions are built

1. **Harvest** — for every read in view the CIGAR is walked for
   `BAM_CREF_SKIP` (`N`) ops. Each gap is recorded as a raw
   `(donor, acceptor, strand)` triple.
2. **Strand** — the `XS` tag is preferred when present (Tophat/STAR output).
   When absent, the alignment's reverse-strand flag is used so unstranded
   libraries still split into two separable buckets rather than mixing.
3. **Cluster** — donor and acceptor positions are clustered independently on
   each strand with a 1D DBSCAN-style linear scan. Positions within
   `splice_cluster_eps` bp of each other collapse into one canonical
   coordinate (the mean of the cluster).
4. **Aggregate** — every raw gap contributes one read of support to the
   `(donor-cluster, acceptor-cluster, strand)` it falls into.
5. **Filter** — junctions supported by fewer than `min_junction_reads`
   reads are dropped.

## Visual encoding

Each surviving junction becomes one bar:

- **Thickness** grows with support, from thin at 1 read to thick at 30+.
- **Colour** uses the matplotlib *Purples* ramp, clamped to the range
  `1..30` — anything above 30 reads uses the same darkest colour. On the
  `igv` theme light = low support; on `dark` and `slate` themes the ramp is
  reversed so light = high support on a dark background.
- **Strand** is shown by the bar's arrow: right-pointing for `+`,
  left-pointing for `-`.
- **Label** — when the bar is wide enough, the supporting read count is
  drawn at the midpoint.

## Settings

Both options are exposed in the Tools dialog under **Introns** and can be set
on startup from the command line.

| Option                    | Default | Meaning                                                                                   |
|---                        |---      |---                                                                                        |
| `splice_cluster_eps`      | 2       | Maximum bp distance between donor (or acceptor) positions merged into one canonical site |
| `min_junction_reads`      | 2       | Minimum reads supporting a junction before it is drawn                                    |

Command-line equivalents:

```shell
gw hg38 -b rna.bam --introns --splice-cluster-eps 5 --min-junction-reads 3
```

## Zoomed-out behaviour

Intron extraction runs over the alignments that GW has loaded into memory.
When the view is wider than the low-memory threshold, GW switches to a
coverage-only path and does not load individual reads — in that mode the
intron track is cleared rather than estimated, so what you see is only ever
built from reads actually in view. Zoom back in to repopulate.

## Notes

- The junction `start`/`end` coordinates use the cluster centroid, so two
  reads whose `N` ops begin at slightly different positions (e.g. because of
  aligner soft-clipping at the exon boundary) will still collapse into one
  intron when `splice_cluster_eps` is large enough.
- Clustering is done per-strand, so `+` and `-` junctions never merge even
  if their coordinates happen to coincide.
- The track is a synthetic track type — it has no backing file, and turning
  it off discards the extracted data.
