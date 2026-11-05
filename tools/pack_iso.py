#!/usr/bin/env python3
"""Builds the Xbox game from your own copy of Medal of Honor: Allied Assault.

Writes both forms of the game:

    output/mohaa.iso   a disc image
    output/mohaa/      the same files as a folder, to copy to the console's hard disk

    python tools/pack_iso.py                      # takes the game from gamedata/
    python tools/pack_iso.py --gamedata D:/MOHAA  # look somewhere else
    python tools/pack_iso.py --no-music           # skip the music conversion

What it does with the game's files, none of which are in this repository:
  * leaves out what only the PC needs (its DLLs);
  * rewrites the menus and the on-screen texts for a controller;
  * converts the streamed music and ambience from MP3 to IMA ADPCM, which takes the
    game about 6% less CPU. This is the slow step, about 20 minutes, once: the
    results stay in output/work/. It needs ffmpeg (requirements.txt installs one);
    without it the game plays the MP3s.
  * adds the cover for the dashboards, if there is an assets/Front.png.

The game itself (default.xbe) comes from engine/ in a release, or from
xbox/mohaa/bin/ when built from source (see README.md).
"""
import argparse
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import convert_music  # noqa: E402
import make_artwork  # noqa: E402
import make_localization  # noqa: E402
import make_menus  # noqa: E402
import xiso  # noqa: E402

REQUIRED = ['Pak0.pk3', 'Pak1.pk3', 'Pak2.pk3', 'Pak3.pk3', 'Pak4.pk3', 'Pak5.pk3']
SKIP_EXTENSIONS = ('.dll', '.exe')


def find_game(start):
    """The folder with the paks: gamedata/main, gamedata/<anything>/main or gamedata itself."""
    candidates = [start, os.path.join(start, 'main')]
    if os.path.isdir(start):
        for name in sorted(os.listdir(start)):
            candidates.append(os.path.join(start, name, 'main'))
    for path in candidates:
        if os.path.isdir(path):
            names = {n.lower() for n in os.listdir(path)}
            if all(r.lower() in names for r in REQUIRED):
                return path
    return None


def find_engine():
    for folder, data in ((os.path.join(ROOT, 'engine'), os.path.join(ROOT, 'engine')),
                         (os.path.join(ROOT, 'xbox', 'mohaa', 'bin'), os.path.join(ROOT, 'xbox', 'mohaa', 'data'))):
        if os.path.exists(os.path.join(folder, 'default.xbe')):
            return folder, data
    return None, None


def place(src, dst):
    """Hard link when possible: the game takes 1.3 GB, three times over otherwise."""
    if os.path.exists(dst):
        if os.path.getsize(dst) == os.path.getsize(src) and os.path.getmtime(dst) >= os.path.getmtime(src):
            return
        os.remove(dst)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def stage_game(game, stage):
    count = 0
    for base, _, files in os.walk(game):
        rel = os.path.relpath(base, game)
        dest = os.path.normpath(os.path.join(stage, 'main', rel))
        os.makedirs(dest, exist_ok=True)
        for name in files:
            if not name.lower().endswith(SKIP_EXTENSIONS):
                place(os.path.join(base, name), os.path.join(dest, name))
                count += 1
    return count


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='latin1', newline='') as f:
        f.write(text)


def copy_folder(src, dst):
    if os.path.exists(dst):
        shutil.rmtree(dst)
    for base, _, files in os.walk(src):
        target = os.path.normpath(os.path.join(dst, os.path.relpath(base, src)))
        os.makedirs(target, exist_ok=True)
        for name in files:
            place(os.path.join(base, name), os.path.join(target, name))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--gamedata', default=os.path.join(ROOT, 'gamedata'), help='where the game is (default: gamedata/)')
    parser.add_argument('--output', default=os.path.join(ROOT, 'output'), help='where to write (default: output/)')
    parser.add_argument('--no-music', action='store_true', help='keep the MP3s; quicker to build, slower to play')
    parser.add_argument('--clean', action='store_true', help='start over, converting the music again')
    parser.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 2), help='music conversions at once')
    args = parser.parse_args()

    game = find_game(args.gamedata)
    if not game:
        sys.exit(f'No game in {args.gamedata}: it needs the main folder of an installed copy, '
                 f'with {REQUIRED[0]} to {REQUIRED[-1]}. See gamedata/README.md.')
    engine, data = find_engine()
    if not engine:
        sys.exit('No default.xbe, in engine/ or xbox/mohaa/bin/. Use a release, or build it (see README.md).')

    stage = os.path.join(args.output, 'work', 'stage')
    if args.clean and os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, 'main'), exist_ok=True)

    print(f'Game:   {game}')
    print(f'Engine: {engine}')
    print(f'{stage_game(game, stage)} files of the game')

    paks = make_localization.Paks(game)
    write_text(os.path.join(stage, 'main', 'global', 'localization.txt'), make_localization.build(paks))
    for name, text in make_menus.build(lambda n: paks.read('ui/' + n)).items():
        write_text(os.path.join(stage, 'main', 'ui', name), text)
    print('Menus and texts rewritten for the controller')

    place(os.path.join(engine, 'default.xbe'), os.path.join(stage, 'default.xbe'))
    for name in ('autoexec.cfg', 'xbox_default.cfg'):
        shutil.copyfile(os.path.join(data, name), os.path.join(stage, 'main', name))

    make_artwork.write(stage)

    if args.no_music:
        print('Music conversion skipped: the game will play the MP3s')
    else:
        convert_music.convert_all(os.path.join(stage, 'main', 'sound'), args.jobs)

    folder = os.path.join(args.output, 'mohaa')
    copy_folder(stage, folder)
    print(f'OK -> {folder}')

    iso = os.path.join(args.output, 'mohaa.iso')
    size = xiso.write_iso(stage, iso)
    print(f'OK -> {iso} ({size // (1024 * 1024)} MB)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
