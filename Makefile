TARGET = gw

CXX = clang++
CXXFLAGS = -g -Wall -std=c++17  -fno-common -dynamic -g -fwrapv
INCLUDE = -I./inc -I./src -I. -I../skia -I./gw
LINK = -L../skia/out/Release-x64 -L../htslib
LIBS = -lglfw -lskia -lm -ldl -lexpat -ldng_sdk -lharfbuzz -licu -ljpeg -lparticles -lpiex -lpng -lskottie \
        -lskparagraph -lskresources -lsksg -lskshaper -lsvg -lwebp -lwebp_sse41 -lzlib -lfreetype -lfontconfig \
        -lhts

.PHONY: default all debug clean

default: $(TARGET)
all: CXXFLAGS += -DNDEBUG -O3
all: default
debug: default

# windows untested here
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -D WIN32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CXXFLAGS += -D LINUX
        LIBS += -lGL
    endif
    ifeq ($(UNAME_S),Darwin)
        CXXFLAGS += -D OSX -stdlib=libc++ -mmacosx-version-min=10.15 -arch x86_64 -undefined dynamic_lookup
    endif
endif

OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))


%.o: %.cpp
	$(CXX) $(CXXFLAGS) -g $(INCLUDE) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -g $(OBJECTS) $(LINK)  $(LIBS) -o $@

clean:
	-rm -f *.o ./src/*.o ./src/*.o.tmp
	-rm -f $(TARGET)