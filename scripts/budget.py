"""Per-map Xbox memory budget, from the PC measurements.

Combines docs/memory-pc.csv (meminfo pools) and docs/textures-projection.csv
(textures projected to DXT) with per-pool reduction factors for two scenarios.
The factors are engineering estimates, to be replaced by real measurements.

Usage: python scripts/budget.py [docs] [--ram 64]
"""
import argparse
import csv
import os

# Fixed costs on the Xbox (MB)
FIXED = {
    'kernel/system': 1.9,        # measured with the probe (65536 - 63684 KB)
    'XBE image': 31.1,           # measured: .text 5.5 + .rdata 1.0 + .data/.bss 24.6 (after BSS cuts)
    'framebuffers': 3.6,         # 640x480x32 double buffered + Z24S8
    'GPU push buffer': 1.0,
    'margin/fragmentation': 3.0,
}

# meminfo pool -> (moderate, aggressive)
FACTORS = {
    'sound_MB':            (0.25, 0.125),  # Xbox ADPCM 4:1; aggressive: also 11 kHz for effects
    'skeletor_MB':         (0.60, 0.40),   # quantized animations + LRU cache
    'static renderer_MB':  (0.75, 0.60),   # compact vertices, 16-bit indices, no PC-only data
    'tiki_MB':             (0.85, 0.70),
    'game_MB':             (0.90, 0.80),
    'static_MB':           (0.90, 0.80),
    'general_MB':          (0.90, 0.80),
    'renderer_MB':         (0.80, 0.70),
    'cgame_MB':            (1.00, 1.00),
}
TEXTURES = ('xbox_dxt_orig_MB', 'xbox_dxt_128_MB')  # (moderate, aggressive)


def num(v):
    return float(str(v).replace(',', '.') or 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('docs', nargs='?', default=os.path.join(os.path.dirname(__file__), '..', 'docs'))
    ap.add_argument('--ram', type=float, default=64.0, help='console RAM in MB (64 or 128)')
    args = ap.parse_args()

    pools = {r['map']: r for r in csv.DictReader(open(os.path.join(args.docs, 'memory-pc.csv'), encoding='utf-8-sig'))}
    tex = {r['map']: r for r in csv.DictReader(open(os.path.join(args.docs, 'textures-projection.csv'), encoding='utf-8-sig'))}
    fixed = sum(FIXED.values())
    avail = args.ram - fixed

    rows = []
    for m, p in pools.items():
        if m not in tex or p['status'] != 'ok':
            continue
        pc = sum(num(p[k]) for k in FACTORS) + num(tex[m]['pc_RGBA8_MB'])
        mod = sum(num(p[k]) * f[0] for k, f in FACTORS.items()) + num(tex[m][TEXTURES[0]])
        agg = sum(num(p[k]) * f[1] for k, f in FACTORS.items()) + num(tex[m][TEXTURES[1]])
        rows.append((m, pc, mod, agg))

    print(f'RAM {args.ram:.0f} MB, fixed {fixed:.1f} MB -> available for data: {avail:.1f} MB\n')
    print(f"{'map':8}{'PC':>8}{'moderate':>10}{'aggressive':>12}")
    for m, pc, mod, agg in rows:
        flag = lambda v: 'OK' if v <= avail else '--'
        print(f'{m:8}{pc:8.1f}{mod:8.1f} {flag(mod)}{agg:9.1f} {flag(agg)}')
    for i, name in ((2, 'moderate'), (3, 'aggressive')):
        ok = sum(1 for r in rows if r[i] <= avail)
        worst = max(rows, key=lambda r: r[i])
        print(f'\n{name}: {ok}/{len(rows)} maps fit; worst {worst[0]} = {worst[i]:.1f} MB '
              f'(margin {avail - worst[i]:+.1f} MB)')

    out = os.path.join(args.docs, f'budget-xbox-{args.ram:.0f}mb.csv')
    with open(out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['map', 'pc_data_MB', 'xbox_moderate_MB', 'xbox_aggressive_MB', 'available_MB'])
        for m, pc, mod, agg in rows:
            w.writerow([m, round(pc, 1), round(mod, 1), round(agg, 1), round(avail, 1)])
    print(f'\nCSV: {out}')


if __name__ == '__main__':
    main()
