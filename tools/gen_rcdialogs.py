#!/usr/bin/env python3
# This file is part of NotePadLX, a Linux port of Notepad++.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option any later version.
#
# Generates src/ui/RcDialogs.inc from upstream DIALOGEX resources (*.rc), so the Qt port builds every
# dialog with the upstream controls, IDs and positions (see src/ui/RcDialog.h).
#
# Usage: tools/gen_rcdialogs.py <upstream PowerEditor/src> <output .inc> DIALOG_ID[@file.rc] ...

import re
import sys
from pathlib import Path

KEYWORDS = {'LTEXT', 'RTEXT', 'CTEXT', 'EDITTEXT', 'COMBOBOX', 'LISTBOX', 'PUSHBUTTON', 'DEFPUSHBUTTON',
            'GROUPBOX', 'CONTROL', 'ICON', 'SCROLLBAR', 'AUTOCHECKBOX', 'AUTORADIOBUTTON', 'CHECKBOX', 'RADIOBUTTON'}

# style keywords -> RcStyle bits (src/ui/RcDialog.h)
STYLE_BITS = {
    'BS_AUTOCHECKBOX': 'RcCheckBox', 'BS_CHECKBOX': 'RcCheckBox', 'BS_3STATE': 'RcCheckBox|RcTriState',
    'BS_AUTO3STATE': 'RcCheckBox|RcTriState', 'BS_AUTORADIOBUTTON': 'RcRadio', 'BS_RADIOBUTTON': 'RcRadio',
    'BS_GROUPBOX': 'RcGroupBox', 'BS_DEFPUSHBUTTON': 'RcDefault', 'BS_SPLITBUTTON': 'RcSplitButton',
    'BS_PUSHLIKE': 'RcPushLike', 'BS_OWNERDRAW': 'RcOwnerDraw',
    'ES_MULTILINE': 'RcMultiLine', 'ES_READONLY': 'RcReadOnly', 'ES_NUMBER': 'RcNumber', 'ES_PASSWORD': 'RcPassword',
    'ES_CENTER': 'RcCenter', 'ES_RIGHT': 'RcRight', 'ES_WANTRETURN': 'RcWantReturn',
    'SS_CENTER': 'RcCenter', 'SS_RIGHT': 'RcRight', 'SS_CENTERIMAGE': 'RcVCenter', 'SS_NOTIFY': 'RcNotify',
    'SS_SUNKEN': 'RcSunken', 'SS_ETCHEDHORZ': 'RcEtchedLine', 'SS_ETCHEDVERT': 'RcEtchedLine',
    'CBS_DROPDOWNLIST': 'RcDropDownList', 'CBS_DROPDOWN': 'RcDropDown', 'CBS_SORT': 'RcSort', 'LBS_SORT': 'RcSort',
    'WS_VSCROLL': 'RcVScroll', 'WS_HSCROLL': 'RcHScroll', 'WS_DISABLED': 'RcDisabled', 'TBS_VERT': 'RcVertical',
    'LVS_REPORT': 'RcReport', 'BS_CENTER': 'RcCenter', 'BS_RIGHT': 'RcRight', 'BS_LEFTTEXT': 'RcLeftText',
    'WS_GROUP': 'RcGroupStart',
}


def tokenize(text):
    tokens, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif text.startswith('//', i):  # a comment, to the end of the line
            while i < n and text[i] != '\n':
                i += 1
        elif c == ',':
            tokens.append(','); i += 1
        elif c == '"' or (c == 'L' and i + 1 < n and text[i + 1] == '"'):
            if c == 'L':
                i += 1
            j, s = i + 1, ''
            while j < n:
                if text[j] == '"':
                    if j + 1 < n and text[j + 1] == '"':
                        s += '"'; j += 2; continue
                    break
                s += text[j]; j += 1
            tokens.append(('str', s)); i = j + 1
        else:
            j = i
            while j < n and text[j] not in ',"' and not text[j].isspace():
                j += 1
            tokens.append(text[i:j]); i = j
    return tokens


def split_args(tokens):
    args, cur = [], []
    for t in tokens:
        if t == ',':
            args.append(cur); cur = []
        else:
            cur.append(t)
    args.append(cur)
    return args


def style_of(arg):
    bits, hidden, words = [], False, [t for t in arg if isinstance(t, str)]
    k = 0
    while k < len(words):
        w = words[k]
        if w == 'NOT' and k + 1 < len(words):
            if words[k + 1] == 'WS_VISIBLE':
                hidden = True
            k += 2
            continue
        if w in STYLE_BITS:
            bits.append(STYLE_BITS[w])
        k += 1
    if hidden:
        bits.append('RcHidden')
    return bits


def num(arg):
    expr = ' '.join(t for t in arg if isinstance(t, str))
    try:
        return int(eval(expr, {}, {}))
    except Exception:
        return expr  # symbolic


def cstr(s):
    # RC strings use the C escapes already (\\n, \\t, \\\\); only a quote ("" in the .rc) needs one
    return 'L"' + s.replace('"', '\\"') + '"'


def parse_dialog(lines, start):
    header = lines[start]
    m = re.match(r'\s*(\w+)\s+DIALOGEX\s+(.*)', header)
    name = m.group(1)
    dims = [x.strip() for x in m.group(2).split(',')]
    w, h = int(dims[2]), int(dims[3])
    caption = ''
    i = start + 1
    while not re.match(r'\s*(BEGIN|\{)\s*$', lines[i]):
        cm = re.match(r'\s*CAPTION\s+L?"(.*)"\s*$', lines[i])
        if cm:
            caption = cm.group(1).replace('""', '"')
        i += 1
    i += 1
    stmts, cur = [], None
    while not re.match(r'\s*(END|\})\s*$', lines[i]):
        line = lines[i].split('//')[0] if '"' not in lines[i] else lines[i]
        stripped = line.strip()
        if stripped and not stripped.startswith('#'):
            first = stripped.split()[0]
            if first in KEYWORDS:
                if cur:
                    stmts.append(cur)
                cur = stripped
            elif cur is not None:
                cur += ' ' + stripped
        i += 1
    if cur:
        stmts.append(cur)

    controls = []
    for st in stmts:
        kw = st.split()[0]
        args = split_args(tokenize(st[len(kw):]))
        text, cls, style = '', '', []
        try:
            if kw in ('LTEXT', 'RTEXT', 'CTEXT', 'PUSHBUTTON', 'DEFPUSHBUTTON', 'GROUPBOX', 'AUTOCHECKBOX',
                      'AUTORADIOBUTTON', 'CHECKBOX', 'RADIOBUTTON'):
                text = args[0][0][1] if args[0] and isinstance(args[0][0], tuple) else ''
                cid, x, y, cw, ch = args[1][0], num(args[2]), num(args[3]), num(args[4]), num(args[5])
                style = style_of(args[6]) if len(args) > 6 else []
                kind = {'LTEXT': 'RcLabel', 'RTEXT': 'RcLabel', 'CTEXT': 'RcLabel', 'PUSHBUTTON': 'RcButton',
                        'DEFPUSHBUTTON': 'RcButton', 'GROUPBOX': 'RcGroup', 'AUTOCHECKBOX': 'RcButton',
                        'CHECKBOX': 'RcButton', 'AUTORADIOBUTTON': 'RcButton', 'RADIOBUTTON': 'RcButton'}[kw]
                if kw == 'RTEXT': style.append('RcRight')
                if kw == 'CTEXT': style.append('RcCenter')
                if kw == 'DEFPUSHBUTTON': style.append('RcDefault')
                if kw in ('AUTOCHECKBOX', 'CHECKBOX'): style.append('RcCheckBox')
                if kw in ('AUTORADIOBUTTON', 'RADIOBUTTON'): style.append('RcRadio')
            elif kw in ('EDITTEXT', 'COMBOBOX', 'LISTBOX', 'SCROLLBAR'):
                cid, x, y, cw, ch = args[0][0], num(args[1]), num(args[2]), num(args[3]), num(args[4])
                style = style_of(args[5]) if len(args) > 5 else []
                kind = {'EDITTEXT': 'RcEdit', 'COMBOBOX': 'RcCombo', 'LISTBOX': 'RcList', 'SCROLLBAR': 'RcOther'}[kw]
            elif kw == 'CONTROL':
                text = args[0][0][1] if args[0] and isinstance(args[0][0], tuple) else ''
                cid = args[1][0]
                cls = args[2][0][1] if isinstance(args[2][0], tuple) else args[2][0]
                style = style_of(args[3])
                x, y, cw, ch = num(args[4]), num(args[5]), num(args[6]), num(args[7])
                lc = cls.lower()
                if lc == 'button':
                    kind = 'RcGroup' if 'RcGroupBox' in style else 'RcButton'
                elif lc == 'static':
                    kind = 'RcLabel'
                elif lc == 'edit':
                    kind = 'RcEdit'
                elif lc == 'combobox':
                    kind = 'RcCombo'
                elif lc == 'listbox':
                    kind = 'RcList'
                elif lc == 'msctls_trackbar32':
                    kind = 'RcSlider'
                elif lc == 'msctls_updown32':
                    kind = 'RcSpin'
                elif lc == 'systabcontrol32':
                    kind = 'RcTab'
                elif lc == 'syslistview32':
                    kind = 'RcListView'
                elif lc == 'systreeview32':
                    kind = 'RcTree'
                else:
                    kind = 'RcOther'
            elif kw == 'ICON':
                continue
            else:
                continue
        except (IndexError, KeyError) as e:
            sys.exit('cannot parse in %s: %s (%s)' % (name, st, e))
        if isinstance(cid, tuple):
            cid = cid[1]
        controls.append((kind, text, cid, x, y, cw, ch, style, cls))
    return name, w, h, caption, controls


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2])
    wanted = sys.argv[3:]
    rc_files = sorted(src.rglob('*.rc'))
    dialogs = {}
    for rc in rc_files:
        raw = rc.read_bytes()
        text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8-sig', errors='replace')
        lines = text.replace('\r\n', '\n').split('\n')
        for i, l in enumerate(lines):
            m = re.match(r'\s*(\w+)\s+DIALOGEX\s', l)
            if m and m.group(1) in wanted and m.group(1) not in dialogs:
                dialogs[m.group(1)] = (rc, parse_dialog(lines, i))

    missing = [d for d in wanted if d not in dialogs]
    if missing:
        sys.exit('dialogs not found: ' + ', '.join(missing))

    o = ['// Generated by tools/gen_rcdialogs.py from upstream Notepad++ .rc files - do not edit', '']
    for d in wanted:
        rc, (name, w, h, caption, controls) = dialogs[d]
        o.append('// %s (%s)' % (name, rc.relative_to(src)))
        o.append('inline constexpr RcControlDef %s_controls[] = {' % name)
        for kind, text, cid, x, y, cw, ch, style, cls in controls:
            st = '|'.join(style) if style else '0'
            o.append('\t{ %s, %s, %s, %s, %s, %s, %s, %s, "%s" },' % (kind, cstr(text), cid, x, y, cw, ch, st, cls))
        o.append('};')
        o.append('inline constexpr RcDialogDef %s_def = { "%s", %d, %d, %s, %s_controls, %d };'
                 % (name, name, w, h, cstr(caption), name, len(controls)))
        o.append('')
    out.write_text('\n'.join(o))


if __name__ == '__main__':
    main()
