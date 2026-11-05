#!/usr/bin/env python3
"""The game's on-screen texts, worded for a controller.

The game shows its texts through global/localization.txt, a table of
{ "text in the scripts" "text on screen" }. A loose copy in main/global replaces
the one in the paks, so the scripts stay as they are. The table belongs to the game,
so it isn't in this repository: tools/pack_iso.py writes it into the disc image. The
names of the buttons inside the parentheses come from the engine
(Key_KeynumToBindString in client/cl_keys.cpp).
"""
import glob
import os
import re
import zipfile

# Texts that talk about the mouse or the movement keys: no word by word rule fits them
WHOLE = {
    'First I want you to use your mouse to look at each of the four guard towers.':
        'First I want you to use the R Stick to look at each of the four guard towers.',
    'Turn the turret with the Mouse.': 'Turn the turret with the R Stick.',
    'Use the mouse or keyboard to look up and around at the towers':
        'Use the R Stick to look up and around at the towers',
    'Use the mouse or keyboard to look up and around at the towers.':
        'Use the R Stick to look up and around at the towers.',
    'Use your MOUSE and the movement keys to look at the towers.':
        'Use the R Stick and the L Stick to look at the towers.',
    'Use your mouse or ': 'Use the R Stick or ',
    'Use your mouse to look at each of the four guard towers.':
        'Use the R Stick to look at each of the four guard towers.',
    'Use your MOUSE to turn and aim.': 'Use the R Stick to turn and aim.',
    'Use your Strafe keys to turn.': 'Move the L Stick left and right to turn.',
    'Using these four keys together with the mouse is critical to your success against the enemy.':
        'Using the L Stick together with the R Stick is critical to your success against the enemy.',
    'Press TAB to view your current objectives': 'Press BACK to view your current objectives',
    ' ) and movement keys to pass under the barbwire.': ' ) and the L Stick to pass under the barbwire.',
    ' ) and movement keys to surpass these crates.': ' ) and the L Stick to surpass these crates.',
    ' ) keys to jump up this pile of crates.': ' ) to jump up this pile of crates.',
    'Use the movement keys and look up to climb the ladder. You may also press your use button. ':
        'Use the L Stick and look up to climb the ladder. You may also press your use button. ',
    'Use the movement keys and look up to climb the ladder. You may also press your USE key.':
        'Use the L Stick and look up to climb the ladder. You may also press your USE button.',
    'Move the tank with your Forward and Backward keys.': 'Move the tank with the L Stick.',
}

# Parts of sentences that the scripts join with the button's name, and that the
# game's table doesn't have: without an entry they would be shown as they are
MISSING = [
    'Approach alarms and press the USE key ( ',
    'Press the USE key  ( ',
    'Press the FORWARD key  ( ',
    'Press the OBJECTIVE key  ( ',
    'Look up and press your forward key ( ',
    ' ) key to holster your weapon.',
    ' ) key to show your papers.',
    ' ) Then look at the target and press your Fire key. ( ',
    'Move the tank with your Forward key ( ',
    ' ) and Backward key. ( ',
    'Hold your Jump key ( ',
]

# "Unknown Key" and "Key Not Found" are the engine's, and never shown in the game
KEEP = {'Unknown Key', 'Key Not Found'}


def reword(text):
    text = re.sub(r'\bkeys\b', 'buttons', text)
    text = re.sub(r'\bKeys\b', 'Buttons', text)
    text = re.sub(r'\bkey\b', 'button', text)
    return re.sub(r'\bKey\b', 'Button', text)


class Paks:
    """Reads files from the game's paks; later paks replace earlier ones, as in the game."""

    def __init__(self, main):
        self.paks = sorted(glob.glob(os.path.join(main, '[Pp]ak*.pk3')), key=lambda p: os.path.basename(p).lower())

    def read(self, name):
        found = None
        for pak in self.paks:
            with zipfile.ZipFile(pak) as z:
                for entry in z.namelist():
                    if entry.lower() == name.lower():
                        found = z.read(entry)
        if found is None:
            raise FileNotFoundError(name)
        return found.decode('latin1')


def build(paks):
    source = paks.read('global/localization.txt')
    entry = re.compile(r'^(\s*\{\s*")((?:[^"\\]|\\.)*)("\s*")((?:[^"\\]|\\.)*)("\s*\}\s*)$')
    found = set()
    lines = []
    for line in source.splitlines():
        m = entry.match(line)
        if m:
            refer, shown = m.group(2), m.group(4)
            found.add(refer)
            if refer in WHOLE:
                new = WHOLE[refer]
            elif refer in KEEP:
                new = shown
            else:
                new = reword(shown)
            if new != shown:
                line = m.group(1) + refer + m.group(3) + new + m.group(5)
        lines.append(line)

    for text in MISSING:
        if text not in found:
            lines.append('{ "%s" "%s" }' % (text, reword(text)))
    for refer in WHOLE:
        if refer not in found:
            raise ValueError(f'not the table this tool knows: {refer}')

    return '\r\n'.join(lines) + '\r\n'
