# gamedata/

Put your own copy of the game here. Nothing in this folder is committed.

Install **Medal of Honor: Allied Assault** on a PC, update it to **patch 1.11**, and
copy its `main` folder here:

```
gamedata/
  main/
    Pak0.pk3 ... Pak5.pk3, pak6.pk3
    sound/
    video/
```

`tools/pack_iso.py` also finds it as `gamedata/<any folder>/main`, or anywhere else
with `--gamedata`. It only reads from here: the PC's DLLs are left out, and what it
changes goes to `output/`.

The game has only been tested with patch 1.11. Its disc images can't be used directly,
because the game's installer packs the files inside them.
