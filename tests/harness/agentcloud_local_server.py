#!/usr/bin/env python3
import argparse
import base64
import hashlib
import json
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
    conn, _ = listener.accept()
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
                return
            sub = envelope.get("sub", 0)
            command = envelope.get("payload", {})
            kind = command.get("cmd")
            if kind == "attach":
                msg = {"type": "hello", "capabilities": ["fork_with_prompt_v1"], "state": {}}
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
    for _ in range(4):
        serve_connection(listener)


if __name__ == "__main__":
    main()
