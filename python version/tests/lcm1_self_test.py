#!/usr/bin/env python3
"""Small standalone test for Local Call LCM1 media chunking."""
import struct

MAGIC = b"LCM1"
HEADER_SIZE = 16
CHUNK_PAYLOAD_SIZE = 16 * 1024


def chunk(frame: bytes, tag: bytes, frame_id: int = 1):
    count = max(1, (len(frame) + CHUNK_PAYLOAD_SIZE - 1) // CHUNK_PAYLOAD_SIZE)
    out = []
    for index in range(count):
        payload = frame[index * CHUNK_PAYLOAD_SIZE : (index + 1) * CHUNK_PAYLOAD_SIZE]
        out.append(MAGIC + tag + b"\x01" + struct.pack("!HHIH", index, count, frame_id, len(payload)) + payload)
    return out


def assemble(packets, tag: bytes):
    chunks = {}
    expected = None
    for packet in packets:
        assert packet[:4] == MAGIC
        assert packet[4:5] == tag
        assert packet[5] == 1
        index, count, frame_id, length = struct.unpack("!HHIH", packet[6:16])
        assert len(packet) == HEADER_SIZE + length
        expected = count
        chunks[index] = packet[HEADER_SIZE:]
    return b"".join(chunks[i] for i in range(expected))


if __name__ == "__main__":
    payload = bytes(range(251)) * 300
    packets = chunk(payload, b"V")
    assert assemble(packets, b"V") == payload
    print(f"LCM1 self-test passed: {len(payload)} bytes in {len(packets)} chunk(s)")
