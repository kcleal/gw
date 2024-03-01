
CXXFLAGS += -std=c++17 -DOSX -stdlib=libc++ -arch arm64 -mmacosx-version-min=11
CPPFLAGS += -I../ -I../include
LDFLAGS += -L../out -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices
LDLIBS += -lskia -lgwplot
CXX=clang++


TARGET: hello

.PHONY: default clean

default: $(TARGET)

all: default

SOURCES=$(wildcard *.cpp)

hello: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -g $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@
	-install_name_tool -change libgwplot.dylib @loader_path/../out/libgwplot.dylib hello

clean:
	-rm -rf *.o ./*.o ./*.dSYM
	-rm -f hello