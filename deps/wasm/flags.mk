# deps/wasm/flags.mk
#
# Emscripten/WASM compile and link flags.
# Included by the root Makefile when EMSCRIPTEN=1 is set.
#
# When invoked via deps/wasm/Makefile the path variables below are passed
# explicitly on the command line.  The ?= defaults let you also run
#   emmake make EMSCRIPTEN=1
# directly from the repo root without the wrapper Makefile.

WASM_SKIA_PATH ?= ./deps/wasm/out/skia_wasm
WASM_HTS_PATH  ?= ./deps/wasm/out/htslib
WASM_LZMA_PATH ?= ./deps/wasm/xz-5.2.5/src/liblzma/.libs

SKIA_PATH := $(WASM_SKIA_PATH)

# Flags that must appear at BOTH compile and link time.
# Pthreads are NOT enabled: the Skia CanvasKit build is single-threaded by
# default, so GW must also be single-threaded to avoid --shared-memory conflicts.
EM_SHARED_FLAGS := --use-port=contrib.glfw3 \
                   -sUSE_ZLIB=1 -sUSE_BZIP2=1 -sUSE_FREETYPE=1

CPPFLAGS += $(EM_SHARED_FLAGS) -I$(WASM_HTS_PATH)

# Match the sk_sp ABI used by the CanvasKit-built libskia.a (is_trivial_abi=true).
CXXFLAGS += -D__STDC_FORMAT_MACROS -O2 "-DSK_TRIVIAL_ABI=[[clang::trivial_abi]]"

LDFLAGS  += -L$(WASM_SKIA_PATH) -L$(WASM_HTS_PATH) -L$(WASM_LZMA_PATH) \
            $(EM_SHARED_FLAGS) \
            -sUSE_WEBGL2=1 -sFULL_ES2=1 \
            -sALLOW_MEMORY_GROWTH=1 \
            -sTOTAL_STACK=4194304 \
            -sINITIAL_MEMORY=268435456 \
            -sMIN_WEBGL_VERSION=2 \
            -sNO_EXIT_RUNTIME=1 \
            -sFORCE_FILESYSTEM=1 \
            -sEXPORTED_RUNTIME_METHODS=callMain,Asyncify \
            -sASYNCIFY=1 \
            -sASYNCIFY_STACK_SIZE=4194304 \
            $(WASM_LDFLAGS_EXTRA) \
            -sEXIT_RUNTIME=0

SKIA_LIBS := -lskia -lskcms -ljpeg -lpng
LDLIBS    += $(SKIA_LIBS) -l:libhts.a -l:liblzma.a
