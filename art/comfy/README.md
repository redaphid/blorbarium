# Adjust grungo in ComfyUI

`grungo_adjust.json` repaints part of one of grungo's frames and leaves every other pixel alone. You pick a frame, a mask, and a prompt. The result keeps the anchor's alpha, so it drops straight into `pets/grungo/art/frames/` and `tools/sprite_pack.py` accepts it.

The graph is the masked inpaint that made grungo's frames in sprite-expressions (`flows/inpaint.json` on branch `blorbarium-art`). It runs SDXL Juggernaut XI with IP-Adapter Plus holding his identity. The sampler re-noises only the mask, and the decode is pasted back over the untouched input.

| file | what it is |
|---|---|
| `grungo_adjust.json` | The workflow for the ComfyUI editor. |
| `grungo_adjust_api.json` | The same graph in API format, for `run.py`. |
| `run.py` | Uploads the inputs, queues batches, checks results, and copies a pick into the pack. |
| `build.py` | Writes both JSON files from one node table. Edit the table, then run it. |

## Set up once

ComfyUI must be running on `http://127.0.0.1:8188` (set `COMFY` to change it). The `comfy-local-ops` skill covers launching it on this machine.

1. Upload the frames, mask presets, and body alpha into ComfyUI's input folder:

	```
	uv run --with pillow --with numpy python art/comfy/run.py install
	```

	This writes `grungo_<frame>.png` for every frame in `pets/grungo/art/frames/`, `grungo_mask_<preset>.png` for each preset, and `grungo_alpha.png`. Rerun it after a frame changes. It overwrites the old uploads.

The workflow needs these nodes and models. All of them are installed in `D:\tools\comfy` (workspace `default`, models in `D:\comfyshared\ComfyModels`):

- Checkpoint `SDXL1.0\juggernautXL_juggXIByRundiffusion.safetensors`.
- IP-Adapter `ip-adapter-plus_sdxl_vit-h.safetensors` and CLIP vision `CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors`.
- From `comfyui_ipadapter_plus`, the nodes `IPAdapterModelLoader`, `IPAdapterAdvanced`, and `PrepImageForClipVision`.
- Core ComfyUI nodes: `CheckpointLoaderSimple`, `CLIPVisionLoader`, `LoadImage`, `LoadImageMask`, `BatchImagesNode`, `CLIPTextEncode`, `MaskComposite`, `GrowMask`, `MaskToImage`, `ImageBlur`, `ImageToMask`, `InvertMask`, `VAEEncode`, `SetLatentNoiseMask`, `KSampler`, `VAEDecode`, `ImageCompositeMasked`, `JoinImageWithAlpha`, `SaveImage`, `PreviewImage`, and `Note`.

## Run it in the editor

1. Open ComfyUI, then drag `art/comfy/grungo_adjust.json` onto the canvas.
2. In **1. Input frame**, choose the `grungo_<frame>.png` to edit.
3. In **4. Edit mask**, choose a preset in **Mask preset**. **Mask preview** shows the area that the run will repaint.
4. To mark your own area, right-click **Input frame**, choose **Open in MaskEditor**, and paint. The strokes add to the preset. Choose `grungo_mask_none.png` to repaint only what you paint.
5. Write the change in **Positive prompt**, between the subject sentence and the style sentence.
6. Set **denoise** and **seed** in **Sampler**, then click **Queue**.

**Adjusted frame** saves to ComfyUI's output folder under `grungo/adjust_*.png`. That is the file to adopt. **Raw decode** is the whole generated canvas, not clipped to his body, and is for new silhouettes only.

Always load the `grungo_<frame>.png` uploads, not the RGBA files in `pets/`. The uploads are opaque on white, which is what the sampler expects. A transparent input would also send its whole background into the mask.

## Run it in batches

`run.py gen` fills the API workflow and queues it `--count` times, adding 1 to the seed each time:

```
uv run --with pillow --with numpy python art/comfy/run.py gen --frame neutral --mask mouth \
	--prompt "(a slightly wider gentle closed-mouth frog smile:1.3), calm content expression" \
	--negative "frown, open mouth" --denoise 0.6 --seed 1001 --count 4
```

It writes `outputs/comfy/<frame>_<mask>_s<seed>.png` and `outputs/comfy/<frame>_<mask>_sheet.png`, which shows the masked area of the input beside each result. For every result it prints three checks. The first is the number of pixels that changed. The second is the number that changed outside the mask plus `--grow`, which must be 0. The third is whether the alpha is unchanged, which must be `True`.

`--frame` and `--ref` take a frame name or a path to a PNG. `--prompt` is the change only. `run.py` puts the subject before it and the style after it. Pass `--subject ""` for art that is not his body. The other flags match the knobs below. Run `run.py gen --help` for the full list.

## Knobs

| knob | node | default | what it does |
|---|---|---|---|
| input frame | **Input frame** | `grungo_neutral.png` | The frame to edit. Every pixel outside the mask comes from here. |
| reference A, B | **Reference A**, **Reference B** | `grungo_neutral.png` | IP-Adapter averages the two frames into his identity. If both are neutral, the result is the proven single-reference setup. Set B to another frame, such as `grungo_happy.png`, to pull its features in. |
| IP-Adapter weight | **IP-Adapter (identity)** `weight` | 0.5 | How hard the references hold his identity. Higher values copy the reference more literally and resist the prompt. |
| IP-Adapter weight type | `weight_type` | `ease in-out` | `style transfer` lends only his linework, shading, and palette, not his body. Use it for new subjects and poses. |
| IP-Adapter end | `end_at` | 0.6 | The fraction of the steps that the references guide. |
| positive prompt | **Positive prompt** | subject, the edit, style | Put the change in the middle. `(words:1.3)` weights a phrase. |
| negative prompt | **Negative prompt** | the base negative, plus `frown, open mouth` | Add what the edit must not produce. |
| mask preset | **Mask preset** | `grungo_mask_mouth.png` | The area to repaint. White means repaint. See the presets below. |
| grow | **Grow mask** `expand` | 8 | Pixels added around the mask so the edit blends in. Nothing outside the grown mask changes. |
| soften | **Soften paste edge** `blur_radius` | 8 | Blends the paste into the input near the mask edge. Keep it at or below grow. |
| seed | **Sampler** `seed` | 1001 | The noise. A different seed gives a different take. |
| steps, cfg | **Sampler** | 28, 7.0 | Sampling steps and prompt strength. The defaults made every committed frame. |
| sampler, scheduler | **Sampler** | `euler`, `normal` | Leave them unless you are experimenting. |
| denoise | **Sampler** `denoise` | 0.6 | How much of the masked area is redrawn. 0.35 to 0.45 heals a rough paint-over. 0.5 to 0.6 reshapes. 0.8 to 0.9 redraws the area from the prompt. |

The presets, in anchor pixels on the 1024 canvas:

| preset | area |
|---|---|
| `face` | The face ellipse plus both eyes, the mask that made the expression frames. With grow 8 it fills the face box `(337,142)-(694,385)` and stops there. |
| `facebox` | The face box as a rectangle, inset by 8 so that grow 8 lands on its edge. |
| `eyes`, `mouth` | The two eye ellipses, or the mouth ellipse (`pets/grungo/art/masks/`). |
| `body` | His whole silhouette (the anchor alpha). Use it for edits across the whole body. |
| `canvas` | The whole image. Use it for new poses with **Raw decode**. |
| `none` | Nothing. Use it with strokes painted in the mask editor. |

## Recipes

These come from the expressions and variety kit in sprite-expressions (`characters/grungo/expressions.json`), which made the committed frames. A prompt cannot close an eye or move a pupil on its own. For changes like that, paint the rough shape on the opaque `grungo_<frame>.png` in an image editor first. Then load the painted file as the input frame and heal it at low denoise.

### Tweak an expression

- **A small change** (a wider smile, a softer frown). Use preset `mouth` or `eyes` at denoise 0.5 to 0.6. The run that proves this workflow used the batch command above. Seed 1004 widened neutral's smile and changed nothing outside the mouth mask.
- **A new face.** Use preset `face` at denoise 0.8 to 0.9. Weight the words, as in `(grumpy annoyed expression:1.3), (eyes narrowed to a flat glare under heavy lids:1.4)`.
- **Closed or half-closed eyes.** Paint the lids over the eyes in his skin colour, then use preset `eyes` at denoise 0.4 to 0.45 with `(eyes fully closed:1.5)`.

### Recolour a region

On the device, genes recolour skin, cloak, eyes, and glow through the palette, so tinting does not need new art. Bake a colour into the frames only for a new base look.

1. Paint the region flat in the new colour on `grungo_neutral.png` in an image editor.
2. Load the painted file as the input frame, and paint the same region in the mask editor with preset `none`.
3. Heal at denoise 0.35 to 0.45 with the colour in the prompt, for example `(a moss green cloak:1.3)`.
4. Adopt the result with `--everywhere` (see the next section) so every frame gets the new colour.

### Add a decal or an accessory

1. Paint the rough shape on `grungo_neutral.png` in an image editor.
2. Mask it with preset `none` and the mask editor.
3. Generate at denoise 0.45 for a decal on skin, or 0.5 for an accessory, with prompts in the kit's form. Positive: `(bold dark round leopard-frog spots on his arms and legs:1.4), same character, same style`. Negative: `glowing eyes, extra limbs, text, different character`.
4. Adopt the result with `--everywhere`.

An accessory must stay inside his silhouette. The pack requires every frame to share the anchor's alpha, and `sprite_pack.py` refuses a frame whose alpha differs. Long eye-stalks or a cane change the outline, so they need new alpha and converter work.

### Make a new pose

A new pose has a new silhouette, so it cannot keep the anchor's alpha.

1. Block the pose in flat shapes on a white 1024 canvas, the way the `shapes` paint op in sprite-expressions does. Then use that file as the input frame.
2. Choose preset `canvas`.
3. Delete the subject sentence from the positive prompt, and describe the pose.
4. In **IP-Adapter (identity)**, set `weight_type` to `style transfer`, `weight` to 0.7, and `end_at` to 0.8.
5. Generate at denoise 0.6.
6. Right-click **Raw decode** and choose **Save Image**. With `run.py`, pass `--raw --subject "" --ipa-weight-type "style transfer" --ipa-weight 0.7 --ipa-end-at 0.8`.

The pack has no slot for a pose yet. `sprite_pack.py` builds the body, the faces in `expressions.def`, the egg, and the remains. A pose needs keyed alpha (sprite-expressions keys blank frames in `forge.py`) and new code in the converter.

## Put a result into the sprite pack

1. Copy the pick over the frame it replaces:

	```
	uv run --with pillow --with numpy python art/comfy/run.py adopt outputs/comfy/neutral_mouth_s1004.png --as neutral
	```

	`adopt` refuses a frame whose alpha differs from the anchor's. For an edit outside the face (a cloak colour, a decal), add `--everywhere`. That copies the edit into every registered frame wherever the frame matched the old one, and each frame keeps its own face.

2. If the eyes moved, measure them again. `tools/sprite_pack.py` reads each frame's eye centres from `pets/grungo/art/eyes.json` to match iris colour, and to anchor the eyes in `happy`, `meow`, `annoyed`, and `alarmed`. In the sprite-expressions worktree, copy the frame to `characters/grungo/raw/<frame>/frame-00.png`, then run `python eyes.py grungo`. Copy that frame's entry from `characters/grungo/eyes.json` into `pets/grungo/art/eyes.json`.

3. Rebuild the pack:

	```
	uv run --with pillow --with numpy python tools/sprite_pack.py --preview outputs/pack-preview
	```

	The converter labels regions itself, from hue rules, the eye centres, and the mouth mask. Check the region labels and colours in `outputs/pack-preview`. The converter also prints the face patch box. A face edit leaves it at `(14, 7, 68, 45)`. A larger box means the edit changed pixels outside the face box, and every face frame then costs more flash.

4. Run the pack's tests:

	```
	MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/wsl_test.sh test_grungo_pack
	```

5. Bless the frame goldens, then look at what changed in `outputs/` before you commit `test/golden/`:

	```
	wsl -d survivor --exec env UPDATE_GOLDEN=1 bash /mnt/d/Projects/blorbarium/tools/sim_film.sh
	```

	Use the `/mnt/d/...` path of the checkout you are working in.

6. Update the provenance paragraph in `pets/grungo/art/README.md`. Name the frame, the prompt, the mask, the denoise, and the seed.

## Change the graph

Edit the `NODES` table in `build.py`, then run `uv run python art/comfy/build.py` with ComfyUI up. It reads each node's widget order from `/object_info` and writes both JSON files. `run.py` finds nodes by title, so keep the titles in its `KNOBS` table in step with `build.py`. If you change the graph in the editor instead, export it in both formats over these files, and expect the next `build.py` run to overwrite it.

To check that the two files agree, convert the editor file and compare the result with the API file:

```
comfy run --workflow art/comfy/grungo_adjust.json --print-prompt --where local
```
