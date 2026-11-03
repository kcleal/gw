"""Small optional WSGI binding; usable directly or mounted under FastAPI/Flask.

Authentication is mandatory: authorize(environ, session_id) must return True.
The host supplies scheduling for router.sweep() and server-side session creation.
"""
import json
from .server import MAX_MESSAGE_BYTES, ProtocolError, fields, identifier


def application(router, authorize):
    def app(environ, start_response):
        def respond(status, content_type, body):
            start_response(status, [("Content-Type", content_type), ("Content-Length", str(len(body))),
                                    ("Cache-Control", "no-store"), ("X-Content-Type-Options", "nosniff")])
            return [body]

        def error(status, code, message):
            return respond(status, "application/json", json.dumps({"error": {"code": code, "message": message}}).encode())

        if environ["REQUEST_METHOD"] != "POST":
            return error("405 Method Not Allowed", "method_not_allowed", "Use POST")
        path = environ.get("PATH_INFO", "")
        if path not in ("/message", "/frame"):
            return error("404 Not Found", "not_found", "Unknown endpoint")
        try:
            length = int(environ.get("CONTENT_LENGTH", "0"))
            if not 0 < length <= MAX_MESSAGE_BYTES:
                return error("413 Content Too Large", "invalid_message", "Invalid content length")
            if environ.get("CONTENT_TYPE", "").split(";", 1)[0].strip() != "application/json":
                return error("415 Unsupported Media Type", "invalid_message", "Expected application/json")
            raw = environ["wsgi.input"].read(length)
            if len(raw) != length:
                return error("400 Bad Request", "invalid_message", "Incomplete body")
            value = json.loads(raw)
            if not isinstance(value, dict):
                return error("400 Bad Request", "invalid_message", "Expected object")
            sid = identifier(value.get("session_id"))
            # Authorization applies to every request, including binary frame reads.
            if not authorize(environ, sid):
                return error("403 Forbidden", "forbidden", "Session access denied")
            if path == "/message":
                reply = router.handle(raw)
                return respond("200 OK", "application/json", json.dumps(reply, allow_nan=False).encode())
            fields(value, ("session_id", "client_id", "connection_id", "frame_id"))
            for key in value:
                if key != "extensions":
                    identifier(value[key])
            png = router.frame(value["session_id"], value["client_id"], value["connection_id"], value["frame_id"])
            return respond("200 OK", "image/png", png)
        except ProtocolError as e:
            return error("409 Conflict", e.code, str(e))
        except (ValueError, UnicodeError, RecursionError):
            return error("400 Bad Request", "invalid_message", "Invalid JSON or content length")
    return app
