#!/usr/bin/env python3
"""
Minimal WebSocket signaling relay for Local Call Pro secure RTC.

This server only relays signed signaling JSON between peers in the same room.
It does NOT decrypt media. WebRTC media remains endpoint-encrypted with DTLS-SRTP.

Run locally:
    python signaling_server.py --host 0.0.0.0 --port 8765

Then clients use:
    ws://SERVER_IP:8765/ws
"""

from __future__ import annotations

import argparse
import asyncio
import json
import time
from collections import defaultdict
from typing import Dict, Set

from aiohttp import WSMsgType, web

rooms: Dict[str, Set[web.WebSocketResponse]] = defaultdict(set)
peer_meta: Dict[web.WebSocketResponse, dict] = {}


async def ws_handler(request: web.Request) -> web.WebSocketResponse:
    room = request.query.get("room", "default")
    peer_id = request.query.get("peer_id", "unknown")
    name = request.query.get("name", peer_id)

    ws = web.WebSocketResponse(heartbeat=20, max_msg_size=8 * 1024 * 1024)
    await ws.prepare(request)

    rooms[room].add(ws)
    peer_meta[ws] = {"room": room, "peer_id": peer_id, "name": name, "joined": int(time.time() * 1000)}

    await broadcast(room, {"type": "server_peer_joined", "peer_id": peer_id, "name": name, "ts": int(time.time() * 1000)}, exclude=ws)

    try:
        async for event in ws:
            if event.type == WSMsgType.TEXT:
                try:
                    payload = json.loads(event.data)
                except Exception:
                    continue
                # Relay only; authentication is enforced by clients using Ed25519 signatures.
                await broadcast(room, payload, exclude=ws)
            elif event.type == WSMsgType.ERROR:
                break
    finally:
        rooms[room].discard(ws)
        peer_meta.pop(ws, None)
        await broadcast(room, {"type": "server_peer_left", "peer_id": peer_id, "name": name, "ts": int(time.time() * 1000)}, exclude=ws)
    return ws


async def broadcast(room: str, payload: dict, exclude: web.WebSocketResponse | None = None) -> None:
    dead = []
    message = json.dumps(payload, separators=(",", ":"))
    for ws in list(rooms.get(room, set())):
        if ws is exclude:
            continue
        try:
            await ws.send_str(message)
        except Exception:
            dead.append(ws)
    for ws in dead:
        rooms[room].discard(ws)
        peer_meta.pop(ws, None)


async def health(_: web.Request) -> web.Response:
    return web.json_response({"ok": True, "rooms": {k: len(v) for k, v in rooms.items()}})


def main() -> None:
    parser = argparse.ArgumentParser(description="Local Call Pro WebSocket signaling relay")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    app = web.Application()
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/health", health)
    web.run_app(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
