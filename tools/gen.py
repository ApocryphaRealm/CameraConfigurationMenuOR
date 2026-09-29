"""Generate CCM's shipped data from the source, so neither can drift from the code:

  dist/.../OBSE/Plugins/CameraConfigurationMenu.ini
      from the rows of kTable in src/Settings.cpp (the same defaults the DLL is compiled with - rule 16)
  dist/.../OBSE/Plugins/ApocryphaMenuFramework/Translations/CameraConfigurationMenu_english.txt
      from every TR("Key", "English") in src/ (UTF-16LE with a BOM, "$CCM_Key<TAB>text")

    python tools/gen.py            write both
    python tools/gen.py --check    exit 1 if either file on disk differs from what the source says
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGINS = os.path.join(ROOT, "dist", "OblivionRemastered", "Binaries", "Win64", "OBSE", "Plugins")
INI = os.path.join(PLUGINS, "CameraConfigurationMenu.ini")
TRANS = os.path.join(PLUGINS, "ApocryphaMenuFramework", "Translations", "CameraConfigurationMenu_english.txt")

ROW = re.compile(r'CCM_ROW\("([\w.]+)",\s*"(\w+)",\s*k(Bool|Int|Float),\s*[\w.\[\]]+,\s*([-0-9.]+),\s*[-0-9.]+,\s*[-0-9.]+,\s*"((?:[^"\\]|\\.)*)"\)')
TR = re.compile(r'TR\("(\w+)",\s*"((?:[^"\\]|\\.)*)"\)')


def ini_text():
    src = io.open(os.path.join(ROOT, "src", "Settings.cpp"), encoding="utf-8").read()
    rows = ROW.findall(src)
    # a row the pattern cannot read would silently vanish from the shipped INI (2026-09-29: [Framing.Sneaking]'s dotted
    # section and fmGroups[3].side dropped every Framing row until the pattern learned them)
    declared = src.count('CCM_ROW("')
    if len(rows) != declared:
        sys.exit(f"gen.py read {len(rows)} of the {declared} CCM_ROW lines in Settings.cpp - fix the ROW pattern")
    if not rows:
        sys.exit("no CCM_ROW rows found in src/Settings.cpp")
    out = ["; CCM - Camera Configuration Menu (Oblivion Remastered). Every setting is also on the CCM page of the",
           "; Apocrypha Menu Framework, which rewrites this file; edits made while the game is closed are read at start."]
    section = None
    for sec, key, kind, default, comment in rows:
        if sec != section:
            section = sec
            out += ["", f"[{sec}]"]
        if comment:
            out.append("; " + comment.replace('\\"', '"'))
        x = float(default)
        out.append(f"{key}=" + (("1" if x else "0") if kind == "Bool" else str(int(x)) if kind == "Int" else f"{x:.2f}"))
    return "\r\n".join(out) + "\r\n"


def english_bytes():
    texts = {}
    for dirpath, _, files in os.walk(os.path.join(ROOT, "src")):
        for f in sorted(files):
            if f.endswith((".cpp", ".h")):
                for key, text in TR.findall(io.open(os.path.join(dirpath, f), encoding="utf-8").read()):
                    text = text.replace('\\"', '"')
                    if key in texts and texts[key] != text:
                        sys.exit(f"TR key {key} has two different English texts: {texts[key]!r} / {text!r}")
                    texts[key] = text
    lines = [f"$CCM_{k}\t{v}" for k, v in sorted(texts.items())]
    return b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"), len(texts)


def main():
    check = "--check" in sys.argv
    ini = ini_text().encode("utf-8")
    eng, n = english_bytes()
    bad = 0
    for path, data in ((INI, ini), (TRANS, eng)):
        old = open(path, "rb").read() if os.path.exists(path) else None
        if check:
            if old != data:
                print("STALE:", os.path.relpath(path, ROOT))
                bad += 1
        elif old != data:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(data)
            print("wrote", os.path.relpath(path, ROOT))
    print(f"{n} translatable strings")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
