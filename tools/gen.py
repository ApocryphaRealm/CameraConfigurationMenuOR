"""Generate CCM's shipped data from the source, so neither can drift from the code:

  dist/.../OBSE/Plugins/CameraConfigurationMenu.ini
      from the rows of kTable in src/Settings.cpp (the same defaults the DLL is compiled with - rule 16)
  dist/.../OBSE/Plugins/ApocryphaMenuFramework/Translations/CameraConfigurationMenu_<language>.txt (eleven files)
      from every TR("Key", "English") in src/ for English, and tools/translations.py for the ten other languages
      (UTF-16LE with a BOM, "$CCM_Key<TAB>text", sorted by key - rule 66). A key the table lacks ships its English
      text in that language, and is named on the console.

    python tools/gen.py            write them all
    python tools/gen.py --check    exit 1 if any file on disk differs from what the source says
"""
import io
import os
import re
import sys

sys.dont_write_bytecode = True  # no tools/__pycache__ left behind by the import below
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import translations  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGINS = os.path.join(ROOT, "dist", "OblivionRemastered", "Binaries", "Win64", "OBSE", "Plugins")
INI = os.path.join(PLUGINS, "CameraConfigurationMenu.ini")
TRANS_DIR = os.path.join(PLUGINS, "ApocryphaMenuFramework", "Translations")

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


def english_texts():
    texts = {}
    for dirpath, _, files in os.walk(os.path.join(ROOT, "src")):
        for f in sorted(files):
            if f.endswith((".cpp", ".h")):
                for key, text in TR.findall(io.open(os.path.join(dirpath, f), encoding="utf-8").read()):
                    text = text.replace('\\"', '"')
                    if key in texts and texts[key] != text:
                        sys.exit(f"TR key {key} has two different English texts: {texts[key]!r} / {text!r}")
                    texts[key] = text
    return texts


def translation_bytes(texts):
    lines = [f"$CCM_{k}\t{v}" for k, v in sorted(texts.items())]
    return b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le")


def language_files():
    """[(path, bytes)] for english and the ten languages of translations.LANGS, the key count, the keys that fell
    back to English, and the table keys the source no longer uses."""
    english = english_texts()
    files = [(os.path.join(TRANS_DIR, "CameraConfigurationMenu_english.txt"), translation_bytes(english))]
    for i, lang in enumerate(translations.LANGS):
        texts = {k: (translations.TABLE[k][i] if k in translations.TABLE else v) for k, v in english.items()}
        files.append((os.path.join(TRANS_DIR, f"CameraConfigurationMenu_{lang}.txt"), translation_bytes(texts)))
    fallback = sorted(k for k in english if k not in translations.TABLE)
    unused = sorted(k for k in translations.TABLE if k not in english)
    return files, len(english), fallback, unused


def main():
    check = "--check" in sys.argv
    ini = ini_text().encode("utf-8")
    for key, row in translations.TABLE.items():
        if len(row) != len(translations.LANGS):
            sys.exit(f"translations.py: {key} has {len(row)} texts, not {len(translations.LANGS)}")
    langs, n, fallback, unused = language_files()
    bad = 0
    for path, data in [(INI, ini)] + langs:
        old = open(path, "rb").read() if os.path.exists(path) else None
        if check:
            if old != data:
                print("STALE:", os.path.relpath(path, ROOT))
                bad += 1
        elif old != data:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(data)
            print("wrote", os.path.relpath(path, ROOT))
    print(f"{n} translatable strings x {len(langs)} languages")
    if fallback:
        print(f"{len(fallback)} key(s) not in tools/translations.py, shipped in English in every language:", ", ".join(fallback))
    if unused:
        print(f"{len(unused)} key(s) in tools/translations.py that the source no longer uses:", ", ".join(unused))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
