Get skia binaries from https://github.com/JetBrains/skia-build/releases/tag/m93-87e8842e8c

Aim for directory structure like this::

    ./dir
    ..../gw
    ..../skia


For linux::

    git clone https://github.com/kcleal/gw.git && \
    mkdir skia && cd skia && \
    wget https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip && \
    unzip Skia-m93-87e8842e8c-linux-Release-x64.zip && cd .. && \
    make

For Mac::

    brew install glew glfw3