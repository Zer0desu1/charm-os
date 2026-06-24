#!/usr/bin/env python3
# ============================================================================
# build_server.py - On-demand custom-firmware build server for the Vecta app.
#
#   The phone can't compile firmware, and ESP32 flashes a whole image at once,
#   so "install exactly these modules" = build a .bin with just that set, then
#   OTA it. This tiny HTTP server (run on the PC that has PlatformIO) does the
#   build on request and streams the .bin back:
#
#       GET /build?mods=camera,game,clock   -> firmware.bin (base + those mods)
#       GET /mods                           -> JSON list of valid module ids
#       GET /ping                           -> "ok" (app uses this to find it)
#
#   The app downloads the .bin from here, then OTA-flashes it to the watch.
#   Both phone and PC must be on the same Wi-Fi. Builds are cached by module
#   set, so re-installing the same combo is instant.
#
#   Run:   python Vecta/tools/build_server.py            (port 8723)
#          python Vecta/tools/build_server.py 9000        (custom port)
#   Then in the app's "Özel kurulum" enter  <this-PC-IP>:8723
# ============================================================================
import http.server
import socketserver
import subprocess
import urllib.parse
import os
import sys
import json
import socket
import hashlib
import shutil
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
VECTA_DIR = os.path.abspath(os.path.join(HERE, ".."))
ENV = "esp32-s3-devkitc-1"
BUILD_BIN = r"C:\Users\Gaming\.vecta_build\{}\firmware.bin".format(ENV)
CACHE_DIR = os.path.join(HERE, ".build_cache")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8723

# Locate pio (same logic as build_bundles.sh: often not on PATH).
def find_pio():
    for c in [
        os.path.expanduser(r"~\.platformio\penv\Scripts\pio.exe"),
        r"C:\Users\Gaming\.platformio\penv\Scripts\pio.exe",
        r"C:\Users\Gaming\AppData\Local\Programs\Python\Python311\Scripts\pio.exe",
        shutil.which("pio") or "",
    ]:
        if c and os.path.isfile(c):
            return c
    return "pio"  # last resort: hope it's on PATH

PIO = find_pio()

# Valid module ids = the MOD_<ID> flags declared in mod_config.h, lowercased.
def load_valid_mods():
    ids = set()
    try:
        with open(os.path.join(VECTA_DIR, "mod_config.h"), "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                # match: #ifndef MOD_XXX  (one per optional module)
                if line.startswith("#ifndef MOD_"):
                    flag = line.split("MOD_", 1)[1].strip()
                    if flag and flag != "DEFAULT":
                        ids.add(flag.lower())
    except OSError:
        pass
    return ids

VALID = load_valid_mods()

def lan_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"

# Build jobs run on a background thread so HTTP requests return immediately and
# the phone never holds a 60s connection open (which network stacks time out).
# State per module-set: {"state": building|ready|error, "path", "err"}.
_jobs = {}
_jobs_lock = threading.Lock()
_build_lock = threading.Lock()  # serialize actual pio runs (shared build dir)

def _key_of(mods):
    return ",".join(sorted(mods)) or "base"

def _bin_path(key):
    h = hashlib.sha1(key.encode()).hexdigest()[:12]
    return os.path.join(CACHE_DIR, "charm-{}.bin".format(h))

def _run_build(key, mods):
    out = _bin_path(key)
    flags = ["-DCHARM_BASE=1"] + ["-DMOD_{}=1".format(m.upper()) for m in mods]
    env = dict(os.environ)
    env["PLATFORMIO_BUILD_FLAGS"] = " ".join(flags)
    print("  building:", key)
    print("  flags:", env["PLATFORMIO_BUILD_FLAGS"])
    try:
        with _build_lock:
            r = subprocess.run([PIO, "run", "-d", VECTA_DIR, "-e", ENV],
                               env=env, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.isfile(BUILD_BIN):
                tail = (r.stdout or "")[-1200:] + (r.stderr or "")[-1200:]
                raise RuntimeError("build failed:\n" + tail)
            os.makedirs(CACHE_DIR, exist_ok=True)
            shutil.copyfile(BUILD_BIN, out)
        sz = os.path.getsize(out)
        print("  done:", os.path.basename(out), sz, "bytes")
        with _jobs_lock:
            _jobs[key] = {"state": "ready", "path": out, "err": None}
    except Exception as e:
        print("  ERROR:", e)
        with _jobs_lock:
            _jobs[key] = {"state": "error", "path": None, "err": str(e)}

def request_build(mods):
    """Return current status dict; start a build if needed. Non-blocking."""
    key = _key_of(mods)
    out = _bin_path(key)
    with _jobs_lock:
        if os.path.isfile(out):
            _jobs[key] = {"state": "ready", "path": out, "err": None}
            return key, _jobs[key]
        job = _jobs.get(key)
        if job and job["state"] in ("building", "ready"):
            return key, job
        # (re)start: previous error or never built
        _jobs[key] = {"state": "building", "path": None, "err": None}
    threading.Thread(target=_run_build, args=(key, mods), daemon=True).start()
    return key, {"state": "building", "path": None, "err": None}

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass  # quiet default logging; we print our own

    def _send(self, code, body, ctype="text/plain"):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        if u.path == "/ping":
            return self._send(200, "ok")
        if u.path == "/mods":
            return self._send(200, json.dumps(sorted(VALID)), "application/json")

        if u.path in ("/status", "/build"):
            raw = (q.get("mods", [""])[0]).strip()
            mods = [m.strip().lower() for m in raw.split(",") if m.strip()]
            bad = [m for m in mods if m not in VALID]
            if bad:
                return self._send(400, "unknown modules: " + ", ".join(bad))
            key, job = request_build(mods)

            # /status: non-blocking - tell the app to keep polling or proceed.
            if u.path == "/status":
                body = {"state": job["state"], "key": key}
                if job["state"] == "ready" and job.get("path"):
                    try:
                        body["size"] = os.path.getsize(job["path"])
                    except OSError:
                        pass
                if job["state"] == "error":
                    body["err"] = job.get("err")
                code = 200 if job["state"] in ("ready", "building") else 500
                return self._send(code, json.dumps(body), "application/json")

            # /build: only serves when ready (app polls /status first).
            if job["state"] != "ready" or not job.get("path"):
                return self._send(409, "not ready: " + job["state"])
            with open(job["path"], "rb") as f:
                data = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(data)
            print("  served:", len(data), "bytes for [", ",".join(mods), "]")
            return
        self._send(404, "not found")

def main():
    if not VALID:
        print("WARNING: no module ids parsed from mod_config.h - /build will reject all.")
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    socketserver.ThreadingTCPServer.daemon_threads = True
    with socketserver.ThreadingTCPServer(("0.0.0.0", PORT), Handler) as httpd:
        print("=" * 60)
        print("Vecta build server running.")
        print("  Phone -> enter this in the app's 'Ozel kurulum':")
        print("      {}:{}".format(lan_ip(), PORT))
        print("  pio:", PIO)
        print("  modules:", len(VALID), "available")
        print("  (Ctrl+C to stop)")
        print("=" * 60)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nstopped.")

if __name__ == "__main__":
    main()
