TARGET = gw

CXXFLAGS += -Wall -std=c++17 -fno-common -fwrapv -O1 -DNDEBUG -pipe -fno-omit-frame-pointer#-fsanitize=address  -fsanitize=undefined

CPPFLAGS += -I./include -I./src -I.

LDLIBS += -lskia -lm -ljpeg -lpng -lsvg -lhts -lfontconfig -lpthread

#LDFLAGS=-fsanitize=address -fsanitize=undefined

# set system
PLATFORM=
ifeq ($(OS),Windows_NT)  # assume we are using msys2-ucrt64 env
    PLATFORM = "Windows"
else
    UNAME_S := $(shell uname -s)
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
ifneq ($(PLATFORM), "Windows")
    ifneq ($(SKIA),"")
        CPPFLAGS += -I$(SKIA)
        LDFLAGS += -L $(wildcard $(SKIA)/out/Rel*)
    else ifeq ($(PLATFORM),"Darwin")
        CPPFLAGS += -I./lib/skia
        LDFLAGS += -L./lib/skia/out/Release-x64
    else ifeq ($(PLATFORM),"Arm64")
        CPPFLAGS += -I./lib/skia
        LDFLAGS += -L./lib/skia/out/Release-arm64
    else
    	CPPFLAGS += -I./lib/skia
        LDFLAGS += -L./lib/skia/out/Release-x64
    endif
endif

SKIA_LINK=
ifeq ($(PLATFORM),"Linux")
    SKIA_LINK = https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip
endif
ifeq ($(PLATFORM),"Darwin")
    SKIA_LINK = https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-macos-Release-x64.zip
endif
ifeq ($(PLATFORM),"Arm64")
    SKIA_LINK = https://github.com/kcleal/skia_build_arm64/releases/download/v0.0.1/skia.zip
endif

# set platform flags and libs
ifeq ($(PLATFORM),"Linux")
    ifeq (${XDG_SESSION_TYPE},"wayland")  # wayland is untested!
        LDLIBS += -lwayland-client
    else
        LDLIBS += -lX11
    endif
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D LINUX -D __STDC_FORMAT_MACROS
    LDFLAGS += -L/usr/local/lib
    LDLIBS += -lGL -lfreetype -lfontconfig -luuid -lglfw -lzlib -licu -ldl

else ifeq ($(PLATFORM),"Darwin")
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D OSX -stdlib=libc++ -arch x86_64 -fvisibility=hidden -mmacosx-version-min=10.15 -Wno-deprecated-declarations
    LDFLAGS += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices -mmacosx-version-min=10.15 -L/usr/local/lib
    LDLIBS += -lglfw -lzlib -licu -ldl

else ifeq ($(PLATFORM),"Arm64")
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D OSX -stdlib=libc++ -arch arm64 -fvisibility=hidden -mmacosx-version-min=10.15 -Wno-deprecated-declarations
    LDFLAGS += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices -mmacosx-version-min=10.15 -L/usr/local/lib
    LDLIBS += -lglfw -lzlib -licu -ldl

else ifeq ($(PLATFORM),"Windows")
    CXXFLAGS += -D WIN32
    CPPFLAGS += $(shell pkgconf -cflags skia) $(shell ncursesw6-config --cflags)
    LDLIBS += $(shell pkgconf -libs skia)
    LDLIBS += -lharfbuzz-subset -lglfw3
endif

.PHONY: default all debug clean

default: $(TARGET)

all: default
debug: default

OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))

# download link for skia binaries, set for non-Windows platforms
prep:
    ifneq ($SKIA_LINK,"")
		$(info "Downloading pre-build skia skia from: $(SKIA_LINK)")
		cd lib/skia && wget -O skia.zip $(SKIA_LINK) && unzip -o skia.zip && rm skia.zip && cd ../../
    endif

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -g $(CPPFLAGS) -c $< -o $@

#.PRECIOUS: $(TARGET) $(OBJECTS)


$(TARGET): $(OBJECTS)
	$(CXX) -g $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	-rm -f *.o ./src/*.o ./src/*.o.tmp
	-rm -f $(TARGET)
