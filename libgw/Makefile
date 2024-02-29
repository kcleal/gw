
CXXFLAGS += -std=c++17 -DOSX -stdlib=libc++ -arch arm64 -mmacosx-version-min=10.15
CPPFLAGS += -I./include
LDFLAGS += -L../ -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices
LDLIBS += -lgw
CXX=clang++


TARGET: hello

.PHONY: default clean

default: $(TARGET)

all: default

SOURCES=$(wildcard *.cpp)

hello: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -g $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	-rm -rf *.o ./*.o ./*.dSYM
	-rm -f hello