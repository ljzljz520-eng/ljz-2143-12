# API 与数据结构

## LayoutConfig

```json
{
  "image_id": "background.png",
  "image_w": 1600,
  "image_h": 1000,
  "mode": "cover",            // cover | contain
  "focus_x": 0.5,             // [0.02, 0.98]（内部会夹取）
  "focus_y": 0.5,
  "rotation": 0,              // 0 | 90 | 180 | 270
  "dpr_override": 0.0         // 0 = 自动；>0 强制
}
```

## GET /api/layout/latest → 200

```json
{ "version": 3, "updated_at": "...", "checksum": "ab12…", "config": { … } }
```

## PUT /api/layout

请求：`{"base_version": 3, "client": "web-admin|c-display", "config": { … }}`

- `200`：返回新的 latest（version=4）。
- `409`：`{"error":"version conflict…","current_version":4,"current":{…}}`
  —— 本地选择不会被修改，由用户决定加载远程或强制保存。
- `400`：配置非法（焦点越界、图片不存在、mode 非法等）。

## POST /api/reports/crop

```json
{
  "device_id": "display-01",
  "image_id": "img-x.png",
  "mode": "cover",
  "crop": { "x": 300, "y": 0, "w": 1000, "h": 1000 },
  "viewport_logical": { "w": 1000, "h": 1000 },
  "dpr": 2.0,
  "rotation": 0,
  "client": "c-display|web-display"
}
```

## POST /api/images/publish

`multipart/form-data`，字段 `image`（.png/.jpg）。成功：

```json
{ "version": 5, "config": { "image_id": "img-xxxx.png", "image_w": …, "image_h": …, … } }
```

落盘 `assets/img-<uuid>.<ext>`：`.part` 写完 fsync 后 rename，发布期间
展示端只会看到旧图或新图。
