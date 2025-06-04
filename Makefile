TARGET = gw
VERSION = "1.2.1"
.PHONY: default all debug clean
default: $(TARGET)
all: default
debug: default

##########################################################
# System info
PLATFORM=
TARGET_OS=
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(OS),Windows_NT)  # assume we are using msys2-ucrt64 env
    PLATFORM = "Windows"
    TARGET_OS = "Windows"
else
    ifdef EMSCRIPTEN
        PLATFORM = "Emscripten"
        TARGET_OS = "Wasm"
    else ifeq ($(UNAME_S),Linux)
        TARGET_OS = "Linux"
        ifeq ($(UNAME_M),aarch64)
            PLATFORM = "Linux-aarch64"
        else
            PLATFORM = "Linux-x64"
        endif
    else ifeq ($(UNAME_S),Darwin)
        TARGET_OS = "MacOS"
        ifeq ($(shell uname -m), arm64)
            PLATFORM = "MacOS-arm64"
        else
            PLATFORM = "MacOS-x64"
        endif
    endif
endif
$(info   PLATFORM=$(PLATFORM))
ifneq ($(PLATFORM),"Emscripten")
    ifdef CONDA_PREFIX
    	$(info   Linking conda from $(CONDA_PREFIX))
        CPPFLAGS += -I$(CONDA_PREFIX)/include
        LDFLAGS += -L$(CONDA_PREFIX)/lib
        LDFLAGS += -Wl,-rpath,$(CONDA_PREFIX)/lib
    endif
endif

##########################################################
# Skia info
OLD_SKIA ?= 0
prep:
ifeq ($(OLD_SKIA),0)
	@if [ "$(PLATFORM)" = "MacOS-x64" ]; then \
		echo "Downloading pre-built skia-m133 for MacOS-Intel"; mkdir -p lib/skia; \
		cd lib/skia && curl -L -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.1.0/skia-m133-macos-Release-x64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "MacOS-arm64" ]; then \
      	echo "Downloading pre-built skia-m133 for MacOS-Arm64"; mkdir -p lib/skia; \
      	cd lib/skia && curl -L -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.1.0/skia-m133-macos-Release-arm64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux-x64" ]; then \
		echo "Downloading pre-built skia-m133 for Linux-x64"; mkdir -p lib/skia; \
		cd lib/skia && curl -L -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.1.0/skia-m133-linux-Release-x64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux-aarch64" ]; then \
    	echo "Downloading pre-built skia-m133 for Linux-aarch64"; mkdir -p lib/skia; \
    	cd lib/skia && curl -L -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.1.0/skia-m133-linux-Release-arm64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	fi
else
	@if [ "$(PLATFORM)" = "MacOS-x64" ]; then \
		echo "Downloading pre-built skia-m93 for MacOS-Intel"; mkdir -p lib/skia; \
		cd lib/skia && curl -L -o skia.zip "https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-macos-Release-x64.zip" && unzip -o skia.zip && rm skia.zip && cd ../../; \
	elif [ "$(PLATFORM)" = "MacOS-arm64" ]; then \
      	echo "Downloading pre-built skia-m93 for MacOS-Arm64"; mkdir -p lib/skia; \
      	cd lib/skia && curl -L -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.0.1/skia.zip" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux-x64" ]; then \
		echo "Downloading pre-built skia-m93 for Linux-x64"; mkdir -p lib/skia; \
		cd lib/skia && curl -L -o skia.zip "https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip" && unzip -o skia.zip && rm skia.zip && cd ../../; \
	fi
endif
SKIA_PATH := $(shell find ./lib/skia/out -type d -name '*Release*' | sort | head -n 1)

##########################################################
# Flags and libs
ifeq ($(OLD_SKIA),1)
	CXXFLAGS += -D OLD_SKIA -D USE_GL
endif
CXXFLAGS += -Wall -std=c++17 -fno-common -fwrapv -fno-omit-frame-pointer -O3 -DNDEBUG -g
LIBGW_INCLUDE=
CPPFLAGS += -I./lib/skia -I./lib/libBigWig -I./include -I. $(LIBGW_INCLUDE) -I./src
LDFLAGS += -L$(SKIA_PATH)
LDLIBS += -lskia -lm -ljpeg -lpng -lpthread
ifeq ($(TARGET_OS),"Linux")  # set platform flags and libs
    CXXFLAGS += -D LINUX -D __STDC_FORMAT_MACROS
    LDLIBS += -lX11
    ifeq ($(USE_WAYLAND),1)
    	LDLIBS += -lwayland-client -lwayland-egl
    endif
    LGLFW := $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")
    LDLIBS += -lGL -lEGL -lGLESv2 -lhts -lfreetype -luuid -lz -lcurl -licu -ldl $(LGLFW) -lsvg -lfontconfig
else ifeq ($(TARGET_OS),"MacOS")
    ifeq ($(PLATFORM),"MacOS-x64")
        CXXFLAGS += -arch x86_64
    else
        CXXFLAGS += -arch arm64
    endif
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D OSX -stdlib=libc++ -fvisibility=hidden -mmacosx-version-min=11 -Wno-deprecated-declarations
    LDFLAGS += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices -mmacosx-version-min=11 -L/usr/local/lib
    LGLFW := $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")
    LDLIBS += -lhts $(LGLFW) -lzlib -lcurl -licu -ldl -lsvg -lfontconfig
else ifeq ($(PLATFORM),"Windows")  # Targets an msys2 build environment
    CXXFLAGS += -D WIN32 -D OLD_SKIA
    SKIA_CFLAGS := $(shell pkg-config --cflags skia 2>/dev/null || echo "-I/ucrt64/include/skia")
    SKIA_LIBS := $(shell pkg-config --libs skia 2>/dev/null || echo "-lskia")
    NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null || echo "")
    CPPFLAGS += $(SKIA_CFLAGS) $(NCURSES_CFLAGS)
    LDLIBS += $(SKIA_LIBS) -lhts -lharfbuzz-subset -lglfw3 -lcurl -lsvg -lfontconfig
else ifeq ($(PLATFORM),"Emscripten")
    CPPFLAGS += -v --use-port=contrib.glfw3 -sUSE_ZLIB=1 -sUSE_FREETYPE=1 -sUSE_ICU=1  -I/usr/local/include
    CFLAGS += -fPIC
    CXXFLAGS += -DBUILDING_LIBGW -D__STDC_FORMAT_MACROS -fPIC
    LDFLAGS += -v -L./wasm_libs/htslib -s RELOCATABLE=1 --no-entry -s STANDALONE_WASM
    LDLIBS += -lwebgl.js -l:libhts.a
endif

##########################################################
# Compile
OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))
OBJECTS += $(patsubst %.c, %.o, $(wildcard ./lib/libBigWig/*.c))
OBJECTS += $(patsubst %.c, %.o, $(wildcard ./include/*.c))

debug: LDFLAGS += -fsanitize=address -fsanitize=undefined

$(TARGET): $(OBJECTS)  # line 131
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@


clean:
	-rm -f *.o ./src/*.o ./src/*.o.tmp ./lib/libBigWig/*.o ./include/*.o
	-rm -f $(TARGET) *.wasm
	-rm -rf libgw


ifeq ($(UNAME_S),Linux)
    SHARED_TARGET = libgw.so
    FORCELOAD_SKIA = -Wl,--whole-archive $(SKIA_PATH)/libskia.a -Wl,--no-whole-archive
endif
ifeq ($(UNAME_S),Darwin)
    SHARED_TARGET = libgw.dylib
    FORCELOAD_SKIA = -Wl,-force_load,$(SKIA_PATH)/libskia.a
endif

shared: CXXFLAGS += -fPIC -DBUILDING_LIBGW
shared: CFLAGS += -fPIC
shared: $(OBJECTS)

ifeq ($(PLATFORM),"Windows")
	$(error Shared library build is not supported on Windows)
endif

# Create shared library-specific LDLIBS with force-loaded Skia (Linux/macOS only)
ifdef FORCELOAD_SKIA
	$(eval SHARED_LDLIBS := $(filter-out -lskia -ljpeg -lpng -lzlib,$(LDLIBS)) $(FORCELOAD_SKIA))
else
	$(eval SHARED_LDLIBS := $(LDLIBS))
endif

ifeq ($(UNAME_S),Darwin)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(SHARED_LDLIBS) -dynamiclib -DBUILDING_LIBGW -install_name @rpath/libgw.dylib -current_version $(VERSION) -compatibility_version $(VERSION) -o $(SHARED_TARGET)
else
	$(CXX) $(OBJECTS) $(LDFLAGS) $(SHARED_LDLIBS) -shared -DBUILDING_LIBGW -Wl,-soname,libgw.so -Wl,-rpath,\$$ORIGIN -o $(SHARED_TARGET)
endif
	-mkdir -p libgw libgw/GW
	-cp $(SKIA_PATH)/libskia.a libgw
	-cp src/*.h libgw/GW
	-cp include/*.h* libgw/GW
	-cp lib/libBigWig/*.h libgw/GW
	-cp -rf lib/skia/include libgw/GW
	-cp -rf lib/skia/modules libgw/GW
	-mv $(SHARED_TARGET) libgw
