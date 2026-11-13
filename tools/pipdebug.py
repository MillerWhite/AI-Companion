#!/usr/bin/env python3
"""Pip's debug page. tools/pipdebug.sh, then open http://localhost:8765
With the cable: this bridges the board's USB port to the page (server-sent events out, posts in); the port is held
while this runs, so stop it before flashing. Without it: the page talks to Pip over Bluetooth itself (Chrome)."""
import glob, http.server, os, queue, sys, threading, time
import serial

HERE = __file__.rsplit("/", 1)[0]
def board():
    return (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
clients, lock = [], threading.Lock()
keep = {}  # the last @N, @K (one per knob) and @T, for a page that opens later
ser = None


def reader():
    global ser
    while True:
        port = board()
        if not port:
            time.sleep(2)
            continue
        try:
            ser = serial.Serial()
            ser.port, ser.baudrate, ser.timeout = port, 115200, 0.5
            ser.dtr = ser.rts = False  # opening must not reset the board
            ser.open()
            ser.write(b"names\ntune\n")
            buf = b""
            while True:
                buf += ser.read(4096)
                *lines, buf = buf.split(b"\n")
                for raw in lines:
                    line = raw.decode("utf-8", "replace").rstrip("\r")
                    if not line.startswith("@"):
                        if " ble: " in line or "E (" in line:  # the board's own Bluetooth story, for the terminal
                            print(line, flush=True)
                        continue
                    if line[:2] in ("@N", "@T"):
                        keep[line[:2]] = line
                    elif line[:2] == "@K":
                        keep[line.split(" ")[1]] = line  # the knobs come before @T, which builds the sliders
                    with lock:
                        for q in clients:
                            q.put(line)
        except Exception as e:  # unplugged or flashing: try again
            print("serial:", e, file=sys.stderr)
            time.sleep(1)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            q = queue.Queue()
            for v in keep.values():
                q.put(v)
            with lock:
                clients.append(q)
            try:
                while True:
                    try:
                        line = q.get(timeout=15)
                    except queue.Empty:
                        line = ":"  # keep-alive
                    self.wfile.write(("data: " + line + "\n\n").encode())
                    self.wfile.flush()
            except Exception:
                pass
            finally:
                with lock:
                    clients.remove(q)
            return
        body = open(HERE + "/../docs/debug.html", "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        cmd = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        if ser and ser.is_open:
            ser.write((cmd.strip() + "\n").encode())
        self.send_response(204)
        self.end_headers()


threading.Thread(target=reader, daemon=True).start()
WEB = int(os.environ.get("PORT", 8765))
print("Pip debug on http://localhost:%d  (USB: %s; without it use the Bluetooth button, in Chrome)" % (WEB, board() or "none"), flush=True)
http.server.ThreadingHTTPServer(("127.0.0.1", WEB), Handler).serve_forever()
