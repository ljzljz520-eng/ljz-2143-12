"""零依赖图片尺寸读取（PNG/JPEG）。"""
import struct


def image_size(path: str):
    with open(path, "rb") as f:
        head = f.read(24)
        if head[:8] == b"\x89PNG\r\n\x1a\n":
            w, h = struct.unpack(">II", head[16:24])
            return int(w), int(h)
        if head[:2] == b"\xff\xd8":  # JPEG
            f.seek(2)
            while True:
                byte = f.read(1)
                while byte and byte != b"\xff":
                    byte = f.read(1)
                while byte == b"\xff":
                    byte = f.read(1)
                if not byte:
                    break
                marker = byte[0]
                if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
                    f.read(3)
                    h, w = struct.unpack(">HH", f.read(4))
                    return int(w), int(h)
                seg = f.read(2)
                if len(seg) != 2:
                    break
                length = struct.unpack(">H", seg)[0]
                f.seek(length - 2, 1)
    raise ValueError(f"unsupported or corrupt image: {path}")
