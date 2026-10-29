#!/usr/bin/env python3
"""Cross-checks src/mesh_codec.cpp against the official Meshtastic protobufs.

Needs: pip install meshtastic cryptography, plus g++ and OpenSSL headers.
Run from the project directory: python3 test/verify_codec.py
"""
import base64
import os
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from meshtastic.protobuf import mesh_pb2, mqtt_pb2, portnums_pb2

DEFAULT_KEY = base64.b64decode("1PG7OiApB1nwvP+rz05pAQ==")
HERE = os.path.dirname(os.path.abspath(__file__))


def expand_psk(psk_b64):
    k = base64.b64decode(psk_b64)
    if len(k) == 1:
        return b"" if k[0] == 0 else DEFAULT_KEY[:15] + bytes([(DEFAULT_KEY[15] + k[0] - 1) & 0xFF])
    return k


def crypt(key, packet_id, sender, data):
    if not key:
        return data
    nonce = packet_id.to_bytes(8, "little") + sender.to_bytes(4, "little") + b"\0" * 4
    c = Cipher(algorithms.AES(key), modes.CTR(nonce)).encryptor()
    return c.update(data) + c.finalize()


def channel_hash(name, key):
    h = 0
    for b in name.encode() + key:
        h ^= b
    return h


def run(binary, *args):
    out = subprocess.run([binary, *args], check=True, capture_output=True, text=True).stdout
    print("   ", out.strip().replace("\n", "\n    "))
    return out


def check(binary, tmp, channel, psk):
    print(f"channel={channel} psk={psk}")
    key = expand_psk(psk)
    path = os.path.join(tmp, "env.bin")

    # C++ -> Python
    out = run(binary, "encode", path, channel, psk)
    assert f"hash={channel_hash(channel, key)}" in out
    env = mqtt_pb2.ServiceEnvelope.FromString(open(path, "rb").read())
    pkt = env.packet
    assert env.channel_id == channel and env.gateway_id == "!1234abcd"
    assert pkt.channel == channel_hash(channel, key)
    data = mesh_pb2.Data.FromString(crypt(key, pkt.id, getattr(pkt, "from"), pkt.encrypted))
    assert data.portnum == portnums_pb2.TEXT_MESSAGE_APP
    assert data.payload.decode().startswith("R12: hello") and data.bitfield == 1
    assert pkt.hop_limit == 3 and pkt.hop_start == 3 and pkt.to == 0xFFFFFFFF

    # Python -> C++, a NodeInfo like a real node sends
    user = mesh_pb2.User(id="!a1b2c3d4", long_name="Alice Node", short_name="ALC",
                         hw_model=mesh_pb2.HardwareModel.HELTEC_V3)
    data = mesh_pb2.Data(portnum=portnums_pb2.NODEINFO_APP, payload=user.SerializeToString(), bitfield=1)
    pkt = mesh_pb2.MeshPacket(to=0xFFFFFFFF, id=0x01020304, hop_limit=2, hop_start=3,
                              channel=channel_hash(channel, key), rx_time=1700000000, rx_snr=7.5, rx_rssi=-90)
    setattr(pkt, "from", 0xA1B2C3D4)
    pkt.encrypted = crypt(key, pkt.id, 0xA1B2C3D4, data.SerializeToString())
    env = mqtt_pb2.ServiceEnvelope(packet=pkt, channel_id=channel, gateway_id="!a1b2c3d4")
    open(path, "wb").write(env.SerializeToString())
    out = run(binary, "decode", path, channel, psk)
    assert "result=0 " in out and f"from={0xA1B2C3D4} " in out and "port=4 " in out
    assert "long=Alice Node short=ALC" in out

    # Same packet under the wrong key must not decode as ours
    other = "AQ==" if psk != "AQ==" else "Ag=="
    out = run(binary, "decode", path, channel, other)
    assert "result=0 " not in out


def main():
    with tempfile.TemporaryDirectory() as tmp:
        binary = os.path.join(tmp, "host_codec")
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-o", binary,
                        os.path.join(HERE, "host_codec.cpp"), os.path.join(HERE, "..", "src", "mesh_codec.cpp"),
                        "-lcrypto"], check=True)
        check(binary, tmp, "LongFast", "AQ==")
        check(binary, tmp, "Private", base64.b64encode(os.urandom(32)).decode())
        check(binary, tmp, "Legacy", base64.b64encode(os.urandom(16)).decode())
    print("all codec checks passed")


if __name__ == "__main__":
    sys.exit(main())
