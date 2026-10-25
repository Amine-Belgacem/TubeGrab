"""Offline yt-dlp retry/partial-resume validation. Uses only Python's standard library."""
import hashlib
import http.server
import json
from pathlib import Path
import queue
import socket
import subprocess
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build" / "recovery-validation" / uuid.uuid4().hex
WORK.mkdir(parents=True)
PAYLOAD = (ROOT / "build" / "media-validation" / "source.mp4").read_bytes() + bytes(8 * 1024 * 1024)
AUDIT = {"transient_failures": 0, "connection_drops": 0, "range_offsets": [], "drop_range_offsets": [], "ignored_range_offsets": []}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path not in ("/sample.mp4", "/drop.mp4", "/ignore-range.mp4"):
            self.send_error(404)
            return
        if self.path == "/sample.mp4" and AUDIT["transient_failures"] < 2:
            AUDIT["transient_failures"] += 1
            self.send_error(503, "Intentional temporary fixture failure")
            return
        header = self.headers.get("Range", "")
        offset = int(header.split("=")[1].split("-")[0]) if header.startswith("bytes=") else 0
        if offset >= len(PAYLOAD):
            self.send_error(416)
            return
        if offset:
            key = "range_offsets" if self.path == "/sample.mp4" else "drop_range_offsets" if self.path == "/drop.mp4" else "ignored_range_offsets"
            AUDIT[key].append(offset)
        if self.path == "/ignore-range.mp4":
            offset = 0  # A valid HTTP server is allowed to ignore Range and return 200.
        drop = self.path == "/drop.mp4" and AUDIT["connection_drops"] == 0
        if drop:
            AUDIT["connection_drops"] += 1
        self.send_response(206 if offset else 200)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Content-Length", str(len(PAYLOAD) - offset))
        if offset:
            self.send_header("Content-Range", f"bytes {offset}-{len(PAYLOAD) - 1}/{len(PAYLOAD)}")
        self.end_headers()
        try:
            for start in range(offset, len(PAYLOAD), 32768):
                self.wfile.write(PAYLOAD[start:start + 32768])
                self.wfile.flush()
                if drop and start - offset >= 131072:
                    self.connection.shutdown(socket.SHUT_RDWR)
                    self.connection.close()
                    return
                time.sleep(0.012)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


def stop_tree(process):
    if process.poll() is None:
        # Only the test's own freshly launched engine process and its descendants.
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        process.wait(timeout=10)


def transfer(info, output_dir, pause=False):
    args = [str(ROOT / "tools" / "yt-dlp.exe"), "--ignore-config", "--no-plugin-dirs",
            "--no-js-runtimes", "--no-colors", "--newline", "--progress", "--progress-delta", "0.1",
            "--continue", "--part", "--no-overwrites", "--retries", "3", "--retry-sleep", "0.1",
            "--socket-timeout", "5", "--no-simulate", "--load-info-json", str(info),
            "--progress-template", "download:TGTEST:%(progress.downloaded_bytes|0)j",
            "--print", "after_move:TGFILE:%(filepath)j", "--paths", str(output_dir),
            "--output", "recovery.%(ext)s"]
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               encoding="utf-8", errors="replace", creationflags=subprocess.CREATE_NO_WINDOW)
    lines, messages = [], queue.Queue()

    def read():
        for line in process.stdout:
            messages.put(line.rstrip())
        messages.put(None)

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    deadline = time.monotonic() + 60
    try:
        while True:
            if time.monotonic() >= deadline:
                raise RuntimeError("Timed out waiting for the local download fixture")
            try:
                line = messages.get(timeout=0.5)
            except queue.Empty:
                continue
            if line is None:
                break
            lines.append(line)
            if pause and line.startswith("TGTEST:") and json.loads(line[7:]) >= 65536:
                stop_tree(process)
                reader.join(timeout=5)
                return lines, process.returncode
        return lines, process.wait(timeout=5)
    finally:
        stop_tree(process)


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
server.daemon_threads = True
threading.Thread(target=server.serve_forever, daemon=True).start()
def make_fixture(name, route):
    folder = WORK / name
    folder.mkdir()
    info = folder / "fixture.info.json"
    fixture = {"id": "abcdefghijk", "title": "Recovery validation", "extractor": "generic",
               "extractor_key": "Generic", "webpage_url": "https://example.invalid/local-fixture",
               "formats": [{"format_id": "http", "url": f"http://127.0.0.1:{server.server_port}/{route}",
                            "ext": "mp4", "protocol": "http", "filesize": len(PAYLOAD)}]}
    info.write_text(json.dumps(fixture), encoding="utf-8")
    return folder, info


def verify_output(folder, code):
    output = folder / "recovery.mp4"
    assert code == 0 and output.is_file(), f"Transfer failed: {folder}"
    assert hashlib.sha256(output.read_bytes()).hexdigest() == expected, f"Output bytes differ from the source: {folder}"
    return output.stat().st_size


expected = hashlib.sha256(PAYLOAD).hexdigest()
try:
    folder, info = make_fixture("interruption", "sample.mp4")
    first, code = transfer(info, folder, pause=True)
    partial = folder / "recovery.mp4.part"
    assert code != 0 and partial.is_file(), "Interrupted transfer did not preserve its partial file"
    partial_bytes = partial.stat().st_size
    assert 0 < partial_bytes < len(PAYLOAD), "Fixture was not interrupted during transfer"
    second, code = transfer(info, folder)
    output_bytes = verify_output(folder, code)
    assert any(offset >= partial_bytes for offset in AUDIT["range_offsets"]), "Resume did not request a nonzero byte range"
    assert AUDIT["transient_failures"] == 2, "Transient HTTP retries were not exercised"
    (folder / "engine.log").write_text("\n".join(first + second), encoding="utf-8")

    folder, info = make_fixture("connection-drop", "drop.mp4")
    lines, code = transfer(info, folder)
    (folder / "engine.log").write_text("\n".join(lines), encoding="utf-8")
    verify_output(folder, code)
    assert AUDIT["connection_drops"] == 1 and AUDIT["drop_range_offsets"], "The connection drop did not trigger byte-range recovery"

    folder, info = make_fixture("range-ignored", "ignore-range.mp4")
    first, code = transfer(info, folder, pause=True)
    assert code != 0 and (folder / "recovery.mp4.part").is_file(), "Range-ignored fixture did not preserve partial data"
    second, code = transfer(info, folder)
    (folder / "engine.log").write_text("\n".join(first + second), encoding="utf-8")
    verify_output(folder, code)
    assert AUDIT["ignored_range_offsets"], "The server did not receive a resumable request to ignore"
    report = {**AUDIT, "partial_bytes": partial_bytes, "output_bytes": output_bytes, "sha256": expected}
    (WORK / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("PASS: real yt-dlp recovered from two HTTP 503 errors, forced process termination, a dropped connection, and a server ignoring Range. All three final files match the source SHA-256.")
    print(f"Evidence: {WORK}")
finally:
    server.shutdown()
    server.server_close()
