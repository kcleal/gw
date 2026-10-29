# deps/wasm/Makefile
#
# WASM build wrapper for GW.
#
# Usage (from this directory):
#   make          — build gw.html / gw.js / gw.wasm / gw.data here
#   make JOBS=8   — build using 8 parallel jobs
#   make clean    — remove demo outputs and isolated WASM GW objects
#   make serve    — serve the demo on http://127.0.0.1:8765/gw.html
#
# Paths to the WASM-compiled libraries (produced by build.sh / compile_wasm_libs.sh).
# Override on the command line if they live elsewhere.

# Number of parallel jobs when -j was not passed to make.
JOBS ?= 2

THIS_DIR       := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT      := $(abspath $(THIS_DIR)/../..)

# Prefer the published m143 package when unpacked locally; source builds remain supported.
WASM_SKIA_PACKAGE := $(THIS_DIR)/out/skia_m143
WASM_SKIA_SRC ?= $(if $(wildcard $(WASM_SKIA_PACKAGE)/out/Release/libskia.a),$(WASM_SKIA_PACKAGE),$(THIS_DIR)/skia)
WASM_SKIA_PATH ?= $(if $(wildcard $(WASM_SKIA_PACKAGE)/out/Release/libskia.a),$(WASM_SKIA_PACKAGE)/out/Release,$(THIS_DIR)/out/skia_wasm)
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
    --js-library $(THIS_DIR)/local-file-library.js \
    --shell-file $(THIS_DIR)/shell.html

.PHONY: all clean serve

all:
	@grep -Eq '^\#define SK_MILESTONE 143$$' $(WASM_SKIA_SRC)/include/core/SkMilestone.h || { echo "WASM requires Skia m143; rebuild dependencies with bash deps/wasm/build.sh"; exit 1; }
	cd $(REPO_ROOT) && env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS -u LDLIBS emmake $(MAKE) $(if $(filter -j%,$(MAKEFLAGS)),,-j$(JOBS)) EMSCRIPTEN=1 \
	    TARGET=$(THIS_DIR)/gw.html \
	    WASM_BUILD_DIR=$(THIS_DIR)/out/gw_objects_m143 \
	    WASM_SKIA_PATH=$(WASM_SKIA_PATH) \
	    WASM_SKIA_SRC=$(WASM_SKIA_SRC) \
	    WASM_HTS_PATH=$(WASM_HTS_PATH) \
	    WASM_LZMA_PATH=$(WASM_LZMA_PATH) \
	    "WASM_LDFLAGS_EXTRA=$(WASM_LDFLAGS_EXTRA)" \
	    "WASM_DEMO_INPUTS=$(THIS_DIR)/shell.html $(THIS_DIR)/pre.js $(THIS_DIR)/local-file-library.js $(REPO_ROOT)/test/test.fa $(REPO_ROOT)/test/test.fa.fai $(REPO_ROOT)/test/test.bam $(REPO_ROOT)/test/test.bam.bai"

serve:
	python3 $(THIS_DIR)/serve.py

clean:
	rm -rf $(THIS_DIR)/out/gw_objects $(THIS_DIR)/out/gw_objects_m143
	rm -f $(THIS_DIR)/gw.html $(THIS_DIR)/gw.js \
	      $(THIS_DIR)/gw.wasm $(THIS_DIR)/gw.data
