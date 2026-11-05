"""Projects Xbox texture memory usage from the measure-maps.ps1 logs.

For each map it reads the `imagelist` output and computes how much the textures
would take:
  - as on PC (RGBA8, 4 B/texel)
  - as DXT1 (opaque, 0.5 B/texel) / DXT5 (with alpha, 1 B/texel)
  - with the resolution capped at 512, 256 and 128 (keeping the aspect ratio)
Mipmaps add 1/3 when the image uses them.

Usage: python scripts/analyze-memory.py [docs]
"""
import csv
import glob
import os
import re
import sys

LINE = re.compile(r'^\s*\d+:\s+(\d+)\s+(\d+)\s+(yes|no )\s+\d+\s+(\S+)\s+\S+\s+(.+?)\s*$')
CAPS = (None, 512, 256, 128)
# imagelist formats that carry alpha
ALPHA_FORMATS = {'RGBA', 'RGBA8', 'RGBA4', 'IA'}


def capped(w, h, cap):
    if cap is None:
        return w, h
    while w > cap or h > cap:
        w, h = max(1, w // 2), max(1, h // 2)
    return w, h


def analyze(log_path):
    images = []
    for raw in open(log_path, encoding='latin-1'):
        line = raw.split('] ', 1)[-1]  # strip the log timestamp
        m = LINE.match(line)
        if m:
            w, h, mips, fmt, name = int(m[1]), int(m[2]), m[3] == 'yes', m[4], m[5]
            images.append((w, h, mips, fmt, name))
    if not images:
        return None

    result = {'images': len(images)}
    pc = sum(w * h * 4 * (4 / 3 if mips else 1) for w, h, mips, _, _ in images)
    result['pc_RGBA8_MB'] = pc / 2**20
    for cap in CAPS:
        total = 0.0
        for w, h, mips, fmt, name in images:
            # Lightmaps, fonts and UI stay uncompressed (DXT artifacts would show)
            raw = name.startswith(('*', 'gfx/2d', 'textures/menu', 'fonts', '$lightmap'))
            cw, ch = (w, h) if raw else capped(w, h, cap)
            bpt = 4 if raw else (1 if fmt in ALPHA_FORMATS else 0.5)
            total += cw * ch * bpt * (4 / 3 if mips else 1)
        result[f'xbox_dxt_{cap or "orig"}_MB'] = total / 2**20
    return result


def main():
    docs = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'docs')
    rows = []
    for log in sorted(glob.glob(os.path.join(docs, 'logs-*.log'))):
        name = os.path.basename(log)[5:-4]
        r = analyze(log)
        if r:
            rows.append({'map': name, **r})
    if not rows:
        sys.exit('No imagelist output found in the logs')

    out = os.path.join(docs, 'textures-projection.csv')
    with open(out, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        for r in rows:
            w.writerow({k: round(v, 2) if isinstance(v, float) else v for k, v in r.items()})

    keys = [k for k in rows[0] if k.endswith('_MB')]
    print(f"{'':22}{'min':>8}{'mean':>8}{'max':>8}  worst map")
    for k in keys:
        vals = [(r[k], r['map']) for r in rows]
        print(f"{k:22}{min(vals)[0]:8.1f}{sum(v for v, _ in vals) / len(vals):8.1f}{max(vals)[0]:8.1f}  {max(vals)[1]}")
    print(f'\nCSV: {out}')


if __name__ == '__main__':
    main()
