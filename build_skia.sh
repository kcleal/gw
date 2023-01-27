#!/bin/bash

echo "Building skia\n"
v=m93-87e8842e8c
mkdir -p lib
cd lib
wget --no-check-certificate -O skia_build.zip https://github.com/JetBrains/skia-build/archive/refs/tags/${v}.zip && unzip -o skia_build.zip && rm skia_build.zip

cd skia-build-${v}
python3 script/checkout.py --version ${v}
python3 script/build.py
cd ../

mkdir -p skia
cp -rf skia-build-${v}/skia/out skia
cp -rf skia-build-${v}/skia/modules skia
cp -rf skia-build-${v}/skia/include skia
cp -rf skia-build-${v}/skia/src skia
rm -rf skia-build-${v}
echo "Building skia finished\n"
