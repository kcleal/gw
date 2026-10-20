# Online resources

Shared catalogue parsing, UCSC discovery and native online-file validation live here.
The ImGui picker is in `../menu_components/imgui/imgui_online_files.cpp`; browser
range I/O is in `../../deps/wasm/online-files.mjs`. Neither changes the public GW API.

The picker exposes references, tracks and variant files. Results remain visible
when selected. Indexed files support regional Open and full Download. UCSC database
tracks offer Download: compatible BED/genePred tables are converted into BED
annotations, retaining exon blocks and coding intervals; other tables are saved as
compressed TSV dumps. Container groups have no data file of their own and remain
visible with an explanation. Data tables are fetched from UCSC's bulk-download
server rather than a truncated REST query.

Downloads go directly into `gw_downloads` beside `.gw.ini`. Existing names receive
numeric suffixes; earlier hash subfolders remain untouched so saved references
continue to work. Only references are registered in `[genomes]`. Browser downloads
persist in IndexedDB, subject to storage limits and a 512 MiB per-file download cap.
Larger browser files should use Open for regional streaming; native downloads have
no such cap. File size and access information appear in catalogue tooltips.

Download regression checks:

```sh
c++ -std=c++17 -Isrc test/test_online_downloads.cpp src/online/downloads.cpp \
    src/online/online_catalogue.cpp src/online/table_export.cpp -lcurl -lz -o /tmp/gw-test-downloads
python3 test/test_online_downloads.py /tmp/gw-test-downloads
node test/test_wasm_downloads.mjs
```
