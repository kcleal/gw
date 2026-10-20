"""Optional GW viewer protocol host. Standard library only; no GW/Python ABI coupling.

HTTP/WebSocket handlers authenticate first, then call Router.handle(). Fetch PNG
bytes separately with Router.frame(). See README.md for the backend contract.
"""
from __future__ import annotations

from collections import OrderedDict
from copy import deepcopy
from dataclasses import dataclass, field
import hashlib
import json
import math
import secrets
import struct
import threading
import time
from typing import Protocol

PROTOCOL = "gw-viewer"
VERSION = 1
MAX_MESSAGE_BYTES = 65536
MAX_COUNTER = 2**64 - 1
ACTIONS = ("navigate", "command", "settings", "pointer", "key", "vscroll", "track_resize")


class ProtocolError(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def fail(code, message):
    raise ProtocolError(code, message)


def counter(value):
    # Decimal strings avoid JavaScript's 53-bit integer limit.
    if (not isinstance(value, str) or not value.isascii() or not value.isdigit()
            or value.startswith("0") or len(value) > 20 or int(value) > MAX_COUNTER):
        fail("invalid_message", "Counters must be canonical positive uint64 strings")
    return int(value)


def identifier(value):
    if not isinstance(value, str) or not 1 <= len(value) <= 128 or any(ord(c) < 33 or ord(c) > 126 for c in value):
        fail("invalid_message", "Invalid identifier")
    return value


def number(value, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high or not math.isfinite(value):
        fail("invalid_message", "Number out of range")
    return value


def integer(value, low, high):
    number(value, low, high)
    if not isinstance(value, int):
        fail("invalid_message", "Expected an integer")
    return value


def fields(obj, required, optional=()):
    if not isinstance(obj, dict) or not set(required) <= obj.keys() or obj.keys() - set(required) - set(optional) - {"extensions"}:
        fail("invalid_message", "Missing or unknown fields")
    if "extensions" in obj and not isinstance(obj["extensions"], dict):
        fail("invalid_message", "extensions must be an object")


def viewport(value):
    fields(value, ("width", "height", "pixel_ratio"))
    w = integer(value["width"], 1, 8192)
    h = integer(value["height"], 1, 8192)
    ratio = number(value["pixel_ratio"], .25, 8)
    # Same rounding in JS and C++; never silently shrink a high-DPI viewport.
    pw, ph = math.floor(w * ratio + .5), math.floor(h * ratio + .5)
    if not (200 <= pw <= 8192 and 100 <= ph <= 8192 and pw * ph <= 33554432):
        fail("invalid_viewport", "Framebuffer must be 200..8192 x 100..8192, at most 32 megapixels")
    return {"width": w, "height": h, "pixel_ratio": ratio}, (pw, ph)


def validate_action(a):
    if not isinstance(a, dict):
        fail("invalid_message", "Expected action object")
    kind = a.get("type")
    optional = ("region_index",)
    if kind in ("navigate", "command"):
        key = "region" if kind == "navigate" else "text"
        fields(a, ("type", key), optional)
        text = a[key]
        if not isinstance(text, str) or not 1 <= len(text) <= 4096 or any(c in text for c in "\r\n\0"):
            fail("invalid_message", "Invalid command or region")
    elif kind == "settings":
        fields(a, ("type", "values"))
        if not isinstance(a["values"], dict) or not a["values"] or len(a["values"]) > 128:
            fail("invalid_message", "Expected settings object")
        for k, v in a["values"].items():
            identifier(k)
            if isinstance(v, str):
                if len(v) > 4096:
                    fail("invalid_message", "Setting string too large")
            elif not isinstance(v, bool):
                number(v, -1e12, 1e12)
    elif kind == "pointer":
        fields(a, ("type", "phase", "x", "y", "button", "modifiers"), ("dx", "dy"))
        if a["phase"] not in ("press", "move", "release", "wheel", "cancel") or a["button"] not in ("left", "middle", "right", "none"):
            fail("invalid_message", "Invalid pointer phase/button")
        number(a["x"], -1e7, 1e7)
        number(a["y"], -1e7, 1e7)
        if a["phase"] == "wheel":
            if not {"dx", "dy"} <= a.keys():
                fail("invalid_message", "Wheel requires dx/dy in normalized steps")
        for key in ("dx", "dy"):
            if key in a:
                number(a[key], -1000, 1000)
    elif kind == "key":
        fields(a, ("type", "key", "modifiers"), optional)
        if a["key"] not in ("ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "PageUp", "PageDown", "BracketLeft", "BracketRight", "KeyL", "KeyF", "KeyR", "Minus", "Equal"):
            fail("unsupported_action", "Unsupported viewer key")
    elif kind == "vscroll":
        fields(a, ("type", "collection_id", "offset"))
        identifier(a["collection_id"])
        integer(a["offset"], 0, 2147483647)
    elif kind == "track_resize":
        fields(a, ("type", "track_id", "y"))
        identifier(a["track_id"])
        number(a["y"], -1e7, 1e7)
    else:
        fail("unsupported_action", "Unknown action type")
    if "region_index" in a:
        integer(a["region_index"], 0, 65535)
    if "modifiers" in a:
        mods = a["modifiers"]
        if not isinstance(mods, list) or any(m not in ("shift", "control", "alt", "meta") for m in mods) or len(set(mods)) != len(mods):
            fail("invalid_message", "Invalid modifiers")


def read_message(raw):
    try:
        if isinstance(raw, (str, bytes)):
            if len(raw.encode() if isinstance(raw, str) else raw) > MAX_MESSAGE_BYTES:
                fail("invalid_message", "Message too large")
            raw = json.loads(raw)
        encoded = json.dumps(raw, allow_nan=False, separators=(",", ":")).encode()
        if len(encoded) > MAX_MESSAGE_BYTES:
            fail("invalid_message", "Message too large")
    except (ValueError, TypeError, UnicodeError, RecursionError) as e:
        if isinstance(e, ProtocolError):
            raise
        fail("invalid_message", "Invalid JSON")
    if not isinstance(raw, dict) or raw.get("protocol") != PROTOCOL or type(raw.get("version")) is not int or raw["version"] != VERSION:
        fail("unsupported_version", "Expected gw-viewer version 1")
    base = ("protocol", "version", "type", "request_id", "session_id")
    kind = raw.get("type")
    if kind == "join":
        fields(raw, (*base, "viewport"), ("client_id", "resume_token"))
        if ("client_id" in raw) != ("resume_token" in raw):
            fail("invalid_message", "Resume requires client_id and resume_token")
        viewport(raw["viewport"])
        if "resume_token" in raw:
            identifier(raw["resume_token"])
    else:
        base += ("client_id", "connection_id", "sequence")
        extra = {"action": ("expected_revision", "action"), "viewport": ("viewport",), "snapshot": (), "heartbeat": (), "leave": ()}
        if kind not in extra:
            fail("invalid_message", "Unknown message type")
        fields(raw, (*base, *extra[kind]), ("frame_id",) if kind == "action" else ())
        counter(raw["sequence"])
        if kind == "action":
            counter(raw["expected_revision"])
            validate_action(raw["action"])
            if raw["action"]["type"] in ("pointer", "key", "vscroll", "track_resize"):
                identifier(raw.get("frame_id"))
        if kind == "viewport":
            viewport(raw["viewport"])
    for key in ("request_id", "session_id", "client_id", "connection_id", "frame_id"):
        if key in raw:
            identifier(raw[key])
    return deepcopy(raw)


@dataclass
class Render:
    png: bytes
    layout: dict = field(default_factory=dict)
    client: dict = field(default_factory=dict)


class Backend(Protocol):
    """One shared view, client-private interaction/render state. Host serializes calls.

    apply must be atomic on failure. Return True only when shared state changes.
    cancel/release_client must not change shared state. render must not mutate it.
    frame is the exact metadata used for hit-testing; pointer coordinates are CSS
    pixels relative to that frame (multiply by its pixel_ratio for native GW).
    """
    capabilities: tuple[str, ...]

    def snapshot(self) -> dict: ...
    def apply(self, client_id: str, action: dict, frame: dict | None) -> bool: ...
    def render(self, client_id: str, viewport: dict) -> Render: ...
    def cancel(self, client_id: str) -> None: ...
    def release_client(self, client_id: str) -> None: ...


@dataclass
class Client:
    resume_token: str
    connection_id: str
    viewport: dict
    last_seen: float
    last_sequence: int = 0
    replies: OrderedDict = field(default_factory=OrderedDict)
    frames: OrderedDict = field(default_factory=OrderedDict)
    disconnected_at: float | None = None
    gesture: bool = False
    gesture_seen: float = 0


@dataclass
class Session:
    backend: Backend
    revision: int = 1
    clients: dict = field(default_factory=dict)


class Router:
    """Transport-neutral reference host; sessions provisioned by trusted application.

    Call sweep periodically even without traffic. Authorize session membership
    before handle/frame/disconnect. IDs/tokens alone do not provide authorization.
    A per-router lock makes the reference implementation deliberately serial.
    """
    def __init__(self, *, clock=time.monotonic, heartbeat_timeout=30, reconnect_grace=120,
                 gesture_timeout=15, max_clients=32, reply_limit=64, frame_limit=2):
        if min(heartbeat_timeout, reconnect_grace, gesture_timeout, max_clients, reply_limit, frame_limit) <= 0:
            raise ValueError("Limits must be positive")
        self.sessions = {}
        self.clock = clock
        self.heartbeat_timeout, self.reconnect_grace = heartbeat_timeout, reconnect_grace
        self.gesture_timeout = gesture_timeout
        self.max_clients, self.reply_limit, self.frame_limit = max_clients, reply_limit, frame_limit
        self.lock = threading.RLock()

    def add_session(self, session_id, backend):
        with self.lock:
            identifier(session_id)
            if session_id in self.sessions:
                raise ValueError("Session already exists")
            self.sessions[session_id] = Session(backend)

    def close_session(self, session_id):
        with self.lock:
            s = self.sessions.pop(session_id)
            for cid in s.clients:
                s.backend.cancel(cid)
                s.backend.release_client(cid)

    def _cancel(self, s, cid, c):
        s.backend.cancel(cid)
        c.gesture = False

    def disconnect(self, session_id, client_id, connection_id):
        with self.lock:
            s = self.sessions.get(session_id)
            c = s.clients.get(client_id) if s else None
            if c and secrets.compare_digest(c.connection_id, connection_id) and c.disconnected_at is None:
                self._cancel(s, client_id, c)
                c.disconnected_at = self.clock()
                c.frames.clear()

    def sweep(self):
        with self.lock:
            now = self.clock()
            for sid, s in self.sessions.items():
                for cid, c in list(s.clients.items()):
                    if c.disconnected_at is None and now - c.last_seen >= self.heartbeat_timeout:
                        self.disconnect(sid, cid, c.connection_id)
                    if c.gesture and now - c.gesture_seen >= self.gesture_timeout:
                        self._cancel(s, cid, c)
                    if c.disconnected_at is not None and now - c.disconnected_at >= self.reconnect_grace:
                        s.backend.release_client(cid)
                        del s.clients[cid]

    def _view(self, s, cid, c):
        rendered = s.backend.render(cid, deepcopy(c.viewport))
        _, size = viewport(c.viewport)
        png = bytes(rendered.png)
        if len(png) < 24 or png[:8] != b"\x89PNG\r\n\x1a\n" or png[12:16] != b"IHDR" or struct.unpack(">II", png[16:24]) != size:
            raise RuntimeError("Backend PNG dimensions do not match requested framebuffer")
        fid = secrets.token_urlsafe(18)
        descriptor = {"frame_id": fid, "revision": str(s.revision), "viewport": deepcopy(c.viewport),
                      "width": size[0], "height": size[1], "mime_type": "image/png", "layout": rendered.layout}
        c.frames[fid] = (deepcopy(descriptor), png)
        while len(c.frames) > self.frame_limit:
            c.frames.popitem(last=False)
        return {"revision": str(s.revision), "shared": s.backend.snapshot(), "client": rendered.client,
                "frame": descriptor}

    def frame(self, session_id, client_id, connection_id, frame_id):
        with self.lock:
            self.sweep()
            s, c = self._client(session_id, client_id, connection_id)
            if frame_id not in c.frames:
                fail("stale_frame", "Frame expired; request a snapshot")
            c.last_seen = self.clock()
            return c.frames[frame_id][1]

    def _client(self, sid, cid, connection):
        s = self.sessions.get(sid)
        c = s.clients.get(cid) if s else None
        if not c or c.disconnected_at is not None or not secrets.compare_digest(c.connection_id, connection):
            fail("client_expired", "Join or reconnect before making requests")
        return s, c

    def handle(self, raw):
        request = {}
        with self.lock:
            try:
                request = read_message(raw)
                self.sweep()
                if request["session_id"] not in self.sessions:
                    fail("session_not_found", "Unknown session")
                result = self._handle(request)
                # Detach backend dictionaries and reject non-JSON response values.
                return json.loads(json.dumps(result, allow_nan=False))
            except ProtocolError as e:
                return self._reply(request, "error", error={"code": e.code, "message": str(e)})
            except Exception:
                return self._reply(request, "error", error={"code": "backend_error", "message": "Backend failed; request a snapshot"})

    def _reply(self, r, kind, **values):
        return {"protocol": PROTOCOL, "version": VERSION, "type": kind,
                "request_id": r.get("request_id", "invalid"), **values}

    def _handle(self, r):
        s = self.sessions[r["session_id"]]
        if r["type"] == "join":
            return self._join(s, r)
        cid = r["client_id"]
        s, c = self._client(r["session_id"], cid, r["connection_id"])
        seq = counter(r["sequence"])
        digest = hashlib.sha256(json.dumps(r, sort_keys=True, separators=(",", ":")).encode()).digest()
        if seq in c.replies:
            old_digest, reply = c.replies[seq]
            if digest != old_digest:
                fail("sequence_conflict", "Sequence was already used for a different request")
            c.last_seen = self.clock()
            return deepcopy(reply)
        if seq <= c.last_sequence:
            fail("duplicate_expired", "Request already processed; fetch a snapshot, do not replay it")
        c.last_sequence, c.last_seen = seq, self.clock()
        # Consume every valid authenticated sequence, including rejected actions.
        try:
            reply = self._process(s, cid, c, r)
        except ProtocolError as e:
            reply = self._reply(r, "error", error={"code": e.code, "message": str(e)})
        except Exception:
            # Do not retry a possibly committed backend mutation after render failure.
            self._cancel(s, cid, c)
            reply = self._reply(r, "error", error={"code": "backend_error", "message": "Backend failed; request a snapshot"})
        c.replies[seq] = (digest, deepcopy(reply))
        while len(c.replies) > self.reply_limit:
            c.replies.popitem(last=False)
        return reply

    def _join(self, s, r):
        vp, _ = viewport(r["viewport"])
        cid = r.get("client_id")
        if cid:
            c = s.clients.get(cid)
            if not c or not secrets.compare_digest(c.resume_token, r["resume_token"]):
                fail("client_expired", "Resume credentials expired")
            self._cancel(s, cid, c)
            c.connection_id = secrets.token_urlsafe(24)
            c.frames.clear()
            c.replies.clear()
        else:
            if len(s.clients) >= self.max_clients:
                fail("capacity", "Session client limit reached")
            cid = secrets.token_urlsafe(18)
            c = Client(secrets.token_urlsafe(32), secrets.token_urlsafe(24), vp, self.clock())
            s.clients[cid] = c
        c.viewport, c.last_seen, c.disconnected_at = vp, self.clock(), None
        try:
            view = self._view(s, cid, c)
        except Exception:
            self._cancel(s, cid, c)
            c.disconnected_at = self.clock()
            if "client_id" not in r:
                s.backend.release_client(cid)
                del s.clients[cid]
            raise
        return self._reply(r, "welcome", session_id=r["session_id"], client_id=cid,
                           connection_id=c.connection_id, resume_token=c.resume_token,
                           last_sequence=str(c.last_sequence), heartbeat_seconds=self.heartbeat_timeout / 3,
                           capabilities=list(s.backend.capabilities), view=view)

    def _process(self, s, cid, c, r):
        kind = r["type"]
        if kind == "heartbeat":
            return self._reply(r, "ack", revision=str(s.revision))
        if kind == "leave":
            self._cancel(s, cid, c)
            s.backend.release_client(cid)
            del s.clients[cid]
            return self._reply(r, "ack", revision=str(s.revision))
        if kind == "viewport":
            self._cancel(s, cid, c)
            c.viewport, _ = viewport(r["viewport"])
            c.frames.clear()
        if kind == "action":
            a = r["action"]
            if a["type"] not in s.backend.capabilities:
                fail("unsupported_action", "Backend does not advertise this action")
            cancel = a["type"] == "pointer" and a["phase"] == "cancel"
            if cancel:
                self._cancel(s, cid, c)  # Cancellation remains legal after view changes.
            else:
                if counter(r["expected_revision"]) != s.revision:
                    self._cancel(s, cid, c)
                    fail("stale_revision", "Shared view changed; request a snapshot")
                frame = None
                if "frame_id" in r:
                    entry = c.frames.get(r["frame_id"])
                    if not entry or entry[0]["revision"] != str(s.revision):
                        self._cancel(s, cid, c)
                        fail("stale_frame", "Frame layout expired; request a snapshot")
                    frame = deepcopy(entry[0])
                if s.revision == MAX_COUNTER:
                    fail("capacity", "Session revision exhausted")
                if a["type"] == "pointer":
                    if a["phase"] == "release" and not c.gesture:
                        fail("gesture_expired", "Gesture was cancelled; start a new gesture")
                    if a["phase"] == "press" and c.gesture:
                        self._cancel(s, cid, c)
                changed = s.backend.apply(cid, deepcopy(a), frame)
                if changed:
                    s.revision += 1
                    # Other clients' in-progress gestures target an obsolete layout.
                    for other_id, other in s.clients.items():
                        if other_id != cid and other.gesture:
                            self._cancel(s, other_id, other)
                if a["type"] == "pointer":
                    if a["phase"] == "press":
                        c.gesture = True
                    elif a["phase"] == "release":
                        c.gesture = False
                    c.gesture_seen = self.clock()
        return self._reply(r, "state", view=self._view(s, cid, c))
