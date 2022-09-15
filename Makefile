TARGET = gw


CXX = clang++
CXXFLAGS = -g -Wall -std=c++17  -fno-common -dynamic -fwrapv #-O3

INCLUDE = -I./inc -I./src -I. -I./gw -I/usr/local/include
LINK = -L $(wildcard ../skia/out/Rel*) -L/usr/local/lib

# Options to use target htslib or skia
HTSLIB ?= ""
ifneq ($(HTSLIB),"")
	INCLUDE += -I$(HTSLIB)
	LINK += -L$(HTSLIB)
endif

SKIA ?= ""
ifneq ($(SKIA),"")
	INCLUDE += -I$(SKIA)
	LINK += -L $(wildcard $(SKIA)/out/Rel*)
else
	INCLUDE += -I../skia
	LINK = -L $(wildcard ../skia/out/Rel*)
endif

LINK += -L /usr/local/lib

LIBS = -lskia -lm -ldl -licu -ljpeg -lpng -lsvg -lzlib -lhts -lfontconfig

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	# included if installed from package manager as required to compile
	ifeq (${XDG_SESSION_TYPE},"x11")
		LIBS += -lX11
	endif
	ifeq (${XDG_SESSION_TYPE},"wayland")
		LIBS += -lwayland-client
	endif
	# check if installed from package manager or manually compiled, assumed manually compiled if not package manager
	ifeq ($(ls /usr/lib/x86_64-linux-gnu/libglfw.so),"")
		LIBS += -lglfw3
	else
		LIBS += -lglfw3
	endif
endif
ifeq ($(UNAME_S),Darwin)
	LIBS += -lglfw3
endif

.PHONY: default all debug clean

default: $(TARGET)
#all: CXXFLAGS += -DNDEBUG -O3
all: default
debug: default

# windows untested here
IS_DARWIN=0
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -lglfw3 -D WIN32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CXXFLAGS += -D LINUX
        LIBS += -lGL -lfreetype -lfontconfig
    endif
    ifeq ($(UNAME_S),Darwin)
    	IS_DARWIN=1
    	# -mmacosx-version-min=10.15
        CXXFLAGS +=  -D OSX -stdlib=libc++ -arch x86_64 -fvisibility=hidden
    endif
endif

CXXFLAGS_link = $(CXXFLAGS)
ifeq ($(IS_DARWIN),1)
	CXXFLAGS_link += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices
endif


OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))


%.o: %.cpp
	$(CXX) $(CXXFLAGS) -g $(INCLUDE) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)


$(TARGET): $(OBJECTS)

	$(CXX) $(CXXFLAGS_link) -g $(OBJECTS) $(LINK)  $(LIBS) -o $@

clean:
	-rm -f *.o ./src/*.o ./src/*.o.tmp
	-rm -f $(TARGET)
