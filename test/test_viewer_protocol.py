"""Protocol conformance/lifecycle tests; no GW, HTTP server or genomic files needed."""
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / "deps/protocol/viewer/v1"
sys.path.insert(0, str(ROOT / "deps"))  # the protocol package lives in deps/
from protocol.viewer.v1 import server as p


def png(w, h):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress((b"\0" * (w + 1)) * h)) + chunk(b"IEND", b""))


class Backend:
    capabilities = p.ACTIONS

    def __init__(self):
        self.position = 100
        self.calls = []
        self.cancelled, self.released = [], []
        self.failed_render = False

    def snapshot(self):
        return {"regions": [f"chr1:{self.position}-{self.position + 100}"]}

    def apply(self, cid, action, frame):
        self.calls.append((cid, action, frame))
        if action["type"] == "command":
            self.position += 1
            return True
        return False

    def render(self, cid, viewport):
        if self.failed_render:
            raise RuntimeError("test render failure")
        _, (w, h) = p.viewport(viewport)
        return p.Render(png(w, h), {"panes": [{"id": "pane-1", "x": 0, "y": 0, "width": viewport["width"], "height": viewport["height"]}]}, {"owner": cid})

    def cancel(self, cid):
        self.cancelled.append(cid)

    def release_client(self, cid):
        self.released.append(cid)


class TestProtocol(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.backend = Backend()
        self.router = p.Router(clock=lambda: self.now, reply_limit=2)
        self.router.add_session("demo", self.backend)
        self.serial = 0
        self.messages = []
        self.a = self.join()
        self.b = self.join(width=500, height=250, ratio=2)

    def send(self, msg):
        reply = self.router.handle(msg)
        self.messages.extend([msg, reply])
        return reply

    def join(self, width=600, height=300, ratio=1, **resume):
        self.serial += 1
        return self.send(dict(protocol=p.PROTOCOL, version=1, type="join", request_id=f"join-{self.serial}",
                              session_id="demo", viewport=dict(width=width, height=height, pixel_ratio=ratio), **resume))

    def message(self, client, kind="snapshot", **extra):
        self.serial += 1
        seq = self.router.sessions["demo"].clients[client["client_id"]].last_sequence + 1
        return dict(protocol=p.PROTOCOL, version=1, type=kind, request_id=f"r-{self.serial}", session_id="demo",
                    client_id=client["client_id"], connection_id=client["connection_id"], sequence=str(seq), **extra)

    def action(self, client, action, **extra):
        return self.message(client, "action", expected_revision=client["view"]["revision"], action=action, **extra)

    def pointer(self, phase):
        return dict(type="pointer", phase=phase, x=120, y=70, button="left", modifiers=[])

    def test_independent_viewports_and_shared_revision(self):
        self.assertEqual((self.a["view"]["frame"]["width"], self.b["view"]["frame"]["width"]), (600, 1000))
        self.assertEqual(self.a["view"]["revision"], self.b["view"]["revision"])
        a = self.send(self.action(self.a, dict(type="command", text="right")))
        b = self.send(self.message(self.b))
        self.assertEqual(a["view"]["shared"], b["view"]["shared"])
        self.assertEqual(b["view"]["frame"]["height"], 500)
        self.assertNotEqual(a["view"]["client"], b["view"]["client"])
        self.assertEqual(self.send(self.action(self.b, dict(type="command", text="right")))["error"]["code"], "stale_revision")

    def test_exact_retry_and_eviction_never_reapply(self):
        msg = self.action(self.a, dict(type="command", text="right"))
        first = self.send(msg)
        self.assertEqual(self.send(msg), first)
        self.assertEqual(len(self.backend.calls), 1)
        conflict = dict(msg, request_id="different")
        self.assertEqual(self.send(conflict)["error"]["code"], "sequence_conflict")
        for _ in range(2):
            self.send(self.message(self.a, "heartbeat"))
        self.assertEqual(self.send(msg)["error"]["code"], "duplicate_expired")
        self.assertEqual(len(self.backend.calls), 1)

    def test_frame_identity_dpi_and_resize(self):
        fid = self.b["view"]["frame"]["frame_id"]
        self.send(self.action(self.b, self.pointer("press"), frame_id=fid))
        self.assertEqual(self.backend.calls[-1][2]["viewport"]["pixel_ratio"], 2)
        self.assertEqual(self.backend.calls[-1][1]["x"], 120)
        self.send(self.message(self.b, "viewport", viewport=dict(width=600, height=300, pixel_ratio=1.5)))
        reply = self.send(self.action(self.b, self.pointer("release"), frame_id=fid))
        self.assertEqual(reply["error"]["code"], "stale_frame")
        self.assertIn(self.b["client_id"], self.backend.cancelled)
        self.assertEqual(self.router.sessions["demo"].revision, 1)

    def test_client_cannot_use_another_clients_frame(self):
        msg = self.action(self.b, self.pointer("press"), frame_id=self.a["view"]["frame"]["frame_id"])
        self.assertEqual(self.send(msg)["error"]["code"], "stale_frame")
        with self.assertRaises(p.ProtocolError):
            self.router.frame("demo", self.b["client_id"], self.b["connection_id"], self.a["view"]["frame"]["frame_id"])

    def test_disconnect_resume_and_expiry(self):
        self.send(self.action(self.a, self.pointer("press"), frame_id=self.a["view"]["frame"]["frame_id"]))
        self.router.disconnect("demo", self.a["client_id"], self.a["connection_id"])
        self.assertIn(self.a["client_id"], self.backend.cancelled)
        resumed = self.join(client_id=self.a["client_id"], resume_token=self.a["resume_token"])
        self.assertEqual(resumed["client_id"], self.a["client_id"])
        self.assertNotEqual(resumed["connection_id"], self.a["connection_id"])
        self.assertEqual(self.send(self.message(self.a))["error"]["code"], "client_expired")
        # A delayed close from the old socket must not disconnect the resumed one.
        self.router.disconnect("demo", self.a["client_id"], self.a["connection_id"])
        self.assertEqual(self.send(self.message(resumed))["type"], "state")
        self.now = 31
        self.router.sweep()
        self.now = 152
        self.router.sweep()
        self.assertEqual(len(self.router.sessions["demo"].clients), 0)
        self.assertIn("demo", self.router.sessions)  # Browser loss never deletes shared view.
        self.assertEqual(len(self.backend.released), 2)

    def test_gesture_timeout_despite_heartbeats_and_shared_mutations(self):
        self.send(self.action(self.a, self.pointer("press"), frame_id=self.a["view"]["frame"]["frame_id"]))
        self.now = 16
        self.send(self.message(self.a, "heartbeat"))
        self.assertFalse(self.router.sessions["demo"].clients[self.a["client_id"]].gesture)
        self.send(self.action(self.a, self.pointer("press"), frame_id=self.a["view"]["frame"]["frame_id"]))
        self.send(self.action(self.b, dict(type="command", text="right")))
        self.assertFalse(self.router.sessions["demo"].clients[self.a["client_id"]].gesture)

    def test_render_failure_after_commit_is_not_repeated(self):
        self.backend.failed_render = True
        msg = self.action(self.a, dict(type="command", text="right"))
        reply = self.send(msg)
        self.assertEqual(reply["error"]["code"], "backend_error")
        self.assertEqual(self.send(msg), reply)
        self.assertEqual(self.router.sessions["demo"].revision, 2)
        self.assertEqual(len(self.backend.calls), 1)
        self.backend.failed_render = False
        self.assertEqual(self.send(self.message(self.a))["view"]["revision"], "2")

    def test_limits_nonfinite_and_uint64(self):
        for value in ("0", "01", "18446744073709551616", "1.0", "١"):
            with self.assertRaises(p.ProtocolError):
                p.counter(value)
        self.assertEqual(p.counter("18446744073709551615"), 2**64-1)
        for ratio in (float("nan"), float("inf"), 0, 9):
            with self.assertRaises(p.ProtocolError):
                p.viewport(dict(width=600, height=300, pixel_ratio=ratio))
        self.assertEqual(p.viewport(dict(width=601, height=301, pixel_ratio=1.5))[1], (902, 452))
        self.assertEqual(self.router.handle('{"protocol":"gw-viewer","version":2}')["error"]["code"], "unsupported_version")
        msg = self.action(self.a, dict(type="pointer", phase="wheel", x=1, y=2, button="none", modifiers=[]))
        self.assertEqual(self.send(msg)["error"]["code"], "invalid_message")

    def test_valid_messages_conform_to_schema(self):
        try:
            import jsonschema
        except ImportError:
            self.skipTest("Optional jsonschema dependency not installed")
        schema = json.loads((DIRECTORY / "schema.json").read_text())
        validator = jsonschema.Draft202012Validator(schema)
        validator.check_schema(schema)
        self.send(self.message(self.a, "heartbeat"))
        self.send(self.message(self.a, "viewport", viewport=dict(width=600, height=300, pixel_ratio=1.5)))
        self.send(self.action(self.b, dict(type="command", text="right")))
        self.send(self.message(self.a, "leave"))
        for msg in self.messages:
            validator.validate(msg)

    def test_javascript_reads_python_messages(self):
        script = """
import {readReply} from './deps/protocol/viewer/v1/client.mjs';
let data = ''; for await (const part of process.stdin) data += part;
for (const reply of JSON.parse(data)) readReply(reply, reply.request_id);
"""
        replies = [self.a, self.b, self.send(self.message(self.a)), self.send(self.message(self.b, "heartbeat"))]
        subprocess.run(["node", "--input-type=module", "-e", script], cwd=ROOT,
                       input=json.dumps(replies), text=True, check=True)

    def test_wsgi_authentication_and_binary_frame(self):
        from protocol.viewer.v1.wsgi_adapter import application
        app = application(self.router, lambda env, sid: env.get("HTTP_AUTHORIZATION") == "test" and sid == "demo")

        def call(path, payload, authorized=True):
            body = json.dumps(payload).encode()
            status = []
            env = {"REQUEST_METHOD": "POST", "PATH_INFO": path, "CONTENT_TYPE": "application/json",
                   "CONTENT_LENGTH": str(len(body)), "wsgi.input": io.BytesIO(body)}
            if authorized:
                env["HTTP_AUTHORIZATION"] = "test"
            result = b"".join(app(env, lambda code, headers: status.append(code)))
            return status[0], result

        msg = self.message(self.a)
        self.assertEqual(call("/message", msg, False)[0], "403 Forbidden")
        self.assertEqual(call("/message", msg)[0], "200 OK")
        identity = {k: self.a[k] for k in ("session_id", "client_id", "connection_id")}
        identity["frame_id"] = self.a["view"]["frame"]["frame_id"]
        code, data = call("/frame", identity)
        self.assertEqual(code, "200 OK")
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(call("/frame", identity, False)[0], "403 Forbidden")


if __name__ == "__main__":
    unittest.main()
