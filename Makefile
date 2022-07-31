TARGET = gw

CXX = clang++
CXXFLAGS = -g -Wall -std=c++17 -stdlib=libc++ -mmacosx-version-min=10.15 -arch x86_64 -fno-common -dynamic -g -fwrapv
INCLUDE = -I./inc -I./src -I. -I../skia
LINK = -L../skia/out/Release-x64
LIBS = -lglfw -lskia -lm -ldl -lexpat -ldng_sdk -lharfbuzz -licu -ljpeg -lparticles -lpiex -lpng -lskottie \
        -lskparagraph -lskresources -lsksg -lskshaper -lsvg -lwebp -lwebp_sse41 -lzlib -lfreetype -lfontconfig

.PHONY: default all debug clean

default: $(TARGET)
all: CXXFLAGS += -DNDEBUG -O3
all: default
debug: default


OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))


%.o: %.cpp
	clang $(CXXFLAGS) -g $(INCLUDE) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	clang++ -undefined dynamic_lookup -arch x86_64 -g $(OBJECTS) $(LINK)  $(LIBS) -o $@

clean:
	-rm -f *.o ./src/*.o ./src/*.o.tmp
	-rm -f $(TARGET)