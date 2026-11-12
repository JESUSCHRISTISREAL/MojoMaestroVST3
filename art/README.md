# Faceplate pictures

The pictures in `../Assets` are made in three steps. Blender is needed only for step 2.

1. Layout and silkscreen
   - `pipeline/layout.py` writes `layout.json`, the single source of truth for positions (logical px, window 480 x 392).
   - `pipeline/masks.py` draws the lettering into `silk.png` and `silk_height.png`. It needs two fonts in `art/fonts/` (not included): `caveat-700.ttf` and `libre-baskerville-400.ttf`, both from Google Fonts under the SIL Open Font License 1.1.
2. Renders (Blender 5.x, Cycles): `run_all.ps1` writes `out/plate.png`, `out/knob.png`, `out/small.png`, `out/ptr_big.png`, `out/ptr_small.png` and `out/cap.png`. `scene.py` renders the plate (walnut end cheeks, white plastic display bezel), the knobs and the pointers; `scene_cap.py` renders the switch cap.
3. Final pictures: `python pipeline/build_assets.py 3 art/out Assets` resizes the plate, paints the slide channels and the display window (`pipeline/window.py`, plain smoked glass), shapes the outline (`pipeline/shape.py`), prepares the rendered knobs, cleans up the cap and writes the six `*_3x.png` pictures. It needs numpy, Pillow and opencv-python (`pip install numpy pillow opencv-python`).

The live display is drawn by the editor, not by these pictures. Its segment shapes and letter table come from `pipeline/vfd_geometry.py`, which writes `../Source/VfdGeometry.h` (run `python pipeline/vfd_geometry.py` after changing the geometry).

`art/out/` (the renders) is not kept in the repository; re-run step 2 to recreate it.
