#!/usr/bin/env python3
"""Serve a web build with the headers required by Wasm threads."""

from __future__ import annotations

import argparse
import http.server
import os
from pathlib import Path
import socketserver
import ssl


class IsolatedHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--certfile", type=Path)
    parser.add_argument("--keyfile", type=Path)
    args = parser.parse_args()
    if bool(args.certfile) != bool(args.keyfile):
        parser.error("--certfile and --keyfile must be supplied together")
    os.chdir(args.root.resolve())
    with socketserver.ThreadingTCPServer((args.bind, args.port), IsolatedHandler) as server:
        server.daemon_threads = True
        scheme = "http"
        if args.certfile:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(args.certfile.resolve(), args.keyfile.resolve())
            server.socket = context.wrap_socket(server.socket, server_side=True)
            scheme = "https"
        print(f"Serving {args.root} at {scheme}://{args.bind}:{args.port}", flush=True)
        server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
