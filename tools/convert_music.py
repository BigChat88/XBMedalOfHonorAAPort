#!/usr/bin/env python3
"""Converts the streamed MP3s (music, stereo ambience) to IMA ADPCM for the Xbox.

Decoding MP3 took about 6% of the CPU during play, and opening one scanned the
whole file. The game plays <name>.ima when it finds one next to <name>.mp3
(S_IMA_CodecOpenStream in xbox/mohaa/src/xbox_imastream.c); the MP3 stays as a
fallback. An .ima file is an ordinary IMA ADPCM WAV, so any player can open it.

44 kHz sources go to 22 kHz, the rate the MP3 decoder was already producing; 22 kHz
sources stay at 22 kHz. Files that are already up to date are skipped.
"""
import concurrent.futures
import os
import shutil
import subprocess
import tempfile

FOLDERS = ('music', 'amb_stereo')
# Decoded by xbox_imastream.c: stereo blocks of this size hold 1017 samples per channel
BLOCK_BYTES = 1024
# Search depth of the encoder: slower to convert, lower noise, same decoding cost
TRELLIS = 8


def find_ffmpeg():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except (ImportError, RuntimeError):
        return shutil.which('ffmpeg')


def convert(ffmpeg, mp3):
    ima = os.path.splitext(mp3)[0] + '.ima'
    # not next to the output: the disc image packs that folder
    tmp = os.path.join(tempfile.gettempdir(), os.path.basename(ima) + '.tmp.wav')
    subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', mp3, '-map_metadata', '-1', '-ar', '22050',
                    '-c:a', 'adpcm_ima_wav', '-block_size', str(BLOCK_BYTES), '-trellis', str(TRELLIS), tmp], check=True)
    shutil.move(tmp, ima)
    return mp3


def convert_all(sound, jobs):
    todo = []
    for folder in FOLDERS:
        base = os.path.join(sound, folder)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if name.lower().endswith('.mp3'):
                mp3 = os.path.join(base, name)
                ima = os.path.splitext(mp3)[0] + '.ima'
                if not os.path.exists(ima):
                    todo.append(mp3)
    if not todo:
        print('Music already converted')
        return

    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        print('No ffmpeg (pip install -r requirements.txt): the game will play the MP3s, using more CPU')
        return

    print(f'Converting the music: {len(todo)} files, about 20 minutes the first time. It is only done once.')
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for i, mp3 in enumerate(pool.map(lambda m: convert(ffmpeg, m), todo), 1):
            print(f'  {i}/{len(todo)} {os.path.basename(mp3)}', flush=True)
