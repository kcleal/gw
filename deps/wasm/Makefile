# deps/wasm/Makefile
#
# WASM build wrapper for GW.
#
# Usage (from this directory):
#   make          — build gw.html / gw.js / gw.wasm / gw.data here
#   make -j8      — build using 8 parallel jobs  (or: make JOBS=8)
#   make clean    — remove build artefacts from repo root and this directory
#
# Paths to the WASM-compiled libraries (produced by build.sh / compile_wasm_libs.sh).
# Override on the command line if they live elsewhere.

# Number of parallel compile jobs.  Defaults to the value Make itself was
# invoked with (-j N), falling back to 1 if not specified.
JOBS ?= $(filter-out --jobserver%,$(MAKEFLAGS))

THIS_DIR       := $(abspath .)
REPO_ROOT      := $(abspath ../..)

WASM_SKIA_PATH ?= $(THIS_DIR)/out/skia_wasm
WASM_HTS_PATH  ?= $(THIS_DIR)/out/htslib
WASM_LZMA_PATH ?= $(THIS_DIR)/xz-5.2.5/src/liblzma/.libs

# Extra Emscripten link flags: preloaded test data + custom HTML shell.
# Kept here (not in the top-level Makefile) so the top-level stays generic.
WASM_LDFLAGS_EXTRA := \
    --preload-file $(REPO_ROOT)/test/test.fa@/test.fa \
    --preload-file $(REPO_ROOT)/test/test.fa.fai@/test.fa.fai \
    --preload-file $(REPO_ROOT)/test/test.bam@/test.bam \
    --preload-file $(REPO_ROOT)/test/test.bam.bai@/test.bam.bai \
    --pre-js $(THIS_DIR)/pre.js \
    --shell-file $(THIS_DIR)/shell.html

.PHONY: all clean

all:
	cd $(REPO_ROOT) && emmake $(MAKE) -j$(or $(JOBS),1) EMSCRIPTEN=1 \
	    TARGET=$(THIS_DIR)/gw.html \
	    WASM_SKIA_PATH=$(WASM_SKIA_PATH) \
	    WASM_HTS_PATH=$(WASM_HTS_PATH) \
	    WASM_LZMA_PATH=$(WASM_LZMA_PATH) \
	    "WASM_LDFLAGS_EXTRA=$(WASM_LDFLAGS_EXTRA)"

clean:
	cd $(REPO_ROOT) && $(MAKE) EMSCRIPTEN=1 TARGET=$(THIS_DIR)/gw.html clean
	rm -f $(THIS_DIR)/gw.html $(THIS_DIR)/gw.js \
	      $(THIS_DIR)/gw.wasm $(THIS_DIR)/gw.data
