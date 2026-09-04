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
        headers = {}
        for line in request.decode().split("\r\n")[1:]:
            if ":" in line:
                key, value = line.split(":", 1)
                headers[key.lower()] = value.strip()
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
                    "event": {"type": "block", "index": 0,
                              "kind": {"kind": "text"},
                              "text": "Looking at the ledger now."}}})
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
                msg = {"type": "page", "frames": [], "done": True}
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
            elif kind == "list":
                msg = {"type": "sessions", "sessions": [
                    {"session_id": "source-local", "last_seq": 2},
                    {"session_id": "child-local", "parent": "source-local", "last_seq": 1, "running": True},
                ]}
            else:
                msg = {"type": "error", "message": f"unexpected {kind}"}
            send_frame(conn, {"sub": sub, "msg": msg})
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
        if all(not t.is_alive() for t in _served_threads):
            break
    return served


def _report(served):
    if served == 0:
        raise SystemExit("no client ever connected")
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
