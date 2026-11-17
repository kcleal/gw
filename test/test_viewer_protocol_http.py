"""Real viewer-v1 server conformance. Start with test.fa/test.bam and two tracks.

python3 test/test_viewer_protocol_http.py http://127.0.0.1:8767
"""
import json
from pathlib import Path
import struct
import sys
import urllib.request
import uuid

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8766"
try:
    import jsonschema
    validator = jsonschema.Draft202012Validator(json.loads((Path(__file__).resolve().parents[1] / "deps/protocol/viewer/v1/schema.json").read_text()))
except ImportError:
    validator = None


def post(path, value):
    req = urllib.request.Request(URL + path, json.dumps(value).encode(), {"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as response:
        return response.read()


def exchange(value):
    if validator:
        validator.validate(value)
    reply = json.loads(post("/viewer/v1/message", value))
    if validator:
        validator.validate(reply)
    return reply


class Client:
    def __init__(self, width, height, ratio):
        self.sequence = 0
        self.viewport = dict(width=width, height=height, pixel_ratio=ratio)
        self.join()

    def join(self, resume=None):
        reply = exchange(dict(protocol="gw-viewer", version=1, type="join", request_id=str(uuid.uuid4()),
                              session_id="default", viewport=self.viewport, **(resume or {})))
        assert reply["type"] == "welcome", reply
        self.identity = {k: reply[k] for k in ("session_id", "client_id", "connection_id")}
        self.resume = {k: reply[k] for k in ("client_id", "resume_token")}
        self.sequence = int(reply["last_sequence"])
        self.view = reply["view"]

    def message(self, kind="snapshot", **extra):
        self.sequence += 1
        return dict(protocol="gw-viewer", version=1, type=kind, request_id=str(uuid.uuid4()),
                    sequence=str(self.sequence), **self.identity, **extra)

    def send(self, kind="snapshot", **extra):
        reply = exchange(self.message(kind, **extra))
        assert reply["type"] != "error", reply
        if "view" in reply:
            self.view = reply["view"]
        return reply

    def action(self, action):
        extra = dict(expected_revision=self.view["revision"], action=action)
        if action["type"] in ("pointer", "key", "vscroll", "track_resize"):
            extra["frame_id"] = self.view["frame"]["frame_id"]
        return self.send("action", **extra)

    def command(self, command):
        return self.action(dict(type="command", text=command))

    def pointer(self, phase, x, y, **extra):
        return self.action(dict(type="pointer", phase=phase, x=x, y=y, button="left", modifiers=[], **extra))

    def frame(self):
        f = self.view["frame"]
        data = post("/viewer/v1/frame", dict(**self.identity, frame_id=f["frame_id"]))
        assert struct.unpack(">II", data[16:24]) == (f["width"], f["height"])
        return data


a, b = Client(900, 600, 1), Client(601, 401, 1.5)
assert a.view["frame"]["width"] == 900
assert b.view["frame"]["width"] == 902
a.frame(); b.frame()
a.command("chr1:9900-10300")
stale = exchange(b.message("action", expected_revision=b.view["revision"], action=dict(type="command", text="right")))
assert stale["error"]["code"] == "stale_revision", stale
b.send()
assert b.view["shared"]["regions"] == a.view["shared"]["regions"]

# Local click/popups keep the shared revision unchanged and stay client-private.
before = a.view["revision"]
a.pointer("press", 450, 66); a.pointer("release", 450, 66)
assert "seq " in a.view["client"]["popups"], a.view["client"]
assert a.view["revision"] == before
b.send()
assert not b.view["client"]["popups"]

# Shared display settings and synthetic track topology propagate across renderers.
a.action(dict(type="settings", values=dict(snp_threshold=0)))
b.send()
assert b.view["shared"]["settings"]["snp_threshold"] == 0
a.action(dict(type="settings", values=dict(snp_threshold=100000)))
b.send()
assert b.view["shared"]["settings"]["snp_threshold"] == 100000
a.command("theme igv"); b.send()
assert b.view["shared"]["theme"] == "igv"
a.command("introns"); b.send()
assert len(b.view["frame"]["layout"]["tracks"].strip().splitlines()) == 3
a.command("introns"); b.send()
assert len(b.view["frame"]["layout"]["tracks"].strip().splitlines()) == 2

# Track resizing uses the actor's logical coordinate system.
old = a.view["frame"]["layout"]["tracks"]
top = float(old.splitlines()[0].split()[0])
a.action(dict(type="track_resize", track_id="0", y=top-30))
b.send()
assert a.view["frame"]["layout"]["tracks"] != old
assert b.view["frame"]["width"] == 902

# Buffered dragging, wheel, command aliases, filter replication and key relay.
before = a.view["shared"]["regions"]
a.pointer("press", 450, 240); a.pointer("move", 470, 240); a.pointer("release", 470, 240)
assert a.view["shared"]["regions"] != before
before = a.view["shared"]["regions"]
a.pointer("wheel", 450, 240, dx=0, dy=1)
assert a.view["shared"]["regions"] != before
a.action(dict(type="key", key="ArrowRight", modifiers=[]))
a.command("filter mapq >= 30"); b.send(); b.frame()
a.command("refresh")
a.command("chr1")
assert a.view["shared"]["regions"][0].startswith("chr1:")
a.command("this-is-not-a-command")
assert a.view["client"]["output"]
b.send()
assert not b.view["client"]["output"]

# Exact request replay returns the original frame/reply without executing again.
msg = a.message("action", expected_revision=a.view["revision"], action=dict(type="command", text="chr1:9900-10300"))
reply = exchange(msg)
assert exchange(msg) == reply
a.view = reply["view"]

# Resume supersedes old connection IDs, preserves client identity and sends a snapshot.
old_identity = dict(a.identity)
a.join(a.resume)
assert a.identity["client_id"] == old_identity["client_id"]
assert a.identity["connection_id"] != old_identity["connection_id"]
old_request = dict(a.message("heartbeat"), **old_identity)
assert exchange(old_request)["error"]["code"] == "client_expired"
a.frame()
for c in (a, b):
    c.send("leave")
print("PASS: viewer-v1 real PNGs, schema, sharing/DPI, private popups, settings, tracks, gestures, commands, retry and reconnect")
