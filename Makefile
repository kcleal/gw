TARGET = gw

CXXFLAGS += -Wall -std=c++17  -fno-common -dynamic -fwrapv -O3 -DNDEBUG # -g

CPPFLAGS += -I./include -I./src -I. -I/usr/local/include

LDLIBS += -lskia -lm -ldl -licu -ljpeg -lpng -lsvg -lzlib -lhts -lfontconfig -lpthread -lglfw

LDFLAGS += -L./lib/skia/out/Release-x64 -L/usr/local/lib

# try and add conda environment
ifneq ($(CONDA_PREFIX),"")
	CPPFLAGS += -I$(CONDA_PREFIX)/include
	LDFLAGS += -L$(CONDA_PREFIX)/lib
endif

# Options to use target htslib or skia
HTSLIB ?= ""
ifneq ($(HTSLIB),"")
	CPPFLAGS += -I$(HTSLIB)
	LDFLAGS += -L$(HTSLIB)
endif

SKIA ?= ""
ifneq ($(SKIA),"")
	CPPFLAGS += -I$(SKIA)
	LDFLAGS += -L $(wildcard $(SKIA)/out/Rel*)
else
	CPPFLAGS += -I./lib/skia
	LDFLAGS += -L./lib/skia/out/Release-x64
endif


UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	ifeq (${XDG_SESSION_TYPE},"wayland")
		LDLIBS += -lwayland-client
	else
		LDLIBS += -lX11
	endif
endif

.PHONY: default all debug clean

default: $(TARGET)

all: default
debug: default

# windows untested here
IS_DARWIN=0
SKIA_LINK=
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -lglfw3 -D WIN32
    SKIA_LINK = https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-windows-Release-x64.zip
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CXXFLAGS += -D LINUX -D __STDC_FORMAT_MACROS
        LDLIBS += -lGL -lfreetype -lfontconfig -luuid
        SKIA_LINK = https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip
    endif
    ifeq ($(UNAME_S),Darwin)
    	IS_DARWIN = 1
        CXXFLAGS += -D OSX -stdlib=libc++ -arch x86_64 -fvisibility=hidden -mmacosx-version-min=10.15 -Wno-deprecated-declarations
        SKIA_LINK = https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-macos-Release-x64.zip
    endif
    ifeq ($(UNAME_S),arm64)
		IS_DARWIN = 1
		CXXFLAGS += -D OSX -stdlib=libc++ -arch arm64 -fvisibility=hidden -mmacosx-version-min=10.15
		SKIA_LINK = https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-macos-Release-arm64.zip
    endif
endif


ifeq ($(IS_DARWIN),1)
	LDFLAGS += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices -mmacosx-version-min=10.15
endif


OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))

prep:
	$(info "System: $(shell uname -s)")
	$(info "Downloading pre-build skia skia from: $(SKIA_LINK)")
	cd lib/skia && wget -O skia.zip $(SKIA_LINK) && unzip -o skia.zip && rm skia.zip && cd ../../


%.o: %.cpp
	$(CXX) $(CXXFLAGS) -g $(CPPFLAGS) -c $< -o $@

#.PRECIOUS: $(TARGET) $(OBJECTS)


$(TARGET): $(OBJECTS)
	$(CXX) -g $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	-rm -f *.o ./src/*.o ./src/*.o.tmp
	-rm -f $(TARGET)
