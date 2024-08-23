#!/bin/bash

NAME=${PWD}/lib/skia
echo "Skia out folder is: $NAME"

cd ./lib
mkdir -p ${NAME}
mkdir -p build_skia && cd build_skia

ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    EXTRA_CFLAGS='extra_cflags=["-mavx512f", "-mssse3", "-frtti"]'
else
    #elif [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]; then
    EXTRA_CFLAGS=""
fi
echo "Extra flags: ${EXTRA_CFLAGS}"


git clone 'https://chromium.googlesource.com/chromium/tools/depot_tools.git'
export PATH="${PWD}/depot_tools:${PATH}"

# sudo npm install -g @bazel/bazelisk
# sudo apt-get install -y ninja-build libharfbuzz-dev libwebp-dev
#brew install bazelisk ninja harfbuzz webp

depot_tools/fetch skia

cd skia
VERSION=m93

python3 tools/git-sync-deps

REL=Release


# Turn off all GPU backends
bin/gn gen out/${REL} --args="is_official_build=true skia_enable_egl=true skia_use_system_icu=false skia_use_system_zlib=false skia_use_system_expat=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_harfbuzz=false skia_pdf_subset_harfbuzz=true skia_enable_skottie=true ${EXTRA_CFLAGS}"
bin/gn args out/${REL} --list
ninja -C out/${REL}


mkdir -p ${NAME}/out
mkdir -p ${NAME}/third_party/externals


cp -rf out/${REL}/* ${NAME}/out
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
