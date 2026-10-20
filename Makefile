ifdef EMSCRIPTEN
    TARGET = gw.html
else
    TARGET = gw
endif
VERSION = "2.0.0"
.PHONY: default all debug clean
default: $(TARGET)
all: default
debug: default
# System info ----------------------------------------------
PLATFORM=
TARGET_OS=
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(OS),Windows_NT)  # assume we are using msys2-ucrt64 env
    PLATFORM = "Windows"
    TARGET_OS = "Windows"
else
    ifdef EMSCRIPTEN
        PLATFORM = "Emscripten"
        TARGET_OS = "Wasm"
    else ifeq ($(UNAME_S),Linux)
        TARGET_OS = "Linux"
        ifeq ($(UNAME_M),aarch64)
            PLATFORM = "Linux-aarch64"
        else
            PLATFORM = "Linux-x64"
        endif
    else ifeq ($(UNAME_S),Darwin)
        TARGET_OS = "MacOS"
        ifeq ($(shell uname -m), arm64)
            PLATFORM = "MacOS-arm64"
        else
            PLATFORM = "MacOS-x64"
        endif
    endif
endif
$(info   PLATFORM=$(PLATFORM))
ifneq ($(PLATFORM),"Emscripten")
    ifdef CONDA_PREFIX
        $(info   Linking conda from $(CONDA_PREFIX))
        CPPFLAGS += -I$(CONDA_PREFIX)/include
        LDFLAGS += -L$(CONDA_PREFIX)/lib
        LDFLAGS += -Wl,-rpath,$(CONDA_PREFIX)/lib
    endif
endif
# ncurses info --------------------------------------------
NCURSES_CFLAGS := $(shell pkg-config --cflags ncurses 2>/dev/null || echo "")
NCURSES_LIBS := $(shell pkg-config --libs ncurses 2>/dev/null || echo "-lncurses")
# Skia info ---------------------------------------------
OLD_SKIA ?= 0
SKIA_VERSION ?= m143
prep:
ifeq ($(OLD_SKIA),0)
	@if [ "$(PLATFORM)" = "MacOS-x64" ]; then \
		echo "Downloading pre-built skia-$(SKIA_VERSION) for MacOS-Intel"; mkdir -p lib/skia; \
		cd lib/skia && curl -fL -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/$(SKIA_VERSION)/skia-$(SKIA_VERSION)-macos-Release-x64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "MacOS-arm64" ]; then \
		echo "Downloading pre-built skia-$(SKIA_VERSION) for MacOS-Arm64"; mkdir -p lib/skia; \
		cd lib/skia && curl -fL -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/$(SKIA_VERSION)/skia-$(SKIA_VERSION)-macos-Release-arm64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux-x64" ]; then \
		echo "Downloading pre-built skia-$(SKIA_VERSION) for Linux-x64"; mkdir -p lib/skia; \
		cd lib/skia && curl -fL -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/$(SKIA_VERSION)/skia-$(SKIA_VERSION)-linux-Release-x64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux-aarch64" ]; then \
		echo "Downloading pre-built skia-$(SKIA_VERSION) for Linux-aarch64"; mkdir -p lib/skia; \
		cd lib/skia && curl -fL -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/$(SKIA_VERSION)/skia-$(SKIA_VERSION)-linux-Release-arm64.tar.gz" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	fi
else
	@if [ "$(PLATFORM)" = "MacOS-x64" ]; then \
		echo "Downloading pre-built skia-m93 for MacOS-Intel"; mkdir -p lib/skia; \
		cd lib/skia && curl -fL -o skia.zip "https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-macos-Release-x64.zip" && unzip -o skia.zip && rm skia.zip && cd ../../; \
	elif [ "$(PLATFORM)" = "MacOS-arm64" ]; then \
		echo "Downloading pre-built skia-m93 for MacOS-Arm64"; mkdir -p lib/skia; \
		cd lib/skia && curl -fL -o skia.tar.gz "https://github.com/kcleal/skia_build_arm64/releases/download/v0.0.1/skia.zip" && tar -xvf skia.tar.gz && rm skia.tar.gz && cd ../../; \
	elif [ "$(PLATFORM)" = "Linux-x64" ]; then \
		echo "Downloading pre-built skia-m93 for Linux-x64"; mkdir -p lib/skia; \
		cd lib/skia && curl -fL -o skia.zip "https://github.com/JetBrains/skia-build/releases/download/m93-87e8842e8c/Skia-m93-87e8842e8c-linux-Release-x64.zip" && unzip -o skia.zip && rm skia.zip && cd ../../; \
	fi
endif
SKIA_PATH := $(shell if [ -d ./lib/skia/out ]; then find ./lib/skia/out -type d -name '*Release*' | sort | head -n 1; fi)
# Flags and libs -----------------------------------------
ifeq ($(OLD_SKIA),1)
	CXXFLAGS += -D OLD_SKIA -D USE_GL
endif
CXXFLAGS += -Wall -std=c++17 -fno-common -fwrapv -fno-omit-frame-pointer -O3 -DNDEBUG -g
# Emit .d files so header changes rebuild dependent objects (see -include below)
CXXFLAGS += -MMD -MP
CFLAGS += -MMD -MP
LIBGW_INCLUDE=
IMGUI = ./lib/dear_imgui
CPPFLAGS += -I./lib/skia -I./lib/libBigWig -I$(IMGUI) -I$(IMGUI)/backends -I./include -I. $(LIBGW_INCLUDE) -I./src
LDFLAGS += $(if $(SKIA_PATH),-L$(SKIA_PATH))
SKIA_LIBS := -lskia -lm -ljpeg -lpng -lpthread
ifeq ($(TARGET_OS),"Linux")  # set platform flags and libs
    CXXFLAGS += -D LINUX -D __STDC_FORMAT_MACROS
    LDLIBS += $(SKIA_LIBS) -lX11
    ifeq ($(USE_WAYLAND),1)
        LDLIBS += -lwayland-client -lwayland-egl
    endif
    LGLFW := $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")
    CPPFLAGS += $(NCURSES_CFLAGS)
    LDLIBS += -lGL -lEGL -lGLESv2 -lhts -lfreetype -luuid -lz -lcurl -licu -ldl $(LGLFW) -lsvg -lfontconfig $(NCURSES_LIBS)
else ifeq ($(TARGET_OS),"MacOS")
    ifeq ($(PLATFORM),"MacOS-x64")
        CXXFLAGS += -arch x86_64
    else
        CXXFLAGS += -arch arm64
    endif
    CPPFLAGS += -I/usr/local/include
    CXXFLAGS += -D OSX -stdlib=libc++ -fvisibility=hidden -mmacosx-version-min=11 -Wno-deprecated-declarations
    LDFLAGS += -undefined dynamic_lookup -framework OpenGL -framework AppKit -framework ApplicationServices -mmacosx-version-min=11 -L/usr/local/lib
    LGLFW := $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")
    CPPFLAGS += $(NCURSES_CFLAGS)
    LDLIBS += $(SKIA_LIBS) -lhts $(LGLFW) -lzlib -lcurl -licu -ldl -lsvg -lfontconfig $(NCURSES_LIBS)
else ifeq ($(PLATFORM),"Windows")  # Targets an msys2 build environment
    CXXFLAGS += -D WIN32
    SKIA_CFLAGS := $(shell pkg-config --cflags skia 2>/dev/null || echo "$(if $(MINGW_PREFIX),-I$(MINGW_PREFIX)/include/skia)")
    SKIA_LIBS := $(shell pkg-config --libs skia 2>/dev/null || echo "-lskia")
    NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null || echo "")
    NCURSES_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo "-lncursesw")
    CPPFLAGS += $(SKIA_CFLAGS) $(NCURSES_CFLAGS)
    LDLIBS += $(SKIA_LIBS) -lhts -lharfbuzz-subset -lglfw3 -lcurl $(NCURSES_LIBS)
endif
ifdef EMSCRIPTEN
include deps/wasm/flags.mk
endif
debug: CXXFLAGS += -O0 -DDEBUG
debug: CFLAGS += -O0 -DDEBUG
# Compile --------------------------------------------------
OBJECTS = $(patsubst %.cpp, %.o, $(wildcard ./src/*.cpp ./src/online/*.cpp) ./src/menu_components/online_genome_utils.cpp ./src/menu_components/startup_genome_dialog.cpp) $(patsubst %.c, %.o, $(wildcard ./lib/libBigWig/*.c))
OBJECTS += $(patsubst %.c, %.o, $(if $(EMSCRIPTEN),$(filter-out ./include/glad.c, $(wildcard ./include/*.c)),$(wildcard ./include/*.c)))
OBJECTS += $(patsubst %.cpp, %.o, $(wildcard $(IMGUI)/*.cpp) $(IMGUI)/backends/imgui_impl_glfw.cpp $(IMGUI)/backends/imgui_impl_opengl3.cpp)
ifdef REMOTE_UI
OBJECTS := $(filter-out ./src/main.o,$(OBJECTS)) ./deps/wasm_remote/main.o
endif
ifdef EMSCRIPTEN
# Keep browser objects separate so native and WASM builds can coexist.
WASM_BUILD_DIR ?= ./deps/wasm/out/gw_objects
OBJECTS := $(patsubst ./%,$(WASM_BUILD_DIR)/%,$(OBJECTS))
$(OBJECTS): deps/wasm/flags.mk
$(TARGET): $(WASM_DEMO_INPUTS)
$(WASM_BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(WASM_BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif
SERVER_OBJECTS := ./protocol/viewer/native/viewer_interface.o
-include $(OBJECTS:.o=.d) $(SERVER_OBJECTS:.o=.d)
./lib/libBigWig/%.o: ./lib/libBigWig/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-attribute-warning -c $< -o $@
$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@


clean:
	-rm -f $(SERVER_OBJECTS) $(SERVER_OBJECTS:.o=.d)
	-rm -f *.o ./src/*.o ./src/*.o.tmp ./src/online/*.o ./lib/libBigWig/*.o ./include/*.o
	-rm -f ./lib/dear_imgui/*.o ./lib/dear_imgui/backends/*.o
	-rm -f *.d ./src/*.d ./src/online/*.d ./src/menu_components/*.d ./lib/libBigWig/*.d ./include/*.d
	-rm -f ./lib/dear_imgui/*.d ./lib/dear_imgui/backends/*.d
	-rm -f $(TARGET) *.wasm
	-rm -rf libgw
	-rm -f libgw_server_core.a


ifeq ($(UNAME_S),Linux)
    SHARED_TARGET = libgw.so
    FORCELOAD_SKIA = -Wl,--whole-archive $(SKIA_PATH)/libskia.a -Wl,--no-whole-archive
endif
ifeq ($(UNAME_S),Darwin)
    SHARED_TARGET = libgw.dylib
    FORCELOAD_SKIA = -Wl,-force_load,$(SKIA_PATH)/libskia.a
endif

shared: CXXFLAGS += -fPIC -DBUILDING_LIBGW
shared: CFLAGS += -fPIC
shared: $(OBJECTS)

ifeq ($(PLATFORM),"Windows")
	$(error Shared library build is not supported on Windows)
endif

# Create shared library-specific LDLIBS with force-loaded Skia (Linux/macOS only).
ifdef FORCELOAD_SKIA
	$(eval SHARED_LDLIBS := $(FORCELOAD_SKIA) $(filter-out -lskia -ljpeg -lpng -lzlib,$(LDLIBS)))
else
	$(eval SHARED_LDLIBS := $(LDLIBS))
endif

ifeq ($(UNAME_S),Darwin)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(SHARED_LDLIBS) -dynamiclib -DBUILDING_LIBGW -install_name @rpath/libgw.dylib -current_version $(VERSION) -compatibility_version $(VERSION) -o $(SHARED_TARGET)
else
	$(CXX) $(OBJECTS) $(LDFLAGS) $(SHARED_LDLIBS) -shared -DBUILDING_LIBGW -Wl,-soname,libgw.so -Wl,-rpath,\$$ORIGIN -o $(SHARED_TARGET)
endif
	-mkdir -p libgw libgw/GW
	-cp $(SKIA_PATH)/libskia.a libgw
	-cp src/*.h src/*.hpp libgw/GW
	-mkdir -p libgw/GW/online
	-cp src/online/*.h libgw/GW/online
	-cp include/*.h* libgw/GW
	-cp lib/libBigWig/*.h libgw/GW
	-cp -rf lib/skia/include libgw/GW
	-cp -rf lib/skia/modules libgw/GW
	-mv $(SHARED_TARGET) libgw

# Regression checks use temporary fixtures and do not open native windows.
.PHONY: test-startup test-ui
test-startup: $(TARGET)
	python3 test/test_startup_validation.py

test-ui: $(filter-out ./src/main.o,$(OBJECTS))
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG test/test_ui_interactions.cpp $(filter-out ./src/main.o,$(OBJECTS)) $(LDFLAGS) $(LDLIBS) -o /tmp/gw-ui-interactions
	/tmp/gw-ui-interactions

# Optional native server. Neither oat++ nor server code is part of the default target.
GW_SERVER_DIR ?= ../gw-server
GW_SERVER_BUILD_DIR ?= $(GW_SERVER_DIR)/build
OATPP_ROOT ?=
OATPP_SOURCE_DIR ?=
.PHONY: server-core server
server-core: libgw_server_core.a
libgw_server_core.a: $(filter-out ./src/main.o,$(OBJECTS)) $(SERVER_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^
server: server-core
	env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS -u LDLIBS cmake -S "$(GW_SERVER_DIR)" -B "$(GW_SERVER_BUILD_DIR)" -DGW_ROOT="$(CURDIR)" -DGW_CORE_LIBRARY="$(CURDIR)/libgw_server_core.a" "-DGW_NATIVE_LINK_FLAGS=$(subst -L./,-L$(CURDIR)/,$(LDFLAGS)) $(LDLIBS)" "-DCMAKE_PREFIX_PATH=$(OATPP_ROOT)" "-DOATPP_SOURCE_DIR=$(OATPP_SOURCE_DIR)"
	cmake --build "$(GW_SERVER_BUILD_DIR)" -j2
