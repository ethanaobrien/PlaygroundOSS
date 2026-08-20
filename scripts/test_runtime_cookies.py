#!/usr/bin/env python3
"""Exercise shared libcurl cookies, persistence, reload, and clearing."""

from __future__ import annotations

import argparse
import http.server
import shutil
import subprocess
import threading
from pathlib import Path


class CookieHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
        if self.path == "/set":
            body = b"set"
            self.send_response(200)
            self.send_header(
                "Set-Cookie", "playground_session=durable; Path=/; HttpOnly"
            )
        elif self.path == "/check":
            cookie = self.headers.get("Cookie", "")
            body = (
                b"present"
                if "playground_session=durable" in cookie
                else b"absent"
            )
            self.send_response(200)
        else:
            body = b"missing"
            self.send_response(404)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format: str, *_args: object) -> None:
        return


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=Path)
    parser.add_argument("work", type=Path)
    args = parser.parse_args()
    shutil.rmtree(args.work, ignore_errors=True)
    args.work.mkdir(parents=True, exist_ok=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), CookieHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = f"http://127.0.0.1:{server.server_address[1]}"
        subprocess.run([str(args.probe), str(args.work), url], check=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
