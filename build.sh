

set -x -e

INCLUDE_PATH="${PREFIX}/include"
LIBRARY_PATH="${PREFIX}/lib"

if [ "$(uname)" == "Darwin" ]; then
    MACOSX_VERSION_MIN=10.6
    CXXFLAGS="-mmacosx-version-min=${MACOSX_VERSION_MIN}"
    CXXFLAGS="${CXXFLAGS} -stdlib=libstdc++"
    LINKFLAGS="-mmacosx-version-min=${MACOSX_VERSION_MIN}"
    LINKFLAGS="${LINKFLAGS} -stdlib=libstdc++ -L${LIBRARY_PATH}"

#    make

fi

if [ "$(uname)" == "Linux" ]; then
    CXXFLAGS="${CXXFLAGS} -stdlib=libstdc++"
    LINKFLAGS="${LINKFLAGS} -stdlib=libstdc++ -L${LIBRARY_PATH}"
    LDFLAGS="${LIBRARY_PATH}"
    mkdir -p skia
    cd skia && \
    wget https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip && \
    unzip Skia-m93-87e8842e8c-linux-Release-x64.zip && cd ..
    ls

    rm gw -rf
    cp -rf /home/kez/CLionProjects/gw_dev/gw gw_build  # change this to local gw
    #git clone https://github.com/kcleal/gw && mv gw gw_build

    cd gw_build && make && cd ..
    cp gw_build/gw .

fi