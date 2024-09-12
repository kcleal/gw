#!/bin/bash
# Requires clang compiler
# Linux deps:
# sudo apt-get install -y ninja-build libharfbuzz-dev libwebp-dev
# Apple deps:
# brew install bazelisk ninja harfbuzz webp
# Run this script from to gw directory
#    bash ./deps/build_skia.sh

NAME=${PWD}/lib/skia
echo "Skia out folder is: $NAME"

cd ./lib
mkdir -p ${NAME}
mkdir -p build_skia && cd build_skia

# Detect architecture and OS
ARCH=$(uname -m)
OS=$(uname -s)

# Set default flags
EXTRA_CFLAGS=""
EXTRA_LDFLAGS=""

# Architecture-specific flags
if [ "$ARCH" = "x86_64" ]; then
    EXTRA_CFLAGS='extra_cflags=["-mavx2", "-mfma", "-mavx512f", "-mavx512dq", "-msse4.2", "-mpopcnt", "-frtti"]'
    EXTRA_LDFLAGS='extra_ldflags=["-mavx2", "-mfma", "-mavx512f", "-mavx512dq"]'
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    EXTRA_CFLAGS='extra_cflags=["-march=armv8-a+crc+crypto", "-frtti"]'
    EXTRA_LDFLAGS='extra_ldflags=["-march=armv8-a+crc+crypto"]'
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

# OS-specific flags
if [ "$OS" = "Darwin" ]; then
    # macOS-specific flags
    EXTRA_CFLAGS="${EXTRA_CFLAGS::-1}, \"-mmacosx-version-min=10.13\"]"
    EXTRA_LDFLAGS="${EXTRA_LDFLAGS::-1}, \"-mmacosx-version-min=10.13\"]"
    EXTRA_ARGS="skia_use_gl=true skia_use_metal=true"
elif [ "$OS" = "Linux" ]; then
    # Linux-specific flags
    EXTRA_ARGS="skia_use_egl=true skia_use_gl=true skia_use_x11=true"
else
    echo "Unsupported OS: $OS"
    exit 1
fi

echo "Architecture: $ARCH"
echo "Operating System: $OS"
echo "Extra C flags: $EXTRA_CFLAGS"
echo "Extra LD flags: $EXTRA_LDFLAGS"
echo "Extra arguments: $EXTRA_ARGS"

git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="${PWD}/depot_tools:${PATH}"
depot_tools/fetch skia

cd skia
VERSION=m93  #-87e8842e8c
git checkout origin/chrome/${VERSION}
echo "Checked out Skia version: ${VERSION}"
python3 tools/git-sync-deps

REL=Release

# Enable EGL and GL backends, X11 and Wayland
#bin/gn gen out/${REL} --args="is_official_build=true skia_use_system_icu=false skia_use_system_zlib=false skia_use_system_expat=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_harfbuzz=false skia_pdf_subset_harfbuzz=true skia_enable_skottie=true skia_use_egl=true skia_use_gl=true skia_use_x11=truem target_cpu=\"${ARCH}\" ${EXTRA_CFLAGS} cc=\"clang\" cxx=\"clang++\""

# Generate build files
bin/gn gen out/${REL} --args="is_official_build=true \
    skia_use_system_icu=false \
    skia_use_system_zlib=false \
    skia_use_system_expat=false \
    skia_use_system_libjpeg_turbo=false \
    skia_use_system_libpng=false \
    skia_use_system_libwebp=false \
    skia_use_system_harfbuzz=false \
    skia_pdf_subset_harfbuzz=true \
    skia_enable_skottie=true \
    target_cpu=\"${ARCH}\" \
    ${EXTRA_CFLAGS} \
    ${EXTRA_LDFLAGS} \
    cc=\"clang\" \
    cxx=\"clang++\" \
    ${EXTRA_ARGS}"
bin/gn args out/${REL} --list
ninja -C out/${REL}

mkdir -p ${NAME}/out/${REL}
mkdir -p ${NAME}/third_party/externals

cp -rf out/${REL}/* ${NAME}/out/${REL}
cp -rf include ${NAME}
cp -rf modules ${NAME}
cp -rf src ${NAME}

libs=( "freetype" "harfbuzz" "icu" "libpng" "zlib" )

for l in "${libs[@]}"
do
  echo $l
  mkdir -p ${NAME}/third_party/externals/${l}
  cp -rf third_party/externals/${l}/src ${NAME}/third_party/externals/${l}
  cp -rf third_party/externals/${l}/include ${NAME}/third_party/externals/${l}
  cp -rf third_party/externals/${l}/source ${NAME}/third_party/externals/${l}
  cp third_party/externals/${l}/*.h ${NAME}/third_party/externals/${l}
done

cp -rf third_party/icu ${NAME}/third_party

# clean up
cd ${NAME}
rm -rf modules/skottie/tests
rm -rf out/${REL}/obj
rm -rf out/${REL}/gen
rm -rf out/${REL}/gcc_like_host
find . -name "*.clang-format" -type f -delete
find . -name "*.gitignore" -type f -delete
find . -name "*.md" -type f -delete
find . -name "*.gn" -type f -delete
find . -name "*.ninja" -type f -delete
find . -name "*.cpp" -type f -delete
find . -name "*.ninja.d" -type f -delete
find . -name "*BUILD*" -type f -delete
find . -name "*.txt" -type f -delete
find . -name "test*" -type d -exec rm -rv {} +
cd ../

pwd
rm -rf build_skia

echo "DONE"