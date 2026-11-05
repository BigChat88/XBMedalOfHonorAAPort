#!/bin/sh
# Renames the external symbols defined by a module (cgame or game) so it can be
# linked statically next to the engine without name clashes.
#
# Usage: prefix-module.sh <prefix> <output_dir> <entry> <obj...>
#   <entry> : symbol that is not renamed (the module's entry point)
#
# 1. Collects every external symbol defined in the module's objects.
# 2. Writes an "old new" map (everything prefixed, except <entry>).
# 3. Applies the map to each object with llvm-objcopy --redefine-syms, so the
#    module's internal references point at its own copies.
# 4. Gives the module a life cycle of its own, like a DLL that is loaded and
#    unloaded (sys_xbox.c, Sys_Xbox_LoadLibrary): .data/.bss go to sections
#    named after the prefix (.g_data/.g_bss for g_), the static constructors
#    to .g_i$U instead of the CRT's .CRT$XCU (run on each load, not at boot),
#    and atexit (the static destructors) to g_module_atexit (run on unload).
set -eu

prefix=$1; out=$2; entry=$3; shift 3
mkdir -p "$out"
map="$out/rename.map"

llvm-nm --defined-only --extern-only --no-sort --format=just-symbols "$@" 2>/dev/null \
    | grep -v -e '^@' -e '^$' -e ':$' \
    | sort -u \
    | awk -v p="$prefix" -v e="$entry" '$1 != e { print $1 " " p $1 }' > "$map"
echo "_atexit _${prefix}module_atexit" >> "$map"

echo "[ PREFIX $prefix ] $(wc -l < "$map") symbols, $# objects"

i=0
for obj in "$@"; do
    i=$((i + 1))
    dst="$out/$i.obj"
    # See 4. above
    llvm-objcopy --redefine-syms="$map" "$obj" "$dst"
    python3 "$(dirname "$0")/coff-rename-sections.py" "$dst" .data=".${prefix}data" .bss=".${prefix}bss" \
        '.CRT$XCU'=".${prefix}i\$U"

    # clang emits /INCLUDE:"symbol" in .drectve (e.g. for template statics with
    # dynamic initialization); those names are plain text that --redefine-syms
    # does not touch, so they are rewritten by hand.
    if llvm-objcopy --dump-section=.drectve="$dst.drectve" "$dst" 2>/dev/null; then
        awk -v RS=' ' -v ORS=' ' -v p="$prefix" '
            NR == FNR { ren[$1] = 1; next }
            {
                if (match($0, /^\/INCLUDE:"[^"]*"/)) {
                    name = substr($0, 11, RLENGTH - 11)
                    if (name in ren) $0 = "/INCLUDE:\"" p name "\"" substr($0, RLENGTH + 1)
                }
                print
            }' RS='\n' "$map" RS=' ' "$dst.drectve" > "$dst.drectve.new"
        # --update-section cannot grow a section; lld recognizes .drectve by
        # name, so the whole section is replaced.
        llvm-objcopy --remove-section=.drectve --add-section=.drectve="$dst.drectve.new" "$dst"
        rm -f "$dst.drectve" "$dst.drectve.new"
    fi
done
