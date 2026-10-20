#!/usr/bin/env python3
"""Serve the local demo without caching old JavaScript/WASM after rebuilds."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from socketserver import TCPServer


class DemoHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?", 1)[0] == "/":
            self.path = "/embed.html"
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


class StaticServer(ThreadingHTTPServer):
    def server_bind(self):
        # Avoid a reverse-DNS lookup delaying localhost startup on some hosts.
        TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    handler = partial(DemoHandler, directory=str(Path(__file__).resolve().parent))
    with StaticServer(("127.0.0.1", args.port), handler) as server:
        print(f"GW demo: http://127.0.0.1:{args.port}/gw.html", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
