#!/usr/bin/env python3
"""
Hanabi local mock server — serves the REST + SSE shape hanabi's http adapter
expects, so the app can be exercised fully offline (list, transcripts,
pagination, live events, and SENDING messages) without the real backend.

Language: Python 3 stdlib only (http.server + threading). No install needed.

Point hanabi at it (local http, so a NON-TLS build works):

    HANABI_BACKEND=http \
    HANABI_API_BASE_URL=http://127.0.0.1:8787/api/v1 \
    HANABI_TOKEN=dev \
    HANABI_CHAT_PATH=/chat \
    HANABI_STREAM_PATH=/sessions/{id}/stream \
    ./output/hanabi.exe

Endpoints (all under the /api/v1 prefix baked into HANABI_API_BASE_URL):
    GET  /sessions                     -> {"sessions":[...],"hasMore":bool}   (?limit=N)
    GET  /sessions/{id}/messages       -> {"messages":[...],...}              (?limit=N -> newest N)
    GET  /workspaces                   -> {"workspaces":[...]}
    GET  /sessions/{id}/events         -> SSE (connected frame + activity frames on send)
    POST /chat                         -> send: {session_id?,prompt} -> appends + returns messages
    POST /sessions/{id}/stream         -> SSE token-by-token canned reply (used by HANABI_STREAM_PATH)

Everything is deterministic so it's testable.
"""

import argparse
import json
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# ---------------------------------------------------------------------------
# In-memory store (seeded below). Guarded by a lock; the SSE event bus lets a
# send() on one request wake up the /events stream on another connection.
# ---------------------------------------------------------------------------

NOW = int(time.time())


def ago(seconds):
    return NOW - seconds


LOCK = threading.RLock()

# event bus: session_id -> list of threading.Event-wrapped queues.
# Simpler: each live /events subscriber registers a Condition + a list it
# drains. We push (sessionId, event_dict) frames to every subscriber of that id.
SUBSCRIBERS = {}  # session_id -> list[Subscriber]


class Subscriber:
    def __init__(self, session_id):
        self.session_id = session_id
        self.cond = threading.Condition()
        self.frames = []  # list of dict frames to emit as data: {json}
        self.closed = False

    def push(self, frame):
        with self.cond:
            self.frames.append(frame)
            self.cond.notify_all()

    def drain(self, timeout):
        with self.cond:
            if not self.frames:
                self.cond.wait(timeout=timeout)
            out = self.frames
            self.frames = []
            return out


def subscribe(session_id):
    with LOCK:
        s = Subscriber(session_id)
        SUBSCRIBERS.setdefault(session_id, []).append(s)
        return s


def unsubscribe(sub):
    with LOCK:
        lst = SUBSCRIBERS.get(sub.session_id, [])
        if sub in lst:
            lst.remove(sub)


def emit_event(session_id, event_type, extra=None):
    """Push a nested activity frame {sessionId,event:{type,...},ts} to subscribers."""
    ev = {"type": event_type}
    if extra:
        ev.update(extra)
    frame = {"sessionId": session_id, "event": ev, "ts": int(time.time())}
    with LOCK:
        for s in list(SUBSCRIBERS.get(session_id, [])):
            s.push(frame)


# ---------------------------------------------------------------------------
# Seed data. Shapes mirror the real backend (verified against src/api).
#   session: id,title,status,updatedAt,createdAt,isPinned,workspace,workspaceId,
#            channelType,isProcessing,isSubSession,parentSessionId,model,
#            subSessionStatus
#   message: id,role("user"|"assistant"),createdAt,blocks:[...]
#   block:   {"type":"text","content":...}
#            {"type":"tool_call","toolCall":{id,name,inputs,startedAt}}
#            {"type":"tool_result","toolResult":{output,status,toolCallId,completedAt}}
# ---------------------------------------------------------------------------


def txt(content):
    return {"type": "text", "content": content}


def tool_call(cid, name, inputs, started):
    return {"type": "tool_call",
            "toolCall": {"id": cid, "name": name, "inputs": inputs,
                         "startedAt": started}}


def tool_result(cid, output, status, completed):
    return {"type": "tool_result",
            "toolResult": {"output": output, "status": status,
                           "toolCallId": cid, "completedAt": completed}}


def msg(mid, role, created, blocks):
    return {"id": mid, "role": role, "createdAt": created, "blocks": blocks}


def new_session(sid, title, status="active", updated=None, created=None,
                pinned=False, workspace=None, workspace_id=None,
                processing=False, model="claude-opus", sub=False,
                parent=None, sub_status=None):
    return {
        "id": sid,
        "title": title,
        "status": status,
        "updatedAt": updated if updated is not None else ago(3600),
        "createdAt": created if created is not None else ago(86400),
        "isPinned": pinned,
        "workspace": workspace,
        "workspaceId": workspace_id,
        "channelType": "chat",
        "isProcessing": processing,
        "isSubSession": sub,
        "parentSessionId": parent,
        "model": model,
        "subSessionStatus": sub_status,
    }


WORKSPACES = [
    {"id": "ws-subs", "name": "Subscriptions", "description": "Subscriptions work",
     "appearance": {"color": "#5B8DEF", "emoji": "📊"},
     "createdAt": ago(30 * 86400), "updatedAt": ago(2 * 3600)},
    {"id": "ws-stars", "name": "Stars", "description": "Creator Stars",
     "appearance": {"color": "#F5A623", "emoji": "⭐"},
     "createdAt": ago(60 * 86400), "updatedAt": ago(5 * 3600)},
]

# --- Sessions + transcripts -------------------------------------------------
SESSIONS = {}
MESSAGES = {}  # session_id -> list[message] (ascending createdAt)


def seed():
    # 1) A rich session WITH tool_call / tool_result blocks, in a workspace.
    s1 = new_session("s-tools", "Investigate Stars payout discrepancy",
                     updated=ago(600), created=ago(7200), pinned=True,
                     workspace="Stars", workspace_id="ws-stars")
    SESSIONS[s1["id"]] = s1
    MESSAGES[s1["id"]] = [
        msg("m1", "user", ago(7200),
            [txt("The Stars payout total looks off for creator 12345. Can you dig in?")]),
        msg("m2", "assistant", ago(7100), [
            txt("Let me pull the payout ledger for that creator."),
            tool_call("tc1", "run_query",
                      {"command": "SELECT sum(amount) FROM star_payouts WHERE creator_id=12345",
                       "node": "devvm"},
                      ago(7099)),
            tool_result("tc1",
                        "sum(amount)\n--------\n4821.50",
                        "success", ago(7095)),
            txt("The ledger sums to $4,821.50. Now comparing against the reported total."),
            tool_call("tc2", "read_file",
                      {"command": "cat /var/reports/creator_12345.json", "node": "devvm"},
                      ago(7094)),
            tool_result("tc2",
                        '{"reported_total": 4900.00}',
                        "success", ago(7090)),
            txt("Found it: reported total is $4,900.00 but the ledger is $4,821.50 — "
                "a $78.50 gap. Likely an unreconciled refund. Want me to trace the refunds?"),
        ]),
        msg("m3", "user", ago(660),
            [txt("Yes, trace the refunds.")]),
    ]

    # 2) A LONG thread for pagination / newest-N testing (30 messages).
    s2 = new_session("s-long", "Multi-tier subscriptions design review",
                     updated=ago(300), created=ago(50000),
                     workspace="Subscriptions", workspace_id="ws-subs")
    SESSIONS[s2["id"]] = s2
    long_msgs = []
    for i in range(30):
        role = "user" if i % 2 == 0 else "assistant"
        created = ago(50000 - i * 1500)
        if role == "user":
            body = f"[turn {i}] Question about tier {i // 2 + 1}: how do we handle proration?"
        else:
            body = f"[turn {i}] For tier {i // 2 + 1}, proration is computed daily and credited on the next cycle."
        long_msgs.append(msg(f"lm{i}", role, created, [txt(body)]))
    MESSAGES[s2["id"]] = long_msgs

    # 3) A couple of short unfoldered sessions.
    s3 = new_session("s-short1", "Fix flaky subs_exploration test",
                     updated=ago(1800), created=ago(9000), model="claude-sonnet")
    SESSIONS[s3["id"]] = s3
    MESSAGES[s3["id"]] = [
        msg("s3m1", "user", ago(9000), [txt("The test_subscription_renewal test is flaky. Fix it.")]),
        msg("s3m2", "assistant", ago(8900),
            [txt("It's a time-of-day dependency. I'll freeze the clock in the fixture. Done — it passes 100/100 now.")]),
    ]

    s4 = new_session("s-short2", "[P] Waiting on your call: deprecate v1 API?",
                     status="active", updated=ago(120), created=ago(4000))
    SESSIONS[s4["id"]] = s4
    MESSAGES[s4["id"]] = [
        msg("s4m1", "user", ago(4000), [txt("Should we deprecate the v1 subscriptions API?")]),
        msg("s4m2", "assistant", ago(3900),
            [txt("Pros: less maintenance. Cons: 3 partners still on it. I need your call before proceeding.")]),
    ]

    # 4) A processing (running) session, and an archived one.
    s5 = new_session("s-running", "Running: backfill payout reconciliation",
                     updated=ago(30), created=ago(600), processing=True,
                     workspace="Stars", workspace_id="ws-stars",
                     sub_status="running")
    SESSIONS[s5["id"]] = s5
    MESSAGES[s5["id"]] = [
        msg("s5m1", "user", ago(600), [txt("Backfill the reconciliation for last quarter.")]),
        msg("s5m2", "assistant", ago(590), [txt("Working on it — processing ~40k rows.")]),
    ]

    s6 = new_session("s-arch", "Old spike: donation UI prototype",
                     status="archived", updated=ago(20 * 86400),
                     created=ago(25 * 86400))
    SESSIONS[s6["id"]] = s6
    MESSAGES[s6["id"]] = [
        msg("s6m1", "user", ago(25 * 86400), [txt("Prototype a donation UI.")]),
        msg("s6m2", "assistant", ago(25 * 86400 - 100), [txt("Here's a prototype. Shipped and archived.")]),
    ]


seed()


def session_list_sorted():
    # newest activity first (the adapter also sorts, but match reality).
    return sorted(SESSIONS.values(), key=lambda s: s["updatedAt"], reverse=True)


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

LATENCY_MS = 0


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        # Keep the log terse but present.
        print("[mock] %s - %s" % (self.address_string(), fmt % args))

    # -- helpers ------------------------------------------------------------
    def _delay(self):
        if LATENCY_MS > 0:
            time.sleep(LATENCY_MS / 1000.0)

    def _json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _sse_start(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        # No Content-Length: keep the stream open.
        self.end_headers()

    def _sse_send(self, data_obj):
        frame = "data: " + json.dumps(data_obj) + "\n\n"
        self.wfile.write(frame.encode("utf-8"))
        self.wfile.flush()

    def _strip_prefix(self, path):
        # Accept either /api/v1/... or bare /... so the server is forgiving.
        for pfx in ("/api/v1", "/api"):
            if path.startswith(pfx + "/") or path == pfx:
                return path[len(pfx):] or "/"
        return path

    # -- routing ------------------------------------------------------------
    def do_GET(self):
        parsed = urlparse(self.path)
        path = self._strip_prefix(parsed.path)
        qs = parse_qs(parsed.query)
        self._delay()

        if path == "/sessions":
            return self.handle_sessions(qs)
        if path == "/workspaces":
            return self.handle_workspaces()
        # /sessions/{id}/messages
        parts = [p for p in path.split("/") if p]
        if len(parts) == 3 and parts[0] == "sessions" and parts[2] == "messages":
            return self.handle_messages(parts[1], qs)
        if len(parts) == 3 and parts[0] == "sessions" and parts[2] == "events":
            return self.handle_events(parts[1])
        return self._json({"error": "not found", "path": path}, status=404)

    def do_POST(self):
        parsed = urlparse(self.path)
        path = self._strip_prefix(parsed.path)
        self._delay()

        length = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            body = json.loads(raw) if raw else {}
        except Exception:
            body = {}

        parts = [p for p in path.split("/") if p]
        # streaming reply: POST /sessions/{id}/stream
        if len(parts) == 3 and parts[0] == "sessions" and parts[2] == "stream":
            return self.handle_stream(parts[1], body)
        # send (kickoff or reply): POST /chat
        if path == "/chat":
            return self.handle_chat(body)
        return self._json({"error": "not found", "path": path}, status=404)

    # -- GET /sessions ------------------------------------------------------
    def handle_sessions(self, qs):
        with LOCK:
            items = session_list_sorted()
        limit = _int(qs.get("limit"))
        has_more = False
        if limit and limit < len(items):
            items = items[:limit]
            has_more = True
        return self._json({"sessions": items, "hasMore": has_more})

    # -- GET /workspaces ----------------------------------------------------
    def handle_workspaces(self):
        return self._json({"workspaces": WORKSPACES})

    # -- GET /sessions/{id}/messages ---------------------------------------
    def handle_messages(self, sid, qs):
        with LOCK:
            sess = SESSIONS.get(sid)
            all_msgs = list(MESSAGES.get(sid, []))
        if sess is None:
            return self._json({"error": "session not found", "id": sid}, status=404)
        limit = _int(qs.get("limit"))
        has_more = False
        window = all_msgs
        if limit and limit < len(all_msgs):
            # Newest N, still ASCENDING within the window.
            window = all_msgs[-limit:]
            has_more = True  # older ones exist beyond the window
        return self._json({
            "messages": window,
            "title": sess["title"],
            "model": sess["model"],
            "workspaceId": sess["workspaceId"],
            "workspace": sess["workspace"],
            "isStreaming": sess["isProcessing"],
            "hasMore": has_more,
        })

    # -- GET /sessions/{id}/events (SSE) ------------------------------------
    def handle_events(self, sid):
        self._sse_start()
        # First frame: top-level {type:"connected"} (adapter ignores it).
        try:
            self._sse_send({"type": "connected", "sessionId": sid,
                            "ts": int(time.time())})
        except (BrokenPipeError, ConnectionResetError):
            return
        sub = subscribe(sid)
        try:
            # Emit a session_start activity so a fresh subscriber sees one event.
            emit_event(sid, "session_start")
            last_ping = time.time()
            while True:
                frames = sub.drain(timeout=1.0)
                for f in frames:
                    self._sse_send(f)
                # Heartbeat comment every ~15s so proxies keep the stream open
                # (a ":" line is a comment the adapter ignores).
                now = time.time()
                if now - last_ping > 15:
                    self.wfile.write(b": keep-alive\n\n")
                    self.wfile.flush()
                    last_ping = now
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            unsubscribe(sub)

    # -- POST /chat (send: kickoff or reply) --------------------------------
    def handle_chat(self, body):
        prompt = body.get("prompt", "")
        session_id = body.get("session_id") or body.get("sessionId")

        if not session_id:
            # KICKOFF: create a new session, seed with the user prompt.
            sid = "s-" + uuid.uuid4().hex[:8]
            now = int(time.time())
            with LOCK:
                SESSIONS[sid] = new_session(sid, prompt[:60] or "New task",
                                            updated=now, created=now)
                user_m = msg("u-" + uuid.uuid4().hex[:6], "user", now, [txt(prompt)])
                reply = self._canned_reply(prompt, now + 1)
                MESSAGES[sid] = [user_m, reply]
            emit_event(sid, "session_start")
            emit_event(sid, "message")
            # Kickoff response carries the new session id.
            return self._json({"id": sid, "session": {"id": sid}})

        # REPLY into an existing session.
        now = int(time.time())
        with LOCK:
            if session_id not in SESSIONS:
                return self._json({"error": "session not found", "id": session_id},
                                  status=404)
            user_m = msg("u-" + uuid.uuid4().hex[:6], "user", now, [txt(prompt)])
            reply = self._canned_reply(prompt, now + 1)
            MESSAGES[session_id].append(user_m)
            MESSAGES[session_id].append(reply)
            SESSIONS[session_id]["updatedAt"] = now
        # Fire the SSE activity so any open /events stream refetches.
        emit_event(session_id, "message")
        # Reply response: the assistant message(s). Return both new turns under
        # "messages" (the adapter picks the last assistant one).
        return self._json({"messages": [user_m, reply]})

    # -- POST /sessions/{id}/stream (SSE token-by-token) --------------------
    def handle_stream(self, sid, body):
        prompt = body.get("prompt", "")
        now = int(time.time())
        # Append the user turn immediately.
        with LOCK:
            exists = sid in SESSIONS
            if exists:
                user_m = msg("u-" + uuid.uuid4().hex[:6], "user", now, [txt(prompt)])
                MESSAGES[sid].append(user_m)
        self._sse_start()
        reply_text = self._canned_reply_text(prompt)
        # Optional title_update on the first streamed reply of a session.
        try:
            # Stream the reply token-by-token (word by word here, deterministic).
            for word in reply_text.split(" "):
                self._sse_send({"type": "text", "text": word + " "})
                time.sleep(0.02)
            self._sse_send({"type": "done"})
        except (BrokenPipeError, ConnectionResetError):
            return
        # Persist the assistant turn + fire the activity event.
        with LOCK:
            if exists:
                MESSAGES[sid].append(msg("a-" + uuid.uuid4().hex[:6], "assistant",
                                         int(time.time()), [txt(reply_text)]))
                SESSIONS[sid]["updatedAt"] = int(time.time())
        emit_event(sid, "message")

    # -- canned deterministic assistant reply -------------------------------
    def _canned_reply_text(self, prompt):
        return ("Mock reply: I received \"%s\". This is a canned response from "
                "the local mock server." % (prompt.strip()[:120]))

    def _canned_reply(self, prompt, created):
        return msg("a-" + uuid.uuid4().hex[:6], "assistant", created,
                   [txt(self._canned_reply_text(prompt))])


def _int(v):
    if not v:
        return None
    try:
        return int(v[0]) if isinstance(v, list) else int(v)
    except (ValueError, TypeError):
        return None


def main():
    global LATENCY_MS
    ap = argparse.ArgumentParser(description="Hanabi local mock server")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--latency-ms", type=int, default=0,
                    help="artificial per-request latency to test loading spinners")
    args = ap.parse_args()
    LATENCY_MS = args.latency_ms

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    print("Hanabi mock server on http://%s:%d/api/v1  (latency=%dms)"
          % (args.host, args.port, LATENCY_MS))
    print("  sessions: %d, workspaces: %d" % (len(SESSIONS), len(WORKSPACES)))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
