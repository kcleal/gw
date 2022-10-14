GW
==

.. image:: inc/banner.png
    :align: center


GW is a fast genome browser for sequencing data (.bam/.cram format) that is used directly from the terminal. GW can also be used to
view and label variant data from vcf files, and display these as image-tiles for annotation. Check out the examples below!


Installing GW
--------------

The easiest way to get GW up and running is to grab one of the pre-built binaries from the release page::

    wget https://github.com/kcleal/gw/releases/gw....blah

GW is built using clang and make, and requires glfw and skia libraries. To get glfw.
If you need to build GW from source, we have put together a build script to try and make this pain free. You can run this using one of the
following::

    build_gw.sh linux
    build_gw.sh mac
    build_gw.sh windows

If you want to manually build GW, we recommend using a pre-built skia binary from jetbrains https://github.com/JetBrains/skia-build/releases/tag/m93-87e8842e8c.
Aim for directory structure like this::

    ./dir
    ..../gw
    ..../skia

And build GW using::

    cd gw && make


Old build instructions
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

Get skia binaries from https://github.com/JetBrains/skia-build/releases/tag/m93-87e8842e8c

Aim for directory structure like this::

    ./dir
    ..../gw
    ..../skia

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

User Guide
==========

For labelling data, make sure all variantIDs in your inout vcf are unique!


Remote
======

gw can be used on remote servers. Simply use `ssh -X remote` and when gw gets used, the window will show up on your screen.



