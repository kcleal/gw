// wasm_stubs.cpp — Weak-symbol GLFW stubs for the WebAssembly/Emscripten build.
//
// The emscripten-glfw contrib port (--use-port=contrib.glfw3) does not implement
// every GLFW function. These weak fallbacks satisfy wasm-ld for the missing ones.
// When the port provides a strong symbol the linker picks it; when it doesn't,
// the stub below is used instead.  On non-Emscripten targets this file is empty.

#ifdef __EMSCRIPTEN__
#include <GLFW/glfw3.h>
#include <emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/html5.h>
#include <string>
#include <iostream>

extern "C" {

// The browser drives its own event loop via requestAnimationFrame;
// waking it with an empty platform event is a no-op.
__attribute__((weak)) void glfwPostEmptyEvent(void) {}

// No windowing system to shut down in the browser.
__attribute__((weak)) void glfwTerminate(void) {}

// No primary monitor in the browser; return NULL.
__attribute__((weak)) GLFWmonitor* glfwGetPrimaryMonitor(void) { return nullptr; }

// Return a plausible 1920x1080 work area so callers can compute step sizes.
__attribute__((weak)) void glfwGetMonitorWorkarea(GLFWmonitor* /*mon*/,
                                                   int* xpos, int* ypos,
                                                   int* width, int* height) {
    if (xpos)   *xpos   = 0;
    if (ypos)   *ypos   = 0;
    if (width)  *width  = 1920;
    if (height) *height = 1080;
}

// Window position is not meaningful in the browser.
__attribute__((weak)) void glfwGetWindowPos(GLFWwindow* /*win*/,
                                             int* xpos, int* ypos) {
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}
__attribute__((weak)) void glfwSetWindowPos(GLFWwindow* /*win*/, int /*x*/, int /*y*/) {}

// Map window/framebuffer queries to the Emscripten canvas size.
__attribute__((weak)) void glfwGetWindowSize(GLFWwindow* /*win*/,
                                              int* width, int* height) {
    int w = 0, h = 0;
    emscripten_get_canvas_element_size("#canvas", &w, &h);
    if (width)  *width  = w;
    if (height) *height = h;
}
__attribute__((weak)) void glfwSetWindowSize(GLFWwindow* /*win*/, int w, int h) {
    emscripten_set_canvas_element_size("#canvas", w, h);
}
__attribute__((weak)) void glfwGetFramebufferSize(GLFWwindow* /*win*/,
                                                   int* width, int* height) {
    int w = 0, h = 0;
    emscripten_get_canvas_element_size("#canvas", &w, &h);
    if (width)  *width  = w;
    if (height) *height = h;
}

}  // extern "C"

// ── Remote genome mounting ─────────────────────────────────────────────────────
// gw_mount_remote_js: async JS function (via Asyncify/EM_ASYNC_JS) that mounts
// a remote HTTPS URL as a virtual Emscripten FS node with lazy Range-fetch reads.
// Fetches and fully caches the small .fai and .gzi index files up front so that
// fai_load() can proceed without additional async fetches for those.
// Returns 1 on success, 0 on failure.
EM_ASYNC_JS(int, gw_mount_remote_js, (const char* url, const char* local_path), {
    var urlStr      = UTF8ToString(url);
    var localStr    = UTF8ToString(local_path);
    var slash       = localStr.lastIndexOf('/');
    var dir         = slash >= 0 ? localStr.slice(0, slash) : '/';
    var name        = slash >= 0 ? localStr.slice(slash + 1) : localStr;

    // Already mounted? (stat succeeds)
    try { FS.stat(localStr); return 1; } catch(e) {}

    try {
        try { FS.mkdir(dir); } catch(e) {}

        // HEAD request to discover the file size without downloading the body.
        var headResp = await fetch(urlStr, {method: 'HEAD'});
        if (!headResp.ok) {
            console.error('[GW] HEAD failed for', urlStr, headResp.status);
            return 0;
        }
        var fileSize = parseInt(headResp.headers.get('Content-Length') || '0');
        if (fileSize <= 0) {
            console.error('[GW] Could not determine size for', urlStr);
            return 0;
        }

        // Create a zero-byte placeholder node that HTSlib can open.
        FS.createDataFile(dir, name, new Uint8Array(0), /*read*/true, /*write*/false, /*own*/false);
        var node = FS.lookupPath(localStr, {follow: true}).node;

        // Override getattr so stat() reports the real file size.
        var baseGetattr = node.node_ops.getattr;
        node.node_ops = Object.assign({}, node.node_ops, {
            getattr: function(n) {
                var a = baseGetattr.call(node.node_ops, n);
                a.size = fileSize;
                return a;
            }
        });

        // Override stream_ops with Asyncify-suspending Range reads.
        node.stream_ops = Object.assign({}, node.stream_ops, {
            read: function(stream, buf, offset, length, pos) {
                if (length === 0 || pos >= fileSize) return 0;
                return Module['Asyncify'].handleAsync(async function() {
                    var end = Math.min(pos + length, fileSize);
                    var r = await fetch(urlStr, {
                        headers: {'Range': 'bytes=' + pos + '-' + (end - 1)}
                    });
                    var ab  = await r.arrayBuffer();
                    var src = new Uint8Array(ab);
                    buf.set(src, offset);
                    return src.byteLength;
                });
            },
            llseek: function(stream, offset, whence) {
                var p = offset;
                if (whence === 1) p += stream.position;   // SEEK_CUR
                if (whence === 2) p += fileSize;           // SEEK_END
                if (p < 0) throw new FS.ErrnoError(28);
                return p;
            }
        });

        // Fetch the small .fai index completely (required by fai_load).
        try {
            var fr = await fetch(urlStr + '.fai');
            if (fr.ok) {
                FS.createDataFile(dir, name + '.fai',
                                  new Uint8Array(await fr.arrayBuffer()),
                                  true, false, false);
            }
        } catch(e) { console.warn('[GW] Could not fetch .fai:', e); }

        // Fetch the small .gzi index completely (required for bgzf-compressed FASTA).
        try {
            var gr = await fetch(urlStr + '.gzi');
            if (gr.ok) {
                FS.createDataFile(dir, name + '.gzi',
                                  new Uint8Array(await gr.arrayBuffer()),
                                  true, false, false);
            }
        } catch(e) { /* .gzi is optional */ }

        console.log('[GW] Mounted remote genome:', urlStr, '->', localStr,
                    '(' + (fileSize / 1048576).toFixed(1) + ' MB, lazy range-fetch)');
        return 1;

    } catch(e) {
        console.error('[GW] gw_mount_remote_js failed:', e);
        return 0;
    }
});

// resolve_genome_path: if path is an http/https URL, mount it as a virtual
// FS node and return the local /remote/<name> path.  Returns the original
// path unchanged for local files, or an empty string on mount failure.
std::string resolve_genome_path(const std::string& path) {
    if (path.size() < 8) return path;
    bool is_http  = path.substr(0, 7) == "http://";
    bool is_https = path.substr(0, 8) == "https://";
    if (!is_http && !is_https) return path;

    size_t slash = path.rfind('/');
    std::string fname = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    std::string local = "/remote/" + fname;

    int ok = gw_mount_remote_js(path.c_str(), local.c_str());
    if (!ok) {
        std::cerr << "Warning: failed to mount remote genome: " << path << std::endl;
        return "";
    }
    return local;
}

#endif  // __EMSCRIPTEN__
