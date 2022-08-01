TARGET = gw


CXX = clang++
CXXFLAGS = -g -Wall -std=c++17  -fno-common -dynamic -g -fwrapv

INCLUDE = -I./inc -I./src -I. -I./gw
LINK = -L $(wildcard ../skia/out/Rel*)

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
	INCLUDE = -I./inc -I./src -I. -I../skia -I./gw
	LINK = -L $(wildcard ../skia/out/Rel*)
endif

LIBS = -lglfw -lskia -lm -ldl -licu -ljpeg -lpng -lsvg -lzlib -lfreetype -lfontconfig -lhts

.PHONY: default all debug clean

default: $(TARGET)
all: CXXFLAGS += -DNDEBUG -O3
all: default
debug: default

# windows untested here
IS_DARWIN=0
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -D WIN32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CXXFLAGS += -D LINUX
        LIBS += -lGL
    endif
    ifeq ($(UNAME_S),Darwin)
    	IS_DARWIN=1
        CXXFLAGS += -D OSX -stdlib=libc++ -mmacosx-version-min=10.15 -arch x86_64
    endif
endif

CXXFLAGS_link = $(CXXFLAGS)
ifeq ($(IS_DARWIN),1)
	CXXFLAGS_link += -undefined dynamic_lookup
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