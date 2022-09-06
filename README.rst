Get skia binaries from https://github.com/JetBrains/skia-build/releases/tag/m93-87e8842e8c

Aim for directory structure like this::

    ./dir
    ..../gw
    ..../skia

Installing dependencies
-----------------------

For linux::
    
    sudo apt install clang cmake 
    sudo apt install libglfw3 libglfw-dev

or::
    
    wget https://github.com/glfw/glfw/releases/download/3.3.8/glfw-3.3.8.zip && \
    cd glfw-3.3.8 && \
    cmake -S . -B build && \
    cd build && \
    sudo make install

For Mac::

    brew install glew glfw3

Installing gw:
===============

For linux::

    git clone https://github.com/kcleal/gw.git && \
    mkdir skia && cd skia && \
    wget https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip && \
    unzip Skia-m93-87e8842e8c-linux-Release-x64.zip && cd ../gw && \
    make

For mac::

    git clone https://github.com/kcleal/gw.git && \
    mkdir skia && cd skia && \
    wget https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-macos-Release-x64.zip && \
    unzip Skia-m93-87e8842e8c-linux-Release-x64.zip && cd ../gw && \
    make


Remote
======

gw can be used on remote servers. Simply use `ssh -X remote` and when gw gets used, the window will show up on your screen.
