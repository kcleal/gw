# Online files

Native GW and the full client-side WASM build share **Open → Local file / Online
file**. Local file retains each platform's existing picker. Online file uses
four tabs, a searchable catalogue and recent files (saved alongside the other
settings in `.gw.ini`). Index paths are discovered automatically; overrides are
hidden under **Configure paths**. Variant browser and Annotation track are
independent checkboxes, so a VCF can be opened in both modes. The Open action
stays at the bottom of the dialog. The remote-rendered
WASM UI leaves online opening to its hosting application.

The bundled catalogue is compiled into GW, so native installations do not need
an extra resource directory. Edit `catalogue.json`, then run
`python3 resources/online/update_catalogue.py`; commit both JSON and `.inc`.

## UCSC discovery and file sizes

In **Tracks**, enter an assembly ID (for example `hg38`) and a search term, then
press **Search UCSC** or Enter. A known online reference pre-fills the assembly.
GW uses UCSC's targeted `/search` endpoint restricted to `trackDb`; it does not
download the enormous whole-assembly `/list/tracks` response. Up to 300 matching
tracks are listed. Selecting a result resolves its `/list/schema` metadata to
an indexed bigBed, bigGenePred, bigWig or supported indexed VCF file. Database
tracks without a directly readable indexed file offer Download, converting
compatible tables into local BED annotations. Other table schemas are available as
compressed TSV dumps. Track groups remain visible but have no data file of their
own. A live regional REST/Ensembl feature adapter is a future extension.

Queries are asynchronous, limited to one new UCSC request per second, and
cached for eight searches and 128 selections. Searching does not affect the
rendering path. UCSC metadata can contain duplicate settings; the provider
adapter accepts those while GW's catalogue parser still rejects duplicate keys.

Hover information marks regional files as indexed/streamed. Plain
BED/VCF/GFF/GTF files show their whole-file download size when known. Selecting
an unknown-sized non-indexed file triggers a cached background HEAD request;
no body is downloaded to measure its size. Unknown or CORS-hidden lengths stay
unknown. Pasted file URLs receive the same check when editing finishes. The
optional catalogue `sizeBytes` field can provide a known size directly.

## Lab catalogues

The internal catalogue format follows [schema.json](schema.json). Provider
adapters and future deployment configuration can supply entries in this format;
custom catalogue management is intentionally absent from the everyday picker:

```json
{
  "version": 1,
  "entries": [{
    "id": "lab:hg002-bam",
    "name": "HG002 PacBio",
    "category": "alignment",
    "assembly": "GRCh38",
    "sample": "HG002",
    "technology": "PacBio CCS",
    "format": "bam",
    "url": "https://example.org/HG002.bam",
    "indexURL": "https://example.org/HG002.bam.bai",
    "source": "Example lab",
    "release": "2026-01",
    "attribution": "Dataset citation / terms"
  }]
}
```

Categories are `reference`, `alignment`, `track`, `variant`. IDs must be unique.
Other optional fields: `compressedIndexURL` (BGZF FASTA GZI), `regionsURL`
(confident-region BED), `locus` (initial GW region), and `bundle` (an array of
resource IDs from the catalogue). Bundle members open reference first. The
bundled HG002 alignment offers a complete GRCh38 reference/alignment/benchmark
VCF/confident-region bundle. All headers and indexes are checked before opening
any member. Assembly checks accept `chr1`/`1` and `chrM`/`MT` aliases and compare
available contig lengths; they are not a liftover or sequence-identity test.
Changing the online reference is always allowed, including when existing
alignments have different contig lengths. The view is clamped to a valid
reference chromosome (using an alias or first chromosome if needed). Local
BAM/CRAM files without the resulting exact chromosome names are closed; their
associated intron tracks are removed as well.

IGV genome JSON arrays are also accepted, including `fastaURL`, `indexURL`, and
`compressedIndexURL`. The current IGV/Broad list loads automatically in the
background and appears directly in Reference genome → Catalogue.
Known retired hg19/hg38 GenePattern URLs are mapped to their public IGV mirrors.

Without an explicit index, GW tries conventional `.fai`, `.bai` (including
`reads.bai`), `.csi`, `.crai`, and `.tbi` names. It never builds online indexes.
An explicit index is preserved separately from the data URL; existing GW API
signatures remain unchanged.

## Browser transport

`deps/wasm/online-files.mjs` mounts remote files in the same read-only filesystem
adapter as browser-local Files. Data enters WASM only as requested. Each source
uses 256 KiB pages with an 8 MiB LRU cache. Range requests must return HTTP 206
with a valid, exposed `Content-Range`; a 200 response is cancelled, not downloaded.
CORS must permit GET plus Range and preferably expose Content-Range (and ETag).
For hosts such as UCSC that hide Content-Range, a successful HEAD with the total
Content-Length plus an exact partial Content-Length is also accepted.
A missing index, blocked CORS, truncated range, changed file or timeout produces
an error. Unindexed text annotations are limited to 16 MiB in the browser;
FASTA/BAM/BGZF/bigBed/bigWig remain regional. Indexes up to 64 MiB are prepared once, using up to four parallel 1 MiB range
requests, before GW starts reading. Preparing them outside Asyncify keeps the
UI responsive and avoids repeatedly fetching an index during header validation
and reopening. Small unindexed text annotations (up to 16 MiB) are likewise prepared before
GW scans them. Large indexed genomic data remains lazy.

Native discovery and preflight run in a worker; native HTSlib/libBigWig readers
handle data access. Browser fetching is asynchronous; the existing main-loop
command queue runs HTSlib validation and loading, so DOM callbacks cannot reenter
an active Asyncify read. Cancel/ESC cancels file preparation and browser range requests
before committing a load; the final installation of validated resources is not
interruptible. Native preflight cancellation discards its result when the
current reader finishes.

Catalogue metadata is descriptive, not a licence grant. Provider terms and
availability still apply. UCSC hub discovery and repository-wide searches are
future extensions; this version uses curated, directly addressable files.

## Checks

- `make test-ui`: catalogue round trips, explicit indexes and contig checks,
  alongside existing rendering/interaction regressions.
- `node test/test_wasm_online_files.mjs`: bounded range/cache/error/cancel and
  HEAD-only size checks.
- Add the compiled `test_wasm_local_io.c` HTSlib fixture path as an argument to
  that Node test to exercise real WASM HTSlib reads through the HTTP adapter.

Catalogue-only WASM exception regression:

```sh
em++ -std=c++17 -O2 -fexceptions -Isrc test/test_wasm_online_catalogue.cpp \
  src/online/online_catalogue.cpp -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node \
  -sEXPORTED_FUNCTIONS=_catalogue_count,_bundle_count \
  -sEXPORTED_RUNTIME_METHODS=ccall -o /tmp/gw-online-catalogue.mjs
node test/test_wasm_online_catalogue.mjs /tmp/gw-online-catalogue.mjs
```

Provider tests (no renderer or network required):

```sh
c++ -std=c++17 -Isrc test/test_online_providers.cpp src/online/online_catalogue.cpp \
  -o /tmp/gw-test-online-providers
/tmp/gw-test-online-providers
```

Optional arguments are saved UCSC hg38 search and `ncbiRefSeqOther` schema JSON
responses, to validate against real provider metadata.

UCSC database tables remain visible and downloadable. Tables with chromosome,
start/end coordinates are converted to BED; genePred exon blocks become BED12.
Other schemas download as raw compressed TSV without automatic opening. Composite
track groups have no individual data file. Full downloads use UCSC's database dump
server, avoiding REST record limits. Files are saved at the top of `gw_downloads`;
name collisions receive numeric suffixes.
