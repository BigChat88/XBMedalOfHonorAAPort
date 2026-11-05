#!/usr/bin/env python3
"""Rewrites the game's menus for the console, from the player's own paks.

    main.urc              no Multiplayer, no Quit (a console game has nowhere to quit to)
    options_main.urc      no Video
    advancedoptions.urc   what makes no sense on a console is shown dimmed and inert
    audio options.urc     the same
    controls.urc          the controller's layout instead of the key bindings

The menus belong to the game, so they aren't in this repository: tools/pack_iso.py
writes them into the disc image. "inert" is a widget state of this port
(EV_Widget_Inert in third_party/openmohaa/code/uilib/uiwidget.cpp).
"""
import re

# The controller's layout, as xbox/mohaa/src/xbox_paddefaults.cpp binds it
LAYOUT = [
    ('L STICK', 'Move'), ('R STICK', 'Look'), ('R TRIGGER', 'Fire'),
    ('L TRIGGER', 'Secondary fire'), ('A', 'Jump'), ('B', 'Crouch'), ('X', 'Reload'), ('Y', 'Use'),
    ('WHITE / BLACK', 'Lean left / right'), ('L STICK CLICK', 'Run / walk'),
    ('D-PAD LEFT / RIGHT', 'Previous / next weapon'), ('D-PAD UP / DOWN', 'Grenade / holster'),
    ('BACK', 'Objectives'),
]

# Flat panels over the captions printed on the Controls background that no longer
# apply: rectangle, the color of the panel under it and the new caption
CONTROLS_COVERS = [
    ((160, 123, 120, 25), (0.55, 0.61, 0.63), 'CONTROLLER'),
    ((447, 125, 120, 24), (0.54, 0.61, 0.62), 'LOOK'),
    ((450, 160, 120, 16), (0.57, 0.66, 0.70), 'LOOK SPEED'),
    ((419, 204, 182, 24), (0.55, 0.61, 0.62), 'INVERT LOOK'),
    ((416, 236, 188, 28), (0.54, 0.61, 0.62), None),
    ((416, 370, 188, 28), (0.54, 0.60, 0.62), None),
]


def split(text):
    text = text.replace('\r\n', '\n')
    first = text.index('resource')
    return text[:first], re.findall(r'resource\s*\n\w+\s*\n?\{.*?\n\}', text, re.S)


def join(head, resources):
    resources = [r for r in resources if 'ui_voodoo' not in r]
    return head + '\n'.join(resources) + '\nend.\n'


def inert_all_but(text, keep):
    head, resources = split(text)
    out = []
    for r in [r for r in resources if 'ui_voodoo' not in r]:
        kind = re.match(r'resource\s*\n(\w+)', r).group(1)
        name = re.search(r'^name "([^"]*)"', r, re.M)
        title = re.search(r'^title "([^"]*)"', r, re.M)
        ident = {name.group(1) if name else '', title.group(1) if title else ''}
        # the values shown next to the options go with them
        value = kind == 'Label' and 'textures/menu/blank2' in r
        interactive = kind in ('CheckBox', 'Slider', 'PulldownMenuContainer', 'Button', 'Field', 'List')
        if (interactive or value) and not ident & keep:
            r = r.replace('{\n', '{\ninert 1\n', 1)
        out.append(r)
    return head + '\n'.join(out) + '\nend.\n'


def label(x, y, w, h, title, align='left'):
    return f'''resource
Label
{{
name "Default"
rect {x} {y} {w} {h}
fgcolor 1.00 1.00 1.00 1.00
bgcolor 0.00 0.00 0.00 0.00
borderstyle "NONE"
font verdana-14
textalign {align}
title "{title}"
}}'''


def cover(rect, color, title):
    out = f'''resource
Label
{{
name "Default"
rect {rect[0]} {rect[1]} {rect[2]} {rect[3]}
fgcolor 0.00 0.00 0.00 1.00
bgcolor {color[0]:.2f} {color[1]:.2f} {color[2]:.2f} 1.00
borderstyle "NONE"
'''
    if title:
        out += f'font facfont-20\ntitle "{title}"\n'
    return out + '}'


def controls(text):
    head, res = split(text)
    speed = res[23].replace('setrange 1 20', 'setrange 2 10')
    if speed == res[23]:
        raise ValueError('controls.urc is not the one this tool knows')
    rows = []
    for i, (button, action) in enumerate(LAYOUT):
        rows.append(label(72, 168 + i * 15, 140, 15, button))
        rows.append(label(216, 168 + i * 15, 160, 15, action))
    covers = [cover(rect, color, title) for rect, color, title in CONTROLS_COVERS]
    return join(head, res[:2] + res[8:16] + rows + covers + [res[17], speed, res[20]])


def build(read):
    """read(name) gives the text of ui/<name>; returns {file name: text}."""
    head, res = split(read('main.urc'))
    menus = {'main.urc': join(head, [r for r in res
                                      if 'pushmenu multiplayer' not in r and 'stuffcommand "quit"' not in r])}

    head, res = split(read('options_main.urc'))
    menus['options_main.urc'] = join(
        head, [r for r in res if 'pushmenu video_options' not in r and 'shortversion' not in r])

    menus['advancedoptions.urc'] = inert_all_but(read('advancedoptions.urc'), {'subtitles', 'weaponsbar', 'Crosshair', 'back'})
    menus['audio options.urc'] = inert_all_but(read('audio options.urc'), {'Music Volume Slider', 'Master Volume Slider', 'back'})
    menus['controls.urc'] = controls(read('controls.urc'))
    return menus
