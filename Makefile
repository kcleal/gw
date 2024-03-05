TARGET = gw

.PHONY: default all debug clean

default: $(TARGET)

all: default
debug: default

# set system
PLATFORM=
UNAME_S := $(shell uname -s)
ifeq ($(OS),Windows_NT)  # assume we are using msys2-ucrt64 env
    PLATFORM = "Windows"
else
    ifeq ($(UNAME_S),Linux)
        PLATFORM = "Linux"
    else ifeq ($(UNAME_S),Darwin)
        ifeq ($(shell uname -m), arm64)
            PLATFORM = "Arm64"
        else
            PLATFORM = "Darwin"
        endif
    endif
endif

# try and add conda environment
ifdef CONDA_PREFIX
    CPPFLAGS += -I$(CONDA_PREFIX)/include
    LDFLAGS += -L$(CONDA_PREFIX)/lib
endif

# Options to use target htslib or skia
ifdef HTSLIB
    CPPFLAGS += -I$(HTSLIB)
    LDFLAGS += -L$(HTSLIB)
endif

SKIA ?= ""
SKIA_PATH=""
ifneq ($(PLATFORM), "Windows")
    ifneq ($(SKIA),"")
        CPPFLAGS += -I$(SKIA)
        SKIA_PATH = $(wildcard $(SKIA)/out/Rel*)
    else ifeq ($(PLATFORM),"Darwin")
        CPPFLAGS += -I./lib/skia
        SKIA_PATH = ./lib/skia/out/Release-x64
    else ifeq ($(PLATFORM),"Arm64")
        CPPFLAGS += -I./lib/skia
        SKIA_PATH = ./lib/skia/out/Release-arm64
    else
    	CPPFLAGS += -I./lib/skia
        SKIA_PATH = ./lib/skia/out/Release-x64
    endif
endif
LDFLAGS += -L$(SKIA_PATH)

SKIA_LINK=""
USE_GL ?= ""  # Else use EGL backend for Linux only

prep:
	@if [ "$(PLATFORM)" = "Darwin" ]; then \
		SKIA_LINK=""; \
		echo "Downloading pre-built skia for MacOS-Intel"; mkdir -p lib/skia; \
		cd lib/skia && curl -L -o skia.zip "https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-macos-Release-x64.zip" && unzip -o skia.zip && rm skia.zip && cd ../../; \
	elif [ "$(PLATFORM)" = "Arm64" ]; then \
      	echo "Downloading pre-built skia for MacOS-Arm64"; mkdir -p lib/skia; \
      	cd lib/skia && curl -L -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.0.1/skia.zip" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux" ] && [ "$(USE_GL)" = "1" ]; then \
		echo "Downloading pre-built skia for Linux with GL"; mkdir -p lib/skia; \
		cd lib/skia && curl -L -o skia.zip "https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip" && unzip -o skia.zip && rm skia.zip && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux" ]; then \
		echo "Downloading pre-built skia for Linux with EGL"; mkdir -p lib/skia; \
		cd lib/skia && curl -L -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.0.1/skia-m93-linux-Release-x64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	fi


CXXFLAGS += -Wall -std=c++17 -fno-common -fwrapv -fno-omit-frame-pointer -O3 -DNDEBUG

LIBGW_INCLUDE=
shared: LIBGW_INCLUDE=-I./libgw
CPPFLAGS += -I./lib/libBigWig -I./include -I. $(LIBGW_INCLUDE) -I./src

LDLIBS += -lskia -lm -ljpeg -lpng -lsvg -lhts -lfontconfig -lpthread

ifeq ($(PLATFORM),"Linux")  # set platform flags and libs
    ifeq (${XDG_SESSION_TYPE},"wayland")  # wayland is untested!
        LDLIBS += -lwayland-client
    else
        LDLIBS += -lX11
    endif
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D LINUX -D __STDC_FORMAT_MACROS
    LDFLAGS += -L/usr/local/lib
    # If installed from conda, glfw3 is named glfw, therefore if glfw3 is installed by another means use this:
#     LDLIBS += -lGL -lfreetype -lfontconfig -luuid -lzlib -licu -ldl $(shell pkg-config --static --libs x11 xrandr xi xxf86vm glfw3)
#    LDLIBS += -lEGL -lGLESv2 -lfreetype -lfontconfig -luuid -lz -lcurl -licu -ldl -lglfw #$(shell pkg-config --static --libs x11 xrandr xi xxf86vm glfw3)
#    LDLIBS += -lGL -lfreetype -lfontconfig -luuid -lz -lcurl -licu -ldl -lglfw
    ifeq ($(USE_GL),"1")
        LDLIBS += -lGL
    else
        LDLIBS += -lEGL -lGLESv2
    endif
    LDLIBS += -lfreetype -lfontconfig -luuid -lz -lcurl -licu -ldl -lglfw

else ifeq ($(PLATFORM),"Darwin")
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D OSX -stdlib=libc++ -arch x86_64 -fvisibility=hidden -mmacosx-version-min=11 -Wno-deprecated-declarations
    LDFLAGS += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices -mmacosx-version-min=10.15 -L/usr/local/lib
    LDLIBS += -lglfw -lzlib -lcurl -licu -ldl

else ifeq ($(PLATFORM),"Arm64")
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D OSX -stdlib=libc++ -arch arm64 -fvisibility=hidden -mmacosx-version-min=11 -Wno-deprecated-declarations
    LDFLAGS += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices -mmacosx-version-min=10.15 -L/usr/local/lib
    LDLIBS += -lglfw -lzlib -lcurl -licu -ldl

else ifeq ($(PLATFORM),"Windows")
    CXXFLAGS += -D WIN32
    CPPFLAGS += $(shell pkgconf -cflags skia) $(shell ncursesw6-config --cflags)
    LDLIBS += $(shell pkgconf -libs skia)
    LDLIBS += -lharfbuzz-subset -lglfw3 -lcurl
endif

OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))
OBJECTS += $(patsubst %.c, %.o, $(wildcard ./lib/libBigWig/*.c))

debug: CXXFLAGS += -g
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined

$(TARGET): $(OBJECTS)
	$(CXX) -g $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@


clean:
	-rm -f *.o ./src/*.o ./src/*.o.tmp ./lib/libBigWig/*.o
	-rm -f $(TARGET)
	-rm -rf libgw.*



ifeq ($(UNAME_S),Linux)
    SHARED_TARGET = libgw.so
endif
ifeq ($(UNAME_S),Darwin)
    SHARED_TARGET = libgw.dylib
endif

shared: CXXFLAGS += -fPIC -DBUILDING_LIBGW
shared: CFLAGS += -fPIC
shared: $(OBJECTS)

ifeq ($(UNAME_S),Darwin)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -dynamiclib -DBUILDING_LIBGW -o $(SHARED_TARGET)
else
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -shared -DBUILDING_LIBGW -o $(SHARED_TARGET)
endif
	-mkdir -p lib/libgw/include lib/libgw/out
	-cp $(SKIA_PATH)/libskia.a lib/libgw/out
	-cp src/*.h lib/libgw/include
	-cp include/*.h* lib/libgw/include
	-mv $(SHARED_TARGET) lib/libgw/out


