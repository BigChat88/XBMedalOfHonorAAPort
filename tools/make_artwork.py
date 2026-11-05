#!/usr/bin/env python3
"""Writes the game's cover for the dashboards, from assets/Front.png, if it exists.

The assets folder isn't part of the repository. The cover goes next to the XBE in the disc image and the folder:
  _resources/artwork/poster.jpg   the folder XBMC4Gamers reads
  default.tbn                     a JPEG with that name, which plain XBMC shows

The dashboards are made for JPEG, so transparency goes over black. Pillow is only
needed for this step.
"""
import os
import shutil


def remove(stage):
    """A cover left by an earlier build, when the source is gone."""
    shutil.rmtree(os.path.join(stage, '_resources'), ignore_errors=True)
    tbn = os.path.join(stage, 'default.tbn')
    if os.path.exists(tbn):
        os.remove(tbn)


def write(stage):
    remove(stage)
    repository = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    source = os.path.join(repository, 'assets', 'Front.png')
    if not os.path.exists(source):
        return
    try:
        from PIL import Image
    except ImportError:
        print('Pillow is not installed: the game will have no cover (pip install -r requirements.txt)')
        return

    cover = Image.open(source).convert('RGBA')
    flat = Image.new('RGB', cover.size, (0, 0, 0))
    flat.paste(cover, mask=cover.getchannel('A'))
    folder = os.path.join(stage, '_resources', 'artwork')
    os.makedirs(folder, exist_ok=True)
    flat.save(os.path.join(folder, 'poster.jpg'), 'JPEG', quality=92)
    flat.save(os.path.join(stage, 'default.tbn'), 'JPEG', quality=92)
    print(f'Cover {cover.size[0]}x{cover.size[1]} from assets/Front.png')
