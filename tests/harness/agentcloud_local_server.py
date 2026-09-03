#!/usr/bin/env python3
import argparse
import base64
import hashlib
import json


session_of_sub = {}


def _dumps(v):
    return json.dumps(v, separators=(",", ":"))
import socket
import struct


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
        while True:
            try:
                envelope = read_frame(conn)
            except EOFError:
                return True
            sub = envelope.get("sub", 0)
            command = envelope.get("payload", {})
            kind = command.get("cmd")
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
                if command.get("session_id") in ("turn-child", "turn-retract"):
                    state["pending_elicitations"] = []
                    state["child_pending_elicitations"] = [
                        {
                            "session": "kid-local",
                            "elicitation": {
                                "elicitation": 41,
                                "tool": "AskUserQuestion",
                                "message": "the child is asking",
                                "requested_schema": "",
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
                assert command.get("session") != "gone-local", command
                assert command.get("elicitation") != 77, command
                assert command["elicitation"] == 41, command
                if command.get("session") == "kid-local":
                    assert command["action"] == "decline", command
                    assert "content" not in command, command
                    send_frame(conn, {"sub": sub, "msg": {
                        "type": "frame", "frame": "durable", "seq": 91,
                        "event": {"type": "child_elicitation_update",
                                  "session": "kid-local", "elicitation": 41,
                                  "cause": 55}}})
                    continue
                assert command["action"] == "accept", command
                assert "session" not in command, command
                assert command["content"] == {
                    "q1": "promo",
                    "q1_other": "or the bank feed",
                    "q2": ["credits", "rows"],
                    "q3": "check the promo ledger first",
                }, command["content"]
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
                assert command["source_session_id"] == "source-local"
                assert command["prompt"] == "why local?"
                assert command["title"] == "BTW: why local?"
                assert "input" not in command
                msg = {"type": "created", "session": {"session_id": "fork-local"}}
            elif kind == "fork":
                assert command["source_session_id"] == "source-local"
                assert "before_seq" not in command
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


def main():
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
    while True:
        if serve_connection(listener) is False:
            break
        served += 1
        listener.settimeout(5)
    if served == 0:
        raise SystemExit("no client ever connected")


if __name__ == "__main__":
    main()
