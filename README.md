# Medal of Honor: Allied Assault for the original Xbox

A port of **Medal of Honor: Allied Assault** (PC, 2002) to the original Xbox, built
on [OpenMoHAA](https://github.com/openmoh/openmohaa) (the open-source
reimplementation of the game's engine) and [nxdk](https://github.com/XboxDev/nxdk),
plus a tool that turns your own copy of the game into a disc image and a folder.

No copyrighted game data is in this repository. You supply your own copy; the build
packs it for the console.

## What you need to play

* An original Xbox that runs homebrew, with **128 MB of RAM**. The game doesn't fit
  in a stock console's 64 MB. [xemu](https://xemu.app/) works too, set to 128 MB.
* Your own copy of the game, installed and updated to patch 1.11
  (see [gamedata/README.md](gamedata/README.md)).

Single player only: all the campaign's maps load, and the menus hide multiplayer.
Saved games and settings go to `E:\OGXMOHAA` on the hard disk.

## How to build the game

You only need **[Python 3](https://www.python.org/downloads/)** on your `PATH`. No
Docker or Xbox toolchain.

1. Grab the latest [Release](../../releases) (`OGXMOHAA-x.y.z.zip`) and unzip it.
2. Copy the game's `main` folder to `gamedata/main`.
3. Build:
   * **Windows:** double-click `build.cmd`.
   * **Linux/macOS:** `./build.sh`
4. Wait for the two `OK ->` lines. You get both forms of the game:
   * `output/mohaa.iso`, a disc image.
   * `output/mohaa/`, the same files as a folder: copy it to the console's hard disk
     and start `default.xbe`.

The first run takes about 20 minutes, nearly all of it converting the music, which
makes the game run faster and is only done once. `--no-music` skips it. It installs
`imageio-ffmpeg` (the music) and `Pillow` (the cover) with pip, and
`python tools/pack_iso.py --help` lists the options.

### The cover in the dashboards

The game has no cover by default. If you have one, put it in `assets/Front.png`, a
folder that isn't part of the repository, before building. The game then carries it as
`_resources/artwork/poster.jpg` (XBMC4Gamers) and `default.tbn` (XBMC). In
XBMC4Gamers, press the white button on the game and choose *Refresh Synopsis Info*.

## Controls

| Button | Action |
|---|---|
| Left stick | Move / strafe |
| Right stick | Look |
| Right trigger | Fire |
| Left trigger | Secondary attack (melee, scope) |
| A | Jump |
| B | Crouch |
| X | Reload |
| Y | Use (doors, mounted guns) |
| White / Black | Lean left / right |
| Left stick click | Run / walk |
| Right stick click | Binoculars / items |
| D-pad left / right | Previous / next weapon |
| D-pad up | Grenade |
| D-pad down | Holster |
| BACK | Objectives |
| START | Menu |

In menus: D-pad or left stick to move, A to accept, B to go back.

## Building from source

The game builds in Docker, with the image the nxdk project publishes:

```sh
scripts/nxdk.sh xbox/mohaa 'make -j12 xbe'   # xbox/mohaa/bin/default.xbe
python tools/pack_iso.py                     # output/mohaa.iso and output/mohaa/
```

To change the game and try it again without packing everything, `make iso` and
`make folder` (in the same container) put a new XBE on what `pack_iso.py` left in
`output/work/stage`, and `make iso AUTOEXEC=data/autoexec_perf.cfg` boots straight
into a test: the `xbox/mohaa/data/autoexec_*.cfg` files are the test scenes.
`scripts/run-xemu.ps1` starts an image in xemu; copy
`tools/xemu/xemu-128.example.toml` to `xemu-128.toml` and fill in your paths.

[docs/memory.md](docs/memory.md) has the port's measurements: memory budget, frame
times and the reasons for each change. In `third_party/`, `openmohaa` and `pbgl` hold
the upstream code with the port's changes on top, all under `#ifdef NXDK` in the
engine; [third_party/README.md](third_party/README.md) says which commit each came from.

## Credits

* [OpenMoHAA](https://github.com/openmoh/openmohaa) and its contributors: the engine
  this port is built on.
* [nxdk](https://github.com/XboxDev/nxdk): the Xbox toolchain and libraries.
* [pbgl](https://github.com/fgsfdsfgs/pbgl) by fgsfds: OpenGL on the Xbox's GPU.
* [printf](https://github.com/eyalroz/printf) by Marco Paland and Eyal Rozenberg.

## License

GPL version 2, like OpenMoHAA; see [LICENSE](LICENSE). Medal of Honor is a trademark
of Electronic Arts Inc. This project isn't affiliated with EA.

## AI Note

The application was developed using AI. I'm just an enthusiast who wanted to create interesting projects. In this case, how it was achieved is not relevant to me.
