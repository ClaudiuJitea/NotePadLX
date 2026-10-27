#!/usr/bin/env python3
# This file is part of NotePadLX, a Linux port of Notepad++.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option any later version.
#
# Writes the MimeType= line of packaging/notepadlx.desktop (the file associations of the packages) from the file
# extensions of data/langs.model.xml (upstream: the extensions of the File Association page, through the registry).
# Each extension is looked up in the freedesktop shared-mime-info database; only text types are kept (text/*, and
# the types that derive from text/plain, such as application/json), so that a shared extension like ".ts" (MPEG
# transport stream vs TypeScript) does not make NotePadLX the handler of video files.
#
# Usage: tools/gen_mimetypes.py [--check] [<shared-mime-info dir, default /usr/share/mime>]
#   --check: do not write, exit 1 if the .desktop file is not up to date (CI)

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANGS = ROOT / 'data' / 'langs.model.xml'
DESKTOP = ROOT / 'packaging' / 'notepadlx.desktop'

# always claimed: plain text, and text types Notepad++ opens through a user defined language (Markdown) or as text
BASE_TYPES = ['text/plain', 'text/markdown', 'text/x-log']

# text-based, but documents of other applications: an editor should not become their handler
EXCLUDED = re.compile(r'^(image/|application/postscript$|application/vnd\.google-earth|application/gpx|application/gml'
                      r'|application/x-mswinurl$|application/x-tiled-|application/x-wais-source$|text/vnd\.trolltech\.linguist$)')


def load_globs(mime_dir):
    globs = {}
    for line in (mime_dir / 'globs2').read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'):
            continue
        parts = line.split(':')
        if len(parts) < 3:
            continue
        weight, mime, glob = int(parts[0]), parts[1], parts[2]
        flags = parts[3] if len(parts) > 3 else ''
        m = re.fullmatch(r'\*\.([^*?\[\]]+)', glob)
        if not m:
            continue
        ext = m.group(1) if 'cs' in flags else m.group(1).lower()
        globs.setdefault(ext, []).append((weight, mime))
    return globs


def load_parents(mime_dir):
    parents = {}
    for line in (mime_dir / 'subclasses').read_text(encoding='utf-8').splitlines():
        child, _, parent = line.partition(' ')
        if parent:
            parents.setdefault(child, set()).add(parent)
    aliases = {}
    for line in (mime_dir / 'aliases').read_text(encoding='utf-8').splitlines():
        alias, _, canonical = line.partition(' ')
        if canonical:
            aliases[alias] = canonical
    return parents, aliases


def is_text(mime, parents, aliases, seen=None):
    mime = aliases.get(mime, mime)
    if mime.startswith('text/'):
        return True
    seen = seen or set()
    if mime in seen:
        return False
    seen.add(mime)
    return any(is_text(p, parents, aliases, seen) for p in parents.get(mime, ()))


def mime_types(mime_dir):
    globs = load_globs(mime_dir)
    parents, aliases = load_parents(mime_dir)
    types = set()
    for lang in ET.parse(LANGS).getroot().iter('Language'):
        for ext in (lang.get('ext') or '').split():
            candidates = globs.get(ext) or globs.get(ext.lower()) or []
            if not candidates:
                continue
            best = max(w for w, _ in candidates)
            for weight, mime in candidates:
                if weight == best and is_text(mime, parents, aliases) and not EXCLUDED.match(mime):
                    types.add(mime)
    return BASE_TYPES + sorted(types - set(BASE_TYPES))


def main():
    args = [a for a in sys.argv[1:] if a != '--check']
    check = '--check' in sys.argv[1:]
    mime_dir = Path(args[0]) if args else Path('/usr/share/mime')
    line = 'MimeType=' + ''.join(t + ';' for t in mime_types(mime_dir))

    desktop = DESKTOP.read_text(encoding='utf-8')
    updated = re.sub(r'^MimeType=.*$', line, desktop, flags=re.M) if re.search(r'^MimeType=', desktop, re.M) \
        else desktop.rstrip('\n') + '\n' + line + '\n'
    if check:
        if updated != desktop:
            print(f'{DESKTOP} is not up to date: run tools/gen_mimetypes.py', file=sys.stderr)
            return 1
        return 0
    DESKTOP.write_text(updated, encoding='utf-8')
    print(f'{DESKTOP}: {line.count(";")} MIME types')
    return 0


if __name__ == '__main__':
    sys.exit(main())
