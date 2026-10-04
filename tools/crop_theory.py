"""
crop_theory.py - 与 C(layout.c) / JS(geo.js) 严格同构的 Python 实现。
验收脚本用它计算"理论裁切框"，再和现场端真实截图比对。
"""


def clamp_focus(x, y, eps=1e-4):
    return (min(max(x, eps), 1 - eps),
            min(max(y, eps), 1 - eps))


def resolve_layout(cfg, vp):
    iw, ih = float(cfg["image_w"]), float(cfg["image_h"])
    vw, vh = float(vp["w"]), float(vp["h"])
    if cfg["mode"] == "contain":
        scale = min(vw / iw, vh / ih)
        dw, dh = iw * scale, ih * scale
        crop = {"x": 0.0, "y": 0.0, "w": iw, "h": ih}
        target = {"x": (vw - dw) / 2, "y": (vh - dh) / 2, "w": dw, "h": dh}
        fully = True
    else:
        scale = max(vw / iw, vh / ih)
        dw, dh = iw * scale, ih * scale
        cw, ch = vw / scale, vh / scale
        fx, fy = clamp_focus(cfg.get("focus_x", 0.5), cfg.get("focus_y", 0.5))
        cx, cy = fx * (iw - cw), fy * (ih - ch)
        crop = {"x": cx, "y": cy, "w": cw, "h": ch}
        target = {"x": -cx * scale, "y": -cy * scale, "w": dw, "h": dh}
        fully = dw <= vw + 1e-6 and dh <= vh + 1e-6
    return {"scale": scale, "crop": crop, "target": target,
            "fully_visible": fully, "viewport": {"w": vw, "h": vh}}


def reported_crop(t):
    x, y = round(t["crop"]["x"]), round(t["crop"]["y"])
    w = int(t["crop"]["x"] + t["crop"]["w"] + 0.999999) - x
    h = int(t["crop"]["y"] + t["crop"]["h"] + 0.999999) - y
    return {"x": x, "y": y, "w": w, "h": h}


def logical_to_physical(pt, logical, dpr, rot):
    x, y = pt
    if rot == 90:
        return (dpr * (logical["h"] - y), dpr * x)
    if rot == 180:
        return (dpr * (logical["w"] - x), dpr * (logical["h"] - y))
    if rot == 270:
        return (dpr * y, dpr * (logical["w"] - x))
    return (dpr * x, dpr * y)


def physical_to_logical(pt, logical, dpr, rot):
    # g_physical_to_logical 的逆
    X, Y = pt
    X /= dpr
    Y /= dpr
    if rot == 90:   # X = H-y, Y = x
        return (Y, logical["h"] - X)
    if rot == 180:
        return (logical["w"] - X, logical["h"] - Y)
    if rot == 270:  # X = y, Y = w-x
        return (logical["w"] - Y, X)
    return (X, Y)
