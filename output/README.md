# output/

`build.cmd` / `build.sh` (`tools/pack_iso.py`) write the game here, in both forms:

* `mohaa.iso`: a disc image.
* `mohaa/`: the same files as a folder, to copy to the console's hard disk.

`work/` keeps the converted music between runs (delete it, or pass `--clean`, to start
over). Nothing here is committed: it contains your copy of the game.
