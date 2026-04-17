# deps/wasm/build.sh — Build GW for WebAssembly
#
# Usage (from the GW repo root):
#   bash deps/wasm/build.sh
#
# The script:
#   1. Downloads Skia m133 source and builds it for WASM via its CanvasKit
#      compile.sh, then copies the intermediate static libs to deps/wasm/out/skia_wasm/
#   2. Downloads HTSlib compiled binary for WASM.
#   3. Invokes the GW Makefile via emmake with the correct flags.
#
# Prerequisites: emcc (Emscripten), python3, git, curl


set -e   # exit immediately on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GW_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

WASM_DIR="$SCRIPT_DIR"
OUT_DIR="$WASM_DIR/out"
SKIA_SRC="$WASM_DIR/skia"
SKIA_OUT="$OUT_DIR/skia_wasm"
LZMA_VERSION="5.2.5"
LZMA_SRC="$WASM_DIR/xz-${LZMA_VERSION}"
LZMA_INC="$LZMA_SRC/src/liblzma/api"
LZMA_LIBDIR="$LZMA_SRC/src/liblzma/.libs"
HTS_SRC="$WASM_DIR/htslib_src"
HTS_OUT="$OUT_DIR/htslib"

SKIA_BRANCH="chrome/m133"
SKIA_REPO="https://github.com/google/skia.git"

cd "$GW_ROOT"

echo "=== Checking emcc ==="
emcc --version

mkdir -p "$OUT_DIR"


export EMSDK_PYTHON="${EMSDK_PYTHON:-$(command -v python3)}"
echo "Using Python for emcc: $EMSDK_PYTHON ($($EMSDK_PYTHON --version 2>&1))"

# Path to system emsdk.  Detected from $EMSDK env var (set when
# you source your emsdk_env.sh) with a fallback to a known default path.
# This emsdk must be able to install EMSCRIPTEN_TARGET; its upstream/ dir
# is what Skia's GN toolchain will use via the skia_emsdk_dir GN arg.
USER_EMSDK="${EMSDK:-$HOME/Documents/tools/emsdk}"
if [ ! -f "$USER_EMSDK/emsdk.py" ]; then
    echo "ERROR: Cannot find emsdk at $USER_EMSDK"
    echo "       Source your emsdk_env.sh first, or set EMSDK= to the emsdk root."
    exit 1
fi

# Emscripten version used for ALL build steps (Skia, LZMA, HTSlib, GW).
EMSCRIPTEN_TARGET="3.1.65"
SKIA_STAMP="$SKIA_OUT/.built_with_emcc_${EMSCRIPTEN_TARGET}"

# ---------------------------------------------------------------------------
# 1. Build Skia m133 for WASM
# ---------------------------------------------------------------------------
# Skia's DEPS hooks install a pinned emsdk (3.1.44) into
# third_party/externals/emsdk/ and Skia's GN toolchain uses that path via
# the skia_emsdk_dir GN arg (declare_args() in gn/toolchain/wasm.gni).
# We override skia_emsdk_dir to point at the user's emsdk (which has a
# modern emsdk.py able to install 4.x/5.x) by patching compile.sh's gn gen
# call.  This ensures Skia and GW share the same emcc ABI, fixing:
#   "undefined symbol: saveSetjmp / testSetjmp"
if [ -f "$SKIA_OUT/libskia.a" ] && [ -f "$SKIA_STAMP" ]; then
    echo "=== Skia WASM libs already built with this Emscripten — skipping ==="
else
    echo "=== Cloning Skia ($SKIA_BRANCH) ==="
    if [ ! -d "$SKIA_SRC" ]; then
        git clone --branch "$SKIA_BRANCH" --depth 1 "$SKIA_REPO" "$SKIA_SRC"
    fi

    cd "$SKIA_SRC"

    echo "=== Syncing Skia third-party dependencies ==="
    python3 tools/git-sync-deps

    # Install the target Emscripten version into the USER's emsdk (not Skia's
    # bundled one — its emsdk.py is too old to know about 4.x/5.x releases).
    echo "=== Installing Emscripten $EMSCRIPTEN_TARGET into user emsdk ($USER_EMSDK) ==="
    python3 "$USER_EMSDK/emsdk.py" install "$EMSCRIPTEN_TARGET"
    python3 "$USER_EMSDK/emsdk.py" activate "$EMSCRIPTEN_TARGET"

    SKIA_BUNDLED_EMSDK="$SKIA_SRC/third_party/externals/emsdk"
    if [ -d "$SKIA_BUNDLED_EMSDK" ]; then
        echo "=== Symlinking Skia bundled emsdk upstream/ → user emsdk upstream/ ==="
        rm -rf "$SKIA_BUNDLED_EMSDK/upstream"
        ln -s "$USER_EMSDK/upstream" "$SKIA_BUNDLED_EMSDK/upstream"
    fi

    # Remove old Skia build artifacts so ninja doesn't reuse objects compiled
    # with a different Emscripten version (toolchain change isn't always detected).
    rm -rf "$SKIA_SRC/out/canvaskit_wasm"

    echo "=== Fetching GN build tool ==="
    ./bin/fetch-gn

    echo "=== Building Skia CanvasKit for WASM (single-threaded, no pthreads) ==="
    cd "$SKIA_SRC/modules/canvaskit"
    # Remove old flags that no longer exist in newer lld / Emscripten
    sed -i.bak 's/-s LLD_REPORT_UNDEFINED//g' compile.sh
    sed -i.bak2 's/-s ERROR_ON_UNDEFINED_SYMBOLS=1//g' compile.sh
    # Build — produces libskia.a, libskcms.a, libjpeg.a, libpng.a in out/canvaskit_wasm/
    bash compile.sh

    echo "=== Copying Skia WASM static libs ==="
    mkdir -p "$SKIA_OUT"
    find "$SKIA_SRC/out/canvaskit_wasm" -maxdepth 1 -name '*.a' -exec cp {} "$SKIA_OUT/" \;
    echo "Libs copied to $SKIA_OUT:"
    ls "$SKIA_OUT/"
    touch "$SKIA_STAMP"

    cd "$GW_ROOT"
fi

# ---------------------------------------------------------------------------
# Activate the target Emscripten from the user's emsdk so that
# emmake for LZMA, HTSlib, and GW all resolve to the same emcc as libskia.a.
# ---------------------------------------------------------------------------
echo "=== Activating Emscripten $EMSCRIPTEN_TARGET from user emsdk ==="
# shellcheck source=/dev/null
source "$USER_EMSDK/emsdk_env.sh"
echo "Active emcc: $(emcc --version | head -1)"
# Stamp token from the now-active emcc.
EMCC_VER=$(emcc --version 2>&1 | head -1 | sed 's/[^A-Za-z0-9._-]/_/g')


# ---------------------------------------------------------------------------
# 2a. Build LZMA (xz-utils) for WASM — required by HTSlib
# ---------------------------------------------------------------------------
LZMA_STAMP="$WASM_DIR/.lzma_${EMCC_VER}"
if [ -f "$LZMA_LIBDIR/liblzma.a" ] && [ -f "$LZMA_STAMP" ]; then
    echo "=== LZMA already built with matching Emscripten — skipping ==="
else
    echo "=== Downloading xz-utils $LZMA_VERSION ==="
    if [ ! -d "$LZMA_SRC" ]; then
        curl -L -o "$WASM_DIR/xz-${LZMA_VERSION}.tar.gz" \
            "https://tukaani.org/xz/xz-${LZMA_VERSION}.tar.gz"
        tar -xzf "$WASM_DIR/xz-${LZMA_VERSION}.tar.gz" -C "$WASM_DIR"
        rm "$WASM_DIR/xz-${LZMA_VERSION}.tar.gz"
    fi

    cd "$LZMA_SRC"
    # Clean stale objects from any previous build (may have used a different emcc)
    make distclean 2>/dev/null || make clean 2>/dev/null || true
    echo "=== Configuring LZMA for WASM ==="
    emconfigure ./configure --disable-shared --disable-threads

    echo "=== Building LZMA for WASM ==="
    NCPUS="$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
    emmake make -j"$NCPUS" \
        CFLAGS="-Oz -fPIC -s USE_PTHREADS=0 -s EXPORT_ALL=1 -s ASSERTIONS=1"

    touch "$LZMA_STAMP"
    echo "=== LZMA built at $LZMA_LIBDIR ==="
    cd "$GW_ROOT"
fi

# ---------------------------------------------------------------------------
# 2b. Build HTSlib for WASM (headers + libhts.a)
# ---------------------------------------------------------------------------
HTSLIB_VERSION="1.21"
HTS_STAMP="$WASM_DIR/.htslib_${EMCC_VER}"
if [ -f "$HTS_OUT/libhts.a" ] && [ -f "$HTS_STAMP" ]; then
    echo "=== HTSlib already built with matching Emscripten — skipping ==="
else
    echo "=== Downloading HTSlib $HTSLIB_VERSION source ==="
    # Check for BOTH configure and Makefile: previous build attempts can delete
    # the Makefile (e.g. via make distclean) while leaving configure behind,
    # which would cause "No rule to make target" on the next run.
    if [ ! -f "$HTS_SRC/configure" ] || [ ! -f "$HTS_SRC/Makefile" ]; then
        rm -rf "$HTS_SRC"
        mkdir -p "$HTS_SRC"
        curl -L -o "$WASM_DIR/htslib.tar.bz2" \
            "https://github.com/samtools/htslib/releases/download/${HTSLIB_VERSION}/htslib-${HTSLIB_VERSION}.tar.bz2"
        tar -xjf "$WASM_DIR/htslib.tar.bz2" -C "$WASM_DIR"
        rm "$WASM_DIR/htslib.tar.bz2"
        cp -r "$WASM_DIR/htslib-${HTSLIB_VERSION}/." "$HTS_SRC/"
        rm -rf "$WASM_DIR/htslib-${HTSLIB_VERSION}"
    fi

    # Copy headers now (platform-agnostic — safe to do before building).
    mkdir -p "$HTS_OUT"
    cp -r "$HTS_SRC/htslib" "$HTS_OUT/"

    cd "$HTS_SRC"
    echo "=== Configuring HTSlib for WASM ==="

    # Pre-build Emscripten's zlib and bzip2 ports so their headers and static
    # libs exist in the sysroot cache before configure's link tests run.
    # Without this, HTSlib's autoconf zlib check fails because the port library
    # hasn't been compiled into the sysroot yet.
    echo "=== Pre-building Emscripten zlib/bzip2 ports ==="
    embuilder build zlib bzip2

    # Regenerate configure from configure.ac so the script is not the
    # pre-packaged tarball version (which can have platform quirks that cause
    # it to detect libcurl even when --disable-libcurl is passed).
    # Following the biowasm pattern: make clean → autoheader → autoconf → emconfigure.
    make clean || true
    autoheader
    autoconf
    # Keep -s flags OUT of configure CFLAGS (they break the compiler test);
    # they are added back in the emmake step.
    # curl is unusable in the browser sandbox; disable all network backends.
    # --relocatable must NOT be in LDFLAGS here: autoconf tries to link a test
    # executable during the "C compiler works" check, and --relocatable produces
    # a relocatable object instead of an executable, causing that check to fail.
    emconfigure ./configure \
        --disable-libcurl \
        --disable-gcs \
        --disable-s3 \
        --disable-plugins \
        CFLAGS="-O2 -fPIC -I${LZMA_INC}" \
        LDFLAGS="-L${LZMA_LIBDIR}"

    echo "=== Building HTSlib for WASM ==="
    NCPUS="$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
    emmake make -j"$NCPUS" CC=emcc AR=emar \
        CFLAGS="-O2 -fPIC -s USE_ZLIB=1 -s USE_BZIP2=1 -I${LZMA_INC}" \
        LDFLAGS="-O2 -L${LZMA_LIBDIR} --relocatable" \
        libhts.a

    cp libhts.a "$HTS_OUT/"
    touch "$HTS_STAMP"
    echo "=== HTSlib built at $HTS_OUT ==="
    cd "$GW_ROOT"
fi

# ---------------------------------------------------------------------------
# 3. Build GW for WASM
# ---------------------------------------------------------------------------
echo "=== Building GW for WASM ==="
# Clean stale native .o files first — emmake will not recompile them
# on its own, and wasm-ld rejects x86_64 objects as "unknown file type".
make clean
emmake make -j"$NCPUS" EMSCRIPTEN=1 \
    WASM_SKIA_PATH="$SKIA_OUT" \
    WASM_HTS_PATH="$HTS_OUT" \
    WASM_LZMA_PATH="$LZMA_LIBDIR"

echo ""
echo "=== Done! Output: gw.html + gw.js + gw.wasm ==="
