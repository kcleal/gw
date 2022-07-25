TARGET = gw
LIBS = -lm
CXX = g++
CXXFLAGS = -g -Wall -std=c++17

.PHONY: default all clean

default: $(TARGET)
all: default


HEADERS = $(wildcard ./inc/*.h)
OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp))
INCLUDE = -I./inc

%.o: %.cpp $(HEADERS)
	$(CXX) $(INCLUDE) $(CXXFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)