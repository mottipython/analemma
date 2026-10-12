#!/usr/bin/env python3
"""Turn the drawings in assets/src into the pictures the screensaver builds in,
under assets/art.

    tools/make_sprites.py

theors.jpeg is a sheet: six figures on one flat magenta background, not touching,
read off in rows, left to right. light.jpeg is two pictures of light on black, side
by side: the pool on the floor and the shaft. They stay on black, because the
program adds them to what is behind. temple.jpeg is the background. Needs
ImageMagick and numpy.
"""
import subprocess
import numpy as np

SRC, OUT = "assets/src", "assets/art"
NAMES = ["lead_wait", "lead_up", "sing1", "sing2", "sing3", "sing4"]
TALL = 420

def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def runs(on, gap):
    """Stretches where `on` is true, joining ones separated by less than `gap`."""
    out, start, last = [], None, None
    for i, v in enumerate(on):
        if not v: continue
        if start is None: start = i
        elif i - last > gap: out.append((start, last + 1)); start = i
        last = i
    if start is not None: out.append((start, last + 1))
    return [r for r in out if r[1] - r[0] > 30]

def pieces(a, rows):
    """The separate drawings on a sheet, in reading order: split into rows by the
    empty bands between them, then each row into drawings by the empty gaps."""
    solid = a[..., 3] > 128
    bands = runs(solid.sum(axis=1) > 2, 40)
    if len(bands) != rows:
        raise SystemExit(f"found {len(bands)} rows of drawings, expected {rows}")
    out = []
    for y0, y1 in bands:
        for x0, x1 in runs(solid[y0:y1].sum(axis=0) > 2, 40):
            out.append(a[y0:y1, x0:x1])
    return out

def trim(a):
    solid = a[..., 3] > 128
    rows = np.where(solid.sum(axis=1) > 2)[0]; cols = np.where(solid.sum(axis=0) > 2)[0]
    return a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9", path],
                   input=np.ascontiguousarray(rgba).tobytes(), check=True)

def main():
    subprocess.run(["mkdir", "-p", OUT], check=True)
    got = [trim(p) for p in pieces(key(load(f"{SRC}/theors.jpeg")), 2)]
    if len(got) != len(NAMES):
        raise SystemExit(f"theors: found {len(got)} drawings, expected {len(NAMES)}")
    f = TALL / np.median([p.shape[0] for p in got])  # one scale, so they are the same size
    for name, p in zip(NAMES, got):
        w, h = max(1, round(p.shape[1] * f)), max(1, round(p.shape[0] * f))
        save(p, w, h, f"{OUT}/{name}.png")
        print(name, w, h)
    img = load(f"{SRC}/light.jpeg")
    cols = runs(img.max(axis=2).max(axis=0) > 40, 30)
    if len(cols) != 2:
        raise SystemExit(f"light: found {len(cols)} pictures, expected 2")
    for name, (x0, x1) in zip(["pool", "shaft"], cols):
        part = img[:, max(0, x0 - 10):x1 + 10]
        if name == "pool":
            rows = np.where(part.max(axis=2).max(axis=1) > 40)[0]
            part = part[max(0, rows.min() - 10):rows.max() + 10]
        part = np.clip((part - 12) * 255 / 243, 0, 255).astype(np.uint8)  # true black where there is no light
        subprocess.run(["magick", "-size", f"{part.shape[1]}x{part.shape[0]}", "-depth", "8", "RGB:-", "-resize", "500x700>",
                        "-quality", "92", f"{OUT}/{name}.jpg"], input=np.ascontiguousarray(part).tobytes(), check=True)
        print(name, part.shape[1], part.shape[0])
    subprocess.run(["magick", f"{SRC}/temple.jpeg", "-resize", "1920x1080!", "-quality", "90", f"{OUT}/temple.jpg"], check=True)
    print("temple")

if __name__ == "__main__":
    main()
