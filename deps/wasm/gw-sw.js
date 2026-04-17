// gw-sw.js — Service Worker for GW genome browser
//
// Purpose: Allow FS.createLazyFile() to make proper HTTP Range requests
// against large local files (BAM, CRAM, FASTA, etc.) without copying them
// into WASM memory.
//
// How it works:
//   1. The main page sends { type: 'register', files: [File, ...] } via
//      postMessage + MessageChannel.
//   2. This SW stores the File objects in a Map keyed by /gw-local/<name>.
//   3. When Emscripten's XHR makes a Range request to /gw-local/<name>,
//      the SW intercepts it, slices the File, and returns a 206 response.
//   4. No copy of the full file ever enters WASM memory.
//
// Scope: same directory as gw.html (registered as './gw-sw.js').

'use strict';

// Map of pathname → File (e.g. '/gw-local/sample.bam' → File object)
var fileStore = new Map();

// Activate immediately so the controlling page doesn't need a reload.
self.addEventListener('install', function() { self.skipWaiting(); });
self.addEventListener('activate', function(e) { e.waitUntil(self.clients.claim()); });

// ── File registration via MessageChannel ────────────────────────────────────
self.addEventListener('message', function(event) {
  var data = event.data;
  if (!data || data.type !== 'register') return;

  var port   = event.ports[0];
  var names  = [];

  (data.files || []).forEach(function(file) {
    var key = '/gw-local/' + file.name;
    fileStore.set(key, file);
    names.push(file.name);
    console.log('[GW-SW] registered ' + key + ' (' +
                (file.size / 1048576).toFixed(1) + ' MB)');
  });

  if (port) port.postMessage({ type: 'registered', names: names });
});

// ── Fetch interception ───────────────────────────────────────────────────────
self.addEventListener('fetch', function(event) {
  var url  = new URL(event.request.url);

  // Only handle /gw-local/* on the same origin.
  if (url.origin !== self.location.origin) return;
  if (!url.pathname.startsWith('/gw-local/')) return;

  var file = fileStore.get(url.pathname);
  if (!file) {
    // Not registered — return 404 so the caller gets a clear error.
    event.respondWith(new Response('GW-SW: file not registered: ' + url.pathname, {
      status: 404,
      headers: { 'Content-Type': 'text/plain' }
    }));
    return;
  }

  event.respondWith(handleFileRequest(event.request, file));
});

// Maximum bytes returned for a single Range response.
// Emscripten's createLazyFile probes with an open-ended "bytes=0-" request;
// if we returned the whole file the synchronous XHR would hang for large BAMs.
var MAX_CHUNK = 1024 * 1024;  // 1 MB

function handleFileRequest(request, file) {
  var rangeHeader = request.headers.get('Range');
  var isHead      = (request.method === 'HEAD');

  if (!rangeHeader) {
    // HEAD or full GET: return headers (including Content-Length) so callers
    // can determine the file size without downloading any data.
    return new Response(isHead ? null : file.slice(0, 0), {
      status: 200,
      headers: {
        'Content-Length':  String(file.size),
        'Content-Type':    'application/octet-stream',
        'Accept-Ranges':   'bytes'
      }
    });
  }

  // Parse "bytes=start-end" — end may be omitted (open-ended probe).
  var m = rangeHeader.match(/^bytes=(\d+)-(\d*)$/);
  if (!m) {
    return new Response('GW-SW: unsupported Range format: ' + rangeHeader, {
      status: 416,
      headers: { 'Content-Range': 'bytes */' + file.size }
    });
  }

  var start = parseInt(m[1], 10);
  // If end is omitted (open-ended probe), cap at MAX_CHUNK so we never load
  // the whole file into memory synchronously.
  var end = m[2] !== ''
    ? Math.min(parseInt(m[2], 10) + 1, file.size)
    : Math.min(start + MAX_CHUNK, file.size);

  if (start >= file.size || start < 0 || end <= start) {
    return new Response('GW-SW: range out of bounds', {
      status: 416,
      headers: { 'Content-Range': 'bytes */' + file.size }
    });
  }

  var slice = file.slice(start, end);
  return new Response(slice, {
    status: 206,
    headers: {
      'Content-Range':  'bytes ' + start + '-' + (end - 1) + '/' + file.size,
      'Content-Length': String(end - start),
      'Content-Type':   'application/octet-stream',
      'Accept-Ranges':  'bytes'
    }
  });
}
