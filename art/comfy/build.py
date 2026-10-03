#!/usr/bin/env python3
"""Writes grungo_adjust.json (ComfyUI editor format) and grungo_adjust_api.json
(API format) from the one node table below, so the two never drift.

    uv run python art/comfy/build.py      # needs the ComfyUI server: widget order comes from /object_info

Edit the table, not the JSON. run.py addresses nodes by title, so keep titles stable.
"""
import json
import os
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMFY = os.environ.get("COMFY", "http://127.0.0.1:8188")

SUBJECT = ("a stocky green frog villager in a brown hooded cloak and a brown scarf, wide green frog face, "
           "two big round dark red-brown eyes on top of the head, two small eye-stalk antennae, mottled olive green skin")
STYLE = ("front view facing the viewer, cartoon game character illustration, bold black outlines, flat cel shading, "
         "clean shapes, plain flat pure white background")
EDIT = "(a slightly wider gentle closed-mouth frog smile:1.3), calm content expression"
NEGATIVE = ("photorealistic, photo, 3d render, blurry, jpeg artifacts, text, watermark, signature, human, person, "
            "multiple characters, duplicate, cropped, deformed face, extra eyes")

HOW_TO = """GRUNGO ADJUST (blorbarium art/comfy, see README.md)

First time: uv run --with pillow --with numpy python art/comfy/run.py install
That uploads grungo_<frame>.png, grungo_mask_<preset>.png and grungo_alpha.png.

1. Input frame: pick grungo_<frame>.png. To edit your own area, right-click it > Open in MaskEditor and paint; the strokes add to the preset.
2. Mask preset: face / facebox / eyes / mouth / body / canvas / none (paint-only).
3. Write the change in the positive prompt between the subject and the style.
4. Denoise: 0.4 subtle, 0.6 reshapes, 0.8-0.9 redraws the masked area.
5. Queue. 'Adjusted frame' keeps every pixel outside the mask and the anchor alpha, so it drops into pets/grungo/art/frames.
New silhouettes (a pose): preset canvas, then save 'Raw decode' instead; it is not clipped to the body."""

# Columns and groups. Each node: id, class, title, group, (x, y), widgets {name: value}, links {input: (id, slot)}.
GROUPS = {
    "models": ("Models (installed in D:\\comfyshared\\ComfyModels)", "#3f3f3f"),
    "input": ("1. Input frame (paint here to add to the mask)", "#2a4a2a"),
    "identity": ("2. Identity: IP-Adapter on grungo reference frames", "#2a3a5a"),
    "prompt": ("3. Prompts", "#4a3a2a"),
    "mask": ("4. Edit mask (white = regenerate)", "#4a2a4a"),
    "sample": ("5. Sampler: seed, steps, denoise", "#2a4a4a"),
    "output": ("6. Output", "#4a4a2a"),
}

NODES = [
    (99, "Note", "How to use", None, (-420, 0), {"text": HOW_TO}, {}),

    (1, "CheckpointLoaderSimple", "Checkpoint (SDXL Juggernaut XI)", "models", (0, 0),
     {"ckpt_name": "SDXL1.0\\juggernautXL_juggXIByRundiffusion.safetensors"}, {}),
    (40, "IPAdapterModelLoader", "IP-Adapter model", "models", (0, 160),
     {"ipadapter_file": "ip-adapter-plus_sdxl_vit-h.safetensors"}, {}),
    (41, "CLIPVisionLoader", "CLIP vision", "models", (0, 280),
     {"clip_name": "CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors"}, {}),

    (30, "LoadImage", "Input frame", "input", (0, 480), {"image": "grungo_neutral.png"}, {}),

    (42, "LoadImage", "Reference A", "identity", (420, 0), {"image": "grungo_neutral.png"}, {}),
    (45, "LoadImage", "Reference B", "identity", (760, 0), {"image": "grungo_neutral.png"}, {}),
    (46, "BatchImagesNode", "Reference frames", "identity", (1100, 0), {}, {"images.image0": (42, 0), "images.image1": (45, 0)}),
    (43, "PrepImageForClipVision", "Prep references", "identity", (1100, 140),
     {"interpolation": "LANCZOS", "crop_position": "center", "sharpening": 0.0}, {"image": (46, 0)}),
    (44, "IPAdapterAdvanced", "IP-Adapter (identity)", "identity", (1100, 320),
     {"weight": 0.5, "weight_type": "ease in-out", "combine_embeds": "average", "start_at": 0.0, "end_at": 0.6,
      "embeds_scaling": "V only"},
     {"model": (1, 0), "ipadapter": (40, 0), "image": (43, 0), "clip_vision": (41, 0)}),

    (3, "CLIPTextEncode", "Positive prompt", "prompt", (420, 480), {"text": f"{SUBJECT}, {EDIT}, {STYLE}"}, {"clip": (1, 1)}),
    (4, "CLIPTextEncode", "Negative prompt", "prompt", (420, 760), {"text": f"{NEGATIVE}, frown, open mouth"}, {"clip": (1, 1)}),

    (50, "LoadImageMask", "Mask preset", "mask", (900, 600), {"image": "grungo_mask_mouth.png", "channel": "red"}, {}),
    (55, "LoadImageMask", "Body alpha (leave as is)", "mask", (900, 980), {"image": "grungo_alpha.png", "channel": "red"}, {}),
    (56, "MaskComposite", "Painted strokes inside the body", "mask", (1240, 600),
     {"x": 0, "y": 0, "operation": "multiply"}, {"destination": (30, 1), "source": (55, 0)}),
    (57, "MaskComposite", "Preset + painted", "mask", (1240, 780),
     {"x": 0, "y": 0, "operation": "add"}, {"destination": (50, 0), "source": (56, 0)}),
    (51, "GrowMask", "Grow mask", "mask", (1240, 960), {"expand": 8, "tapered_corners": True}, {"mask": (57, 0)}),
    (63, "MaskToImage", "Mask as image", "mask", (1580, 600), {}, {"mask": (57, 0)}),
    (64, "ImageBlur", "Soften paste edge (radius <= grow)", "mask", (1580, 700),
     {"blur_radius": 8, "sigma": 4.0}, {"image": (63, 0)}),
    (65, "ImageToMask", "Soft mask", "mask", (1580, 860), {"channel": "red"}, {"image": (64, 0)}),
    (66, "MaskComposite", "Soft mask inside the grown mask", "mask", (1580, 980),
     {"x": 0, "y": 0, "operation": "multiply"}, {"destination": (51, 0), "source": (65, 0)}),
    (58, "MaskComposite", "Paste mask (inside the body)", "mask", (1240, 1120),
     {"x": 0, "y": 0, "operation": "multiply"}, {"destination": (66, 0), "source": (55, 0)}),
    (59, "MaskToImage", "Mask to image", "mask", (1580, 1160), {}, {"mask": (58, 0)}),
    (60, "PreviewImage", "Mask preview", "mask", (1580, 1260), {}, {"images": (59, 0)}),

    (32, "VAEEncode", "Encode input", "sample", (1960, 0), {}, {"pixels": (30, 0), "vae": (1, 2)}),
    (53, "SetLatentNoiseMask", "Noise only the mask", "sample", (1960, 120), {}, {"samples": (32, 0), "mask": (51, 0)}),
    (6, "KSampler", "Sampler", "sample", (1960, 260),
     {"seed": 1001, "steps": 28, "cfg": 7.0, "sampler_name": "euler", "scheduler": "normal", "denoise": 0.6},
     {"model": (44, 0), "positive": (3, 0), "negative": (4, 0), "latent_image": (53, 0)}),
    (7, "VAEDecode", "Decode", "sample", (1960, 560), {}, {"samples": (6, 0), "vae": (1, 2)}),

    (54, "ImageCompositeMasked", "Paste into input", "output", (2360, 0),
     {"x": 0, "y": 0, "resize_source": False}, {"destination": (30, 0), "source": (7, 0), "mask": (58, 0)}),
    (61, "InvertMask", "Alpha as background mask", "output", (2360, 200), {}, {"mask": (55, 0)}),
    (62, "JoinImageWithAlpha", "Re-attach anchor alpha", "output", (2360, 300), {}, {"image": (54, 0), "alpha": (61, 0)}),
    (12, "SaveImage", "Adjusted frame", "output", (2700, 0), {"filename_prefix": "grungo/adjust"}, {"images": (62, 0)}),
    (13, "PreviewImage", "Raw decode (new silhouettes only)", "output", (2700, 480), {}, {"images": (7, 0)}),
]

FRONTEND_ONLY = {"Note"}
TALL = {"LoadImage": 380, "LoadImageMask": 340, "PreviewImage": 360, "SaveImage": 380, "Note": 520}


def object_info():
    with urllib.request.urlopen(COMFY + "/object_info", timeout=120) as r:
        return json.load(r)


def is_widget(spec):
    t = spec[0]
    return isinstance(t, list) or t in ("INT", "FLOAT", "STRING", "BOOLEAN", "COMBO")


def schema(info, cls):
    """(widget names in order, [(link input name, type)], [(output name, type)], seed widgets)."""
    if cls == "Note":
        return ["text"], [], [], set()
    d = info[cls]
    specs = {**d["input"].get("required", {}), **d["input"].get("optional", {})}
    order = d.get("input_order", {})
    names = order.get("required", []) + order.get("optional", []) or list(specs)
    widgets = [n for n in names if is_widget(specs[n])]
    links = [(n, specs[n][0]) for n in names if not is_widget(specs[n])]
    seeded = {n for n in widgets if len(specs[n]) > 1 and specs[n][1].get("control_after_generate")}
    outs = list(zip(d.get("output_name", d["output"]), d["output"]))
    return widgets, links, outs, seeded


def build(info):
    by_id = {n[0]: n for n in NODES}
    sch = {n[0]: schema(info, n[1]) for n in NODES}
    api, ui_nodes, ui_links = {}, [], []
    out_links = {}
    for nid, cls, title, group, pos, widgets, links in NODES:
        names, link_specs, outs, seeded = sch[nid]
        assert set(widgets) <= set(names), f"{title}: unknown widgets {set(widgets) - set(names)}"
        missing = set(names) - set(widgets)
        assert not missing, f"{title}: set every widget, missing {missing}"
        slots = []
        for name, want in link_specs:
            if want == "COMFY_AUTOGROW_V3":
                slots += [(k, sch[links[k][0]][2][links[k][1]][1]) for k in links if k.startswith(name + ".")]
            else:
                slots.append((name, want))
        assert set(links) <= {n for n, _ in slots}, f"{title}: unknown inputs {set(links) - {n for n, _ in slots}}"
        inputs = []
        for name, want in slots:
            link_id = None
            if name in links:
                src, slot = links[name]
                src_type = sch[src][2][slot][1]
                assert want == src_type, f"{title}.{name}: {src_type} into {want}"
                link_id = len(ui_links) + 1
                ui_links.append([link_id, src, slot, nid, len(inputs), src_type])
                out_links.setdefault((src, slot), []).append(link_id)
            inputs.append({"name": name, "type": want, "link": link_id})
        values = []
        for n in names:
            values.append(widgets[n])
            if n in seeded:
                values.append("fixed")
            if cls == "LoadImage" and n == "image":
                values.append("image")
        h = TALL.get(cls, 60 + 26 * max(len(inputs), len(outs)) + 26 * len(names))
        w = 400 if cls in ("CLIPTextEncode", "Note") else 320
        if cls == "CLIPTextEncode":
            h = 240
        ui_nodes.append({"id": nid, "type": cls, "title": title, "pos": list(pos), "size": [w, h], "flags": {},
                         "order": 0, "mode": 0, "inputs": inputs, "outputs": [], "properties": {"Node name for S&R": cls},
                         "widgets_values": values, "_group": group})
        if cls not in FRONTEND_ONLY:
            api[str(nid)] = {"class_type": cls, "_meta": {"title": title},
                             "inputs": {**widgets, **{k: [str(s), i] for k, (s, i) in links.items()}}}
    for node in ui_nodes:
        outs = sch[node["id"]][2]
        node["outputs"] = [{"name": n, "type": t, "links": out_links.get((node["id"], i), []), "slot_index": i}
                           for i, (n, t) in enumerate(outs)]
    order = topo(by_id)
    for node in ui_nodes:
        node["order"] = order[node["id"]]
    groups = []
    for key, (title, color) in GROUPS.items():
        members = [n for n in ui_nodes if n["_group"] == key]
        x0 = min(n["pos"][0] for n in members) - 20
        y0 = min(n["pos"][1] for n in members) - 60
        x1 = max(n["pos"][0] + n["size"][0] for n in members) + 20
        y1 = max(n["pos"][1] + n["size"][1] for n in members) + 20
        groups.append({"title": title, "bounding": [x0, y0, x1 - x0, y1 - y0], "color": color, "font_size": 24})
    for n in ui_nodes:
        del n["_group"]
    ui = {"last_node_id": max(by_id), "last_link_id": len(ui_links), "nodes": ui_nodes, "links": ui_links,
          "groups": groups, "config": {}, "extra": {"ds": {"scale": 0.55, "offset": [460, 80]}}, "version": 0.4}
    return ui, api


def topo(by_id):
    order, seen = {}, set()

    def visit(nid):
        if nid in seen:
            return
        seen.add(nid)
        for src, _ in by_id[nid][6].values():
            visit(src)
        order[nid] = len(order)

    for nid in by_id:
        visit(nid)
    return order


def main():
    ui, api = build(object_info())
    for name, doc in (("grungo_adjust.json", ui), ("grungo_adjust_api.json", api)):
        (HERE / name).write_text(json.dumps(doc, indent=1) + "\n", newline="\n")
        print("wrote", HERE / name)


if __name__ == "__main__":
    main()
