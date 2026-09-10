#!/usr/bin/env python3
import argparse
import base64
import hashlib
import json
import threading
import traceback

_served_lock = threading.Lock()
_served_threads = []
_thread_errors = []
_other_errors = []


class ProtocolError(AssertionError):
    pass


def require(condition, detail):
    if not condition:
        raise ProtocolError(detail)


def _record_thread_error(args):
    text = "".join(traceback.format_exception(
        args.exc_type, args.exc_value, args.exc_traceback))
    if issubclass(args.exc_type, ProtocolError):
        _thread_errors.append(text)
    else:
        _other_errors.append(text)


threading.excepthook = _record_thread_error


session_of_sub = {}

# How many times each session has been ATTACHED to. A child probe is one
# attach on its own connection, so this is the only place a test can see
# whether the client probed a child at all -- the client's own state says what
# it believes, not what it did on the wire.
_probe_lock = threading.Lock()
attach_counts = {}
# Every page command aimed at resume-local, so a test can say how much a
# resume asked the wire for. resume-local is 100 durable user_input frames,
# seq 1..100, paged newest-first the way the real server pages.
page_requests = []
RESUME_TOP = 100


def resume_page(command):
    before = command.get("before")
    limit = int(command.get("limit", 40))
    hi = RESUME_TOP if before is None else min(int(before) - 1, RESUME_TOP)
    lo = max(1, hi - limit + 1)
    frames = [{"seq": n, "created_at_unix_ms": 1700000000000 + n * 1000,
               "event": {"type": "user_input", "text": f"row {n}"}}
              for n in range(lo, hi + 1)] if hi >= 1 else []
    return {"type": "page", "frames": frames, "done": lo <= 1}
attachment_received = threading.Event()
attachment_payload = None
posted_targets = set()


def _dumps(v):
    return json.dumps(v, separators=(",", ":"))
import signal
import socket
import struct
import sys


def read_exact(conn, count):
    data = b""
    while len(data) < count:
        chunk = conn.recv(count - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


def read_frame(conn):
    first, second = read_exact(conn, 2)
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", read_exact(conn, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", read_exact(conn, 8))[0]
    mask = read_exact(conn, 4) if second & 0x80 else b""
    payload = bytearray(read_exact(conn, length))
    if mask:
        for i in range(length):
            payload[i] ^= mask[i % 4]
    if opcode == 8:
        raise EOFError
    return json.loads(payload.decode())


def send_frame(conn, value):
    payload = json.dumps(value, separators=(",", ":")).encode()
    if len(payload) < 126:
        header = bytes([0x81, len(payload)])
    else:
        header = bytes([0x81, 126]) + struct.pack("!H", len(payload))
    conn.sendall(header + payload)


def serve_connection(listener):
    try:
        conn, _ = listener.accept()
    except socket.timeout:
        return False
    thread = threading.Thread(target=_serve, args=(conn,), daemon=True)
    thread.start()
    with _served_lock:
        _served_threads.append(thread)
    return True


def _serve(conn):
    with conn:
        request = b""
        while b"\r\n\r\n" not in request:
            request += conn.recv(4096)
        head, body = request.split(b"\r\n\r\n", 1)
        lines = head.decode().split("\r\n")
        method, target, _ = lines[0].split(" ", 2)
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                key, value = line.split(":", 1)
                headers[key.lower()] = value.strip()
        if method == "POST":
            global attachment_payload
            length = int(headers.get("content-length", "0"))
            while len(body) < length:
                body += conn.recv(length - len(body))
            require(headers.get("crypto_auth_tokens") == "local-test-token", headers)
            attachment_payload = json.loads(body[:length].decode())
            messages = attachment_payload.get("messages", [])
            require(len(messages) == 1, attachment_payload)
            message = messages[0]
            require(set(message) == {"text", "apply", "attachments"}, message)
            require(message.get("apply") == "after_tool_round", message)
            require("idempotency_key" not in message, message)
            files = message.get("attachments", [])
            if target == "/sessions/reject-local/messages":
                response = b'{"error":"attachment storage unavailable"}'
                conn.sendall(
                    b"HTTP/1.1 503 Service Unavailable\r\n"
                    + b"Content-Type: application/json\r\n"
                    + f"Content-Length: {len(response)}\r\n".encode()
                    + b"Connection: close\r\n\r\n"
                    + response)
                return
            if target == "/sessions/attachment-local/messages":
                require(message.get("text") == "inspect both files", message)
                require([f.get("name") for f in files] ==
                        ["tiny.png", "notes.md"], files)
                require([f.get("media_type") for f in files] ==
                        ["image/png", "text/markdown"], files)
                require([base64.b64decode(f.get("data", "")) for f in files] ==
                        [b"png-bytes", b"# notes\n"], files)
                input_id = 81
            elif target == "/sessions/created-attachment-local/messages":
                require(message.get("text") == "start with evidence", message)
                require([f.get("name") for f in files] == ["notes.md"], files)
                input_id = 91
            elif target == "/sessions/fork-bare/messages":
                require(message.get("text") == "fork with evidence", message)
                require([f.get("name") for f in files] == ["tiny.png"], files)
                input_id = 92
            else:
                raise ProtocolError(target)
            response = json.dumps({"input_ids": [input_id]},
                                  separators=(",", ":")).encode()
            conn.sendall(
                b"HTTP/1.1 202 Accepted\r\n"
                + b"Content-Type: application/json\r\n"
                + f"Content-Length: {len(response)}\r\n".encode()
                + b"Connection: close\r\n\r\n"
                + response)
            posted_targets.add(target)
            if target == "/sessions/attachment-local/messages":
                attachment_received.set()
            return
        accept = base64.b64encode(
            hashlib.sha1((headers["sec-websocket-key"] +
                          "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
        ).decode()
        conn.sendall((
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n\r\n"
        ).encode())
        attached = False
        while True:
            try:
                envelope = read_frame(conn)
            except EOFError:
                return
            sub = envelope.get("sub", 0)
            command = envelope.get("payload", {})
            kind = command.get("cmd")
            if kind == "attach":
                if attached:
                    send_frame(conn, {"sub": 0, "msg": {
                        "type": "error",
                        "message": "already attached; open another connection"}})
                    continue
                attached = True
            if kind == "input":
                if session_of_sub.get(sub) == "turn-retract":
                    send_frame(conn, {"sub": sub, "msg": {
                        "type": "frame", "frame": "durable", "seq": 90,
                        "event": {"type": "child_elicitation_update",
                                  "session": "kid-local",
                                  "elicitation": 41,
                                  "cause": 55}}})
                    send_frame(conn, {"sub": sub, "msg": {
                        "type": "frame", "frame": "durable", "seq": 91,
                        "event": {"type": "run_finished"}}})
                    continue
                send_frame(conn, {"sub": sub, "msg": {
                    "type": "frame", "frame": "durable", "seq": 70,
                    "event": {"type": "block", "run": 7, "call": 128,
                              "index": 0,
                              "block": {"kind": "text",
                                        "text": "Looking at the ledger now."}}}})
                send_frame(conn, {"sub": sub, "msg": {
                    "type": "frame", "frame": "durable", "seq": 71,
                    "event": {
                        "type": "elicitation_requested",
                        "tool": "AskUserQuestion",
                        "message": "Which ledger do we trust?",
                        "requested_schema": _dumps({
                            "type": "object",
                            "properties": {
                                "q1": {"type": "string", "title": "Which?",
                                       "oneOf": [{"const": "ledger"},
                                                 {"const": "promo"}]}}}),
                        "timeout_ms": 600000}}})
                continue
            if kind == "attach":
                state = {}
                if command.get("session_id") == "turn-local":
                    state["pending_elicitations"] = []
                session_of_sub[sub] = command.get("session_id")
                with _probe_lock:
                    attach_counts[command.get("session_id")] = (
                        attach_counts.get(command.get("session_id"), 0) + 1)
                if command.get("session_id") == "ghost-local":
                    send_frame(conn, {"sub": 0, "msg": {
                        "type": "error", "message": "no such session"}})
                    continue
                if command.get("session_id") == "turn-orphan":
                    state["pending_elicitations"] = []
                    state["child_pending_elicitations"] = [{
                        "session": "ghost-local",
                        "elicitation": {
                            "elicitation": 41,
                            "tool": "AskUserQuestion",
                            "message": "the ghost is asking",
                            "requested_schema": _dumps({
                                "type": "object",
                                "properties": {
                                    "q1": {"type": "string",
                                           "title": "Say why"}}}),
                            "timeout_ms": 600000,
                        },
                    }]
                if command.get("session_id") == "kid-local":
                    state["pending_elicitations"] = [{
                        "elicitation": 41,
                        "tool": "AskUserQuestion",
                        "message": "the child is asking",
                        "requested_schema": _dumps({
                            "type": "object",
                            "properties": {
                                "q1": {"type": "string", "title": "Say why"},
                                "q4": {"type": "string", "title": "A file"}}}),
                        "timeout_ms": 600000,
                        "file_keys": ["q4"],
                    }]
                if command.get("session_id") in ("turn-child", "turn-retract"):
                    state["pending_elicitations"] = []
                    state["child_pending_elicitations"] = [
                        {
                            "session": "kid-local",
                            "elicitation": {
                                "elicitation": 41,
                                "tool": "AskUserQuestion",
                                "message": "the child is asking",
                                "requested_schema": _dumps({
                                    "type": "object",
                                    "properties": {
                                        "q1": {"type": "string",
                                               "title": "Say why"},
                                        "q4": {"type": "string",
                                               "title": "A file"}}}),
                                "timeout_ms": 600000,
                            },
                        },
                        {
                            "session": "kid-local",
                            "elicitation": {
                                "elicitation": 42,
                                "tool": "AskUserQuestion",
                                "message": "and asking again",
                                "requested_schema": "",
                                "timeout_ms": 600000,
                            },
                        },
                    ]
                if command.get("session_id") == "turn-settled":
                    state["pending_elicitations"] = [{
                        "elicitation": 71,
                        "tool": "AskUserQuestion",
                        "message": "Which ledger do we trust?",
                        "requested_schema": "",
                        "timeout_ms": 600000,
                    }]
                if command.get("session_id") == "ask-local":
                    state["pending_elicitations"] = [{
                        "elicitation": 41,
                        "tool": "AskUserRichForm",
                        "message": "Which ledger do we trust?",
                        "requested_schema": json.dumps({
                            "type": "object",
                            "properties": {
                                "q1": {
                                    "type": "string",
                                    "title": "Which mismatch?",
                                    "oneOf": [{"const": "ledger"},
                                              {"const": "promo"}],
                                },
                                "q1_other": {"type": "string",
                                             "title": "Other"},
                                "q2": {
                                    "type": "array",
                                    "title": "What may I touch?",
                                    "items": {"anyOf": [{"const": "rows"},
                                                        {"const": "credits"}]},
                                },
                                "q3": {"type": "string", "title": "Notes"},
                                "q4": {"type": "string", "title": "Approval"},
                            },
                        }),
                        "file_keys": ["q4"],
                        "timeout_ms": 600000,
                    }]
                    state["child_pending_elicitations"] = [{
                        "session": "kid-local",
                        "elicitation": {
                            "elicitation": 41,
                            "tool": "AskUserQuestion",
                            "message": "A child is asking too",
                            "requested_schema": "",
                        },
                    }]
                msg = {"type": "hello", "capabilities": ["fork_with_prompt_v1"],
                       "state": state}
            elif kind == "resolve_elicitation":
                require(command.get("session") != "gone-local", command)
                require(command.get("elicitation") != 77, command)
                require(command["elicitation"] == 41, command)
                if command.get("session") == "kid-local":
                    require(command["action"] == "decline", command)
                    require("content" not in command, command)
                    send_frame(conn, {"sub": sub, "msg": {
                        "type": "frame", "frame": "durable", "seq": 91,
                        "event": {"type": "child_elicitation_update",
                                  "session": "kid-local", "elicitation": 41,
                                  "cause": 55}}})
                    continue
                require(command["action"] == "accept", command)
                require("session" not in command, command)
                require(command["content"] == {
                    "q1": "promo",
                    "q1_other": "or the bank feed",
                    "q2": ["credits", "rows"],
                    "q3": "check the promo ledger first",
                }, command["content"])
                send_frame(conn, {"sub": sub, "msg": {
                    "type": "frame", "frame": "durable", "seq": 90,
                    "event": {"type": "elicitation_resolved",
                              "elicitation": 41, "action": "accept",
                              "content": json.dumps(command["content"]),
                              "by": {"by": "user"}}}})
                continue
            elif kind == "page":
                if session_of_sub.get(sub) == "resume-local":
                    with _probe_lock:
                        page_requests.append({"before": command.get("before"),
                                              "limit": command.get("limit")})
                    msg = resume_page(command)
                else:
                    msg = {"type": "page", "frames": [], "done": True}
            elif kind == "probe_pages":
                with _probe_lock:
                    requests = list(page_requests)
                    page_requests.clear()
                msg = {"type": "probe_pages", "requests": requests}
            elif kind == "create":
                require(command.get("title") == "start with evidence", command)
                require("input" not in command, command)
                msg = {"type": "created",
                       "session": {"session_id": "created-attachment-local"}}
            elif kind == "fork_with_prompt":
                require(command["source_session_id"] == "source-local", 'command["source_session_id"] == "source-local"')
                require(command["prompt"] == "why local?", 'command["prompt"] == "why local?"')
                require(command["title"] == "BTW: why local?", 'command["title"] == "BTW: why local?"')
                require("input" not in command, '"input" not in command')
                msg = {"type": "created", "session": {"session_id": "fork-local"}}
            elif kind == "fork":
                require(command["source_session_id"] == "source-local", 'command["source_session_id"] == "source-local"')
                require("before_seq" not in command, '"before_seq" not in command')
                msg = {"type": "created", "session": {"session_id": "fork-bare"}}
            elif kind == "probe_count":
                with _probe_lock:
                    count = attach_counts.get(command.get("session"), 0)
                msg = {"type": "probe_count",
                       "session": command.get("session"), "count": count}
            elif kind == "list":
                # An unsolicited frame ahead of the reply. round_trip used to
                # latch the FIRST message off the socket, so this frame WAS
                # the answer to list -- and both forks failed the same way.
                send_frame(conn, {"sub": 1, "msg": {
                    "type": "frame", "frame": "delta", "seq": 1,
                    "event": {"type": "model_call_started", "call": 1}}})
                msg = {"type": "sessions", "sessions": [
                    {"session_id": "source-local", "last_seq": 2},
                    {"session_id": "child-local", "parent": "source-local", "last_seq": 1, "running": True},
                ]}
            else:
                msg = {"type": "error", "message": f"unexpected {kind}"}
            send_frame(conn, {"sub": sub, "msg": msg})
            if kind == "attach" and command.get("session_id") == "attachment-local":
                require(attachment_received.wait(timeout=10),
                        "attachment HTTP route was never called")
                send_frame(conn, {"sub": sub, "msg": {
                    "type": "frame", "frame": "durable", "seq": 81,
                    "event": {"type": "user_input", "text": "inspect both files",
                              "files": [
                                  {"media_type": "image/png", "file_id": "9001",
                                   "name": "tiny.png"},
                                  {"media_type": "text/markdown", "file_id": "9002",
                                   "name": "notes.md"},
                              ]}}})
                send_frame(conn, {"sub": sub, "msg": {
                    "type": "frame", "frame": "durable", "seq": 82,
                    "event": {"type": "block", "run": 8, "call": 129,
                              "index": 0,
                              "block": {"kind": "text",
                                        "text": "Both attachments arrived."}}}})
                send_frame(conn, {"sub": sub, "msg": {
                    "type": "frame", "frame": "durable", "seq": 83,
                    "event": {"type": "run_finished"}}})
    return True


def _stop(_signum, _frame):
    raise KeyboardInterrupt


def _accept_loop(listener):
    idle_window = 5
    served = 0
    while True:
        if serve_connection(listener) is False:
            break
        served += 1
        listener.settimeout(idle_window)
    return served


def _report(served):
    if served == 0:
        raise SystemExit("no client ever connected")
    if "/sessions/attachment-local/messages" not in posted_targets:
        _thread_errors.append("attachment HTTP route was never called")
    for thread in list(_served_threads):
        thread.join(timeout=45)
    for text in _thread_errors:
        print("[harness] PROTOCOL CHECK FAILED:\n" + text, file=sys.stderr)
    for text in _other_errors:
        print("[harness] UNEXPECTED SERVER-THREAD ERROR:\n" + text,
              file=sys.stderr)
    if _thread_errors or _other_errors:
        raise SystemExit(
            f"{len(_thread_errors)} protocol check(s) and "
            f"{len(_other_errors)} unexpected error(s) on connection threads")


def main():
    signal.signal(signal.SIGTERM, _stop)
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-file", required=True)
    args = parser.parse_args()
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    with open(args.port_file, "w") as out:
        out.write(str(listener.getsockname()[1]))
    listener.settimeout(30)
    served = 0
    try:
        served = _accept_loop(listener)
    except KeyboardInterrupt:
        served = len(_served_threads)
    _report(served)


if __name__ == "__main__":
    main()
