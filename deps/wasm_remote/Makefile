THIS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
GW_ROOT := $(abspath $(THIS_DIR)/../..)
OUT := $(THIS_DIR)/out
.PHONY: all assets clean
all:
	cd $(GW_ROOT) && env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS -u LDLIBS emmake $(MAKE) -j2 EMSCRIPTEN=1 REMOTE_UI=1 TARGET=$(OUT)/viewer.js WASM_BUILD_DIR=$(OUT)/objects "WASM_LDFLAGS_EXTRA=-g0 -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_remote_state,_remote_status,_remote_files -sEXPORTED_RUNTIME_METHODS=callMain,Asyncify,FS,stringToNewUTF8,UTF8ToString" "WASM_DEMO_INPUTS=$(THIS_DIR)/main.cpp $(GW_ROOT)/protocol/viewer/native/viewer_settings.h"
	$(MAKE) assets
assets:
	mkdir -p $(OUT)
	python3 -c 'from pathlib import Path; p=Path("$(THIS_DIR)/shell.html"); Path("$(OUT)/index.html").write_text(p.read_text().replace("{{{ SCRIPT }}}", "<script async src=\"viewer.js\"></script>"))'
	cp $(GW_ROOT)/protocol/viewer/v1/client.mjs $(OUT)/client.mjs
	cp $(THIS_DIR)/embed.mjs $(THIS_DIR)/embed.html $(OUT)/
clean:
	rm -rf $(OUT)
