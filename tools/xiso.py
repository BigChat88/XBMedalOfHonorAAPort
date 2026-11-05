#!/usr/bin/env python3
"""Writes an Xbox disc image (XDVDFS, the "XISO" format) from a folder.

Usage: python tools/xiso.py <folder> <output.iso>

The disc layout: sector 32 holds the volume descriptor, then come the directory
tables, then the files, each starting on a sector. A directory table is a binary
tree of entries sorted by name, ignoring case; an entry never crosses a sector.
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
VOLUME_SECTOR = 32
ATTR_DIRECTORY = 0x10
ATTR_ARCHIVE = 0x20
DIRENT_HEADER = 14
# Discs end on a 64 KB boundary, like the ones extract-xiso writes
END_MODULUS = 0x10000
COPY_CHUNK = 1024 * 1024


class Node:
    def __init__(self, name, path):
        self.name = name
        self.path = path
        self.children = None  # list for a directory
        self.size = 0
        self.sector = 0
        # in the parent's table
        self.left = None
        self.right = None
        self.offset = 0


def sectors(size):
    return (size + SECTOR - 1) // SECTOR


def scan(path, name=''):
    node = Node(name, path)
    node.children = []
    for entry in sorted(os.listdir(path), key=lambda n: n.upper().encode('latin1', 'replace')):
        full = os.path.join(path, entry)
        if os.path.isdir(full):
            child = scan(full, entry)
            # a table with no entries can't be written, and the game needs none
            if child.children:
                node.children.append(child)
        else:
            child = Node(entry, full)
            child.size = os.path.getsize(full)
            if child.size >= 0xFFFFFFFF:
                raise ValueError(f'{full}: too big for a disc image')
            node.children.append(child)
    return node


def tree(entries):
    """Balanced tree over a sorted list; returns its root."""
    if not entries:
        return None
    mid = len(entries) // 2
    root = entries[mid]
    root.left = tree(entries[:mid])
    root.right = tree(entries[mid + 1:])
    return root


def preorder(root, out):
    if root:
        out.append(root)
        preorder(root.left, out)
        preorder(root.right, out)


def entry_size(node):
    return DIRENT_HEADER + len(node.name.encode('latin1'))


def layout_table(directory):
    """Places the entries of a directory; returns the table's size in bytes."""
    root = tree(directory.children)
    order = []
    preorder(root, order)
    directory.order = order
    pos = 0
    for node in order:
        size = entry_size(node)
        if pos // SECTOR != (pos + size - 1) // SECTOR:
            pos = sectors(pos) * SECTOR
        node.offset = pos
        pos += (size + 3) & ~3
    return sectors(pos) * SECTOR


def layout(directory, next_sector):
    """Tables first, breadth doesn't matter: the disc is read by the sectors in the entries."""
    directory.size = layout_table(directory)
    directory.sector = next_sector
    next_sector += sectors(directory.size)
    for child in directory.children:
        if child.children is not None:
            next_sector = layout(child, next_sector)
    return next_sector


def layout_files(directory, next_sector):
    for child in directory.children:
        if child.children is not None:
            next_sector = layout_files(child, next_sector)
        else:
            child.sector = next_sector if child.size else 0
            next_sector += sectors(child.size)
    return next_sector


def table_bytes(directory):
    data = bytearray(b'\xff' * directory.size)
    for node in directory.order:
        name = node.name.encode('latin1')
        attr = ATTR_DIRECTORY if node.children is not None else ATTR_ARCHIVE
        left = node.left.offset // 4 if node.left else 0
        right = node.right.offset // 4 if node.right else 0
        data[node.offset:node.offset + DIRENT_HEADER + len(name)] = struct.pack(
            '<HHIIBB', left, right, node.sector, node.size, attr, len(name)) + name
    return bytes(data)


def write_tables(f, directory):
    f.seek(directory.sector * SECTOR)
    f.write(table_bytes(directory))
    for child in directory.children:
        if child.children is not None:
            write_tables(f, child)


def write_files(f, directory, progress):
    for child in directory.children:
        if child.children is not None:
            write_files(f, child, progress)
        elif child.size:
            f.seek(child.sector * SECTOR)
            with open(child.path, 'rb') as src:
                while True:
                    chunk = src.read(COPY_CHUNK)
                    if not chunk:
                        break
                    f.write(chunk)
            progress(child.size)


def write_iso(folder, output, progress=lambda n: None):
    root = scan(folder)
    if not root.children:
        raise ValueError(f'{folder}: nothing to write')
    first_table = VOLUME_SECTOR + 2
    end = layout_files(root, layout(root, first_table))

    tmp = output + '.tmp'
    with open(tmp, 'wb') as f:
        volume = MAGIC + struct.pack('<IIQ', root.sector, root.size, 0)
        volume += b'\0' * (SECTOR - len(volume) - len(MAGIC)) + MAGIC
        f.seek(VOLUME_SECTOR * SECTOR)
        f.write(volume)
        write_tables(f, root)
        write_files(f, root, progress)
        total = end * SECTOR
        total += (END_MODULUS - total % END_MODULUS) % END_MODULUS
        f.truncate(total)
    os.replace(tmp, output)
    return total


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    print(write_iso(sys.argv[1], sys.argv[2]), 'bytes')
