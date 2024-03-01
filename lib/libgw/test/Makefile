
CXXFLAGS += -std=c++17 -DOSX -stdlib=libc++ -arch arm64 -mmacosx-version-min=11
CPPFLAGS += -I../include -I../../skia/ -I../../libBigWig
LDFLAGS += -L../out -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices
LDLIBS += -lskia -lgw
CXX=clang++


TARGET: hello

.PHONY: default clean

default: $(TARGET)

all: default

SOURCES=$(wildcard *.cpp)

hello: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -g $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@
	-install_name_tool -change libgw.dylib @loader_path/../out/libgw.dylib hello

clean:
	-rm -rf *.o ./*.o ./*.dSYM
	-rm -f hello