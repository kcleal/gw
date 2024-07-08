#!/bin/bash

# Run this in the gw top-level directory (same as Makefile)
# Also dont forget to source emsdk environment
# source ~/Tools/emsdk/emsdk_env.sh

emcc --version
mkdir -p wasm_libs
cd wasm_libs

sudo apt-get install -y zlib1g-dev libbz2-dev libcurl4-gnutls-dev libssl-dev autoconf


# Compile LZMA to WebAssembly
LZMA_VERSION="5.2.5"
curl -LO "https://tukaani.org/xz/xz-${LZMA_VERSION}.tar.gz"
tar -xvf xz-${LZMA_VERSION}.tar.gz && rm xz*.gz
cd xz-${LZMA_VERSION}
emconfigure ./configure --disable-shared --disable-threads
emmake make -j4 CFLAGS="-Oz -fPIC -s USE_PTHREADS=0 -s EXPORT_ALL=1 -s ASSERTIONS=1"
cd ..


# Run ./configure
CFLAGS="-s USE_ZLIB=1 -s USE_BZIP2=1 ${CFLAGS_LZMA} -I./xz-${LZMA_VERSION}/src/liblzma/api/"
LDFLAGS="$LDFLAGS_LZMA -L./xz-${LZMA_VERSION}/src/liblzma/.libs/"
make clean
autoheader
autoconf
emconfigure ./configure CFLAGS="$CFLAGS -fPIC" LDFLAGS="$LDFLAGS --relocatable"


# Build htslib
curl -L -o htslib.tar.bz2 https://github.com/samtools/htslib/releases/download/1.20/htslib-1.20.tar.bz2
tar -xvf htslib.tar.bz2 && rm htslib.tar.bz2 && mv htslib-1.20 htslib
cd htslib
emmake make -j4 CC=emcc AR=emar \
    CFLAGS="-O2 -fPIC $CFLAGS" \
    LDFLAGS="$EM_FLAGS -O2 -s ERROR_ON_UNDEFINED_SYMBOLS=0 $LDFLAGS --relocatable"


git clone https://github.com/google/skia.git
cd skia
VERSION=m93
git checkout origin/chrome/${VERSION}
python3 tools/git-sync-deps
cd modules/canvaskit/
sed -i 's/-s LLD_REPORT_UNDEFINED//g' compile.sh
make release -j4
