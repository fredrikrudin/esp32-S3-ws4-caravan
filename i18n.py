#!/usr/bin/env python3
# esp32-S3-ws4-caravan v1.0
"""Translations for esp32-S3-ws4-caravan, as gettext .po files (edit them with Poedit).

  python3 tools/i18n.py pot     scan the sources, write lang/caravan.pot (the template)
  python3 tools/i18n.py update  bring every lang/<code>.po in line with the template: new texts are
                                added untranslated, texts no longer in the code are kept at the end
                                as obsolete (#~), like Poedit's "Update from POT File"
  python3 tools/i18n.py build   check every lang/<code>.po, write lang_tables.h (compiled in)
  python3 tools/i18n.py         pot, update and build

Run it from the sketch folder (or anywhere: paths are relative to this file).
Adding a language: Poedit -> File -> New from POT/PO file -> lang/caravan.pot,
pick the language, translate, save as lang/<code>.po (e.g. de.po), then run
`python3 tools/i18n.py build` and upload the sketch again.
After changing text in the code: run `python3 tools/i18n.py` (or `pot`, then in
Poedit open each .po and use Translation -> Update from POT File), translate
what is new, and build.

What is extracted: text given to the LVGL calls and helpers that translate
(lv_label_set_text, make_btn, make_section, set_label, ...), the format of
lv_label_set_text_fmt, and anything inside TR("...") or N_("..."). Leading
spaces, line breaks and LVGL symbols are left out of the text, as tr() does.
"""
import os, re, sys, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANG_DIR = os.path.join(ROOT, "lang")
POT = os.path.join(LANG_DIR, "caravan.pot")
OUT_H = os.path.join(ROOT, "lang_tables.h")
SKIP_FILES = {"web.cpp", "lang_tables.h"}

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from strings import groups, has_words, LIT  # noqa: E402

# Which argument of a call is the text that gets translated
TEXT_ARG = {"lv_label_set_text": 1, "lv_label_set_text_fmt": 1, "lv_textarea_set_placeholder_text": 1,
            "lv_dropdown_set_options": 1, "lv_dropdown_set_options_static": 1, "lv_tabview_add_tab": 1,
            "make_btn": 1, "make_section": 1, "make_heading": 1, "make_switch_row": 1, "make_ta": 1,
            "set_label": 1, "settings_page": 1, "TR": 0, "tr": 0, "N_": 0, "confirm": (0, 1)}
MACROS = {"DEG": "\u00b0"}
DYNAMIC = ("FW_VERSION", "__DATE__", "__TIME__", "MDNS_NAME")  # text that changes: not translatable

LANG_NAMES = {"da": "Dansk", "de": "Deutsch", "en": "English", "es": "Español", "fi": "Suomi",
              "fr": "Français", "is": "Íslenska", "it": "Italiano", "nb": "Norsk bokmål",
              "nl": "Nederlands", "nn": "Norsk nynorsk", "no": "Norsk", "pt": "Português", "sv": "Svenska"}

# ---------------------------------------------------------------- C strings
def c_unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "x":
                m = re.match(r"[0-9a-fA-F]+", s[i + 2:])
                out.append(chr(int(m.group(0), 16)))
                i += 2 + len(m.group(0))
                continue
            out.append({"n": "\n", "t": "\t", "r": "\r", "0": "\0"}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    # \xC2\xB0 style escapes were bytes: re-decode
    try:
        return "".join(out).encode("latin-1").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        return "".join(out)

def evaluate(group):
    """The runtime text of a group of literals and macros, or None if it changes at run time."""
    parts = re.findall(LIT + r"|[A-Za-z_][A-Za-z0-9_]*", group)
    text = ""
    for p in parts:
        if p.startswith('"'):
            text += c_unescape(p[1:-1])
        elif p.startswith("LV_SYMBOL_"):
            text += "\uf000"  # any symbol: stripped below if leading
        elif p in MACROS:
            text += MACROS[p]
        elif p in DYNAMIC:
            return None
        elif p == "N_":
            continue
        else:
            return None
    return text

def strip_prefix(t):
    i = 0
    while i < len(t) and (t[i] in " \n" or 0xF000 <= ord(t[i]) <= 0xF8FF):
        i += 1
    return t[i:]

def c_quote(s):
    out = []
    for ch in s:
        if ch == "\\": out.append("\\\\")
        elif ch == '"': out.append('\\"')
        elif ch == "\n": out.append("\\n")
        elif ch == "\t": out.append("\\t")
        elif ord(ch) < 0x20: out.append("\\x%02x" % ord(ch))
        else: out.append(ch)
    return '"' + "".join(out) + '"'

def po_quote(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t")
    if "\n" in s[:-1]:
        lines = s.split("\n")
        chunks = [l + "\\n" for l in lines[:-1]] + ([lines[-1]] if lines[-1] else [])
        return '""\n' + "\n".join('"%s"' % c for c in chunks)
    return '"' + s.replace("\n", "\\n") + '"'

# ---------------------------------------------------------------- extract
def sources():
    for f in sorted(os.listdir(ROOT)):
        if f.endswith((".cpp", ".h", ".ino")) and f not in SKIP_FILES and not f.startswith("lang_"):
            yield f

def extract():
    found = {}  # text -> [refs]
    for f in sources():
        src = open(os.path.join(ROOT, f), encoding="utf-8").read()
        for s, e, g, call, ln, arg in groups(src):
            want = TEXT_ARG.get(call)
            if want is None or not has_words(g): continue
            if (arg not in want) if isinstance(want, tuple) else (arg != want): continue
            text = evaluate(g)
            if text is None: continue
            text = strip_prefix(text)
            if not re.search(r"[A-Za-z]{2,}", re.sub(r"%[-+ #0-9.]*[a-zA-Z]", "", text)): continue
            found.setdefault(text, []).append(f"{f}:{ln}")
    return found

def write_pot(found):
    os.makedirs(LANG_DIR, exist_ok=True)
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M+0000")
    out = ['# ' + version_tag(), '# Texts shown on the display of esp32-S3-ws4-caravan.',
           '# Generated by tools/i18n.py from the source code - do not edit; translate in a .po file.',
           'msgid ""', 'msgstr ""',
           '"Project-Id-Version: esp32-S3-ws4-caravan\\n"',
           f'"POT-Creation-Date: {now}\\n"',
           '"MIME-Version: 1.0\\n"', '"Content-Type: text/plain; charset=UTF-8\\n"',
           '"Content-Transfer-Encoding: 8bit\\n"', '']
    for text, refs in found.items():
        if specs(text):
            out.append("#, c-format")
        for i in range(0, len(refs), 4):
            out.append("#: " + " ".join(refs[i:i + 4]))
        out.append("msgid " + po_quote(text))
        out.append('msgstr ""')
        out.append("")
    open(POT, "w", encoding="utf-8").write("\n".join(out))
    print(f"{POT}: {len(found)} texts")

# ---------------------------------------------------------------- .po files
def read_po(path):
    """[(msgid, msgstr, fuzzy)], header dict"""
    entries, header = [], {}
    cur = {"id": None, "str": None, "fuzzy": False, "plural": False}
    key = None
    def flush():
        nonlocal cur
        if cur["id"] is not None and cur["str"] is not None and not cur["plural"]:
            if cur["id"] == "":
                for line in cur["str"].split("\n"):
                    if ":" in line:
                        k, v = line.split(":", 1)
                        header[k.strip()] = v.strip()
            else:
                entries.append((cur["id"], cur["str"], cur["fuzzy"]))
        cur = {"id": None, "str": None, "fuzzy": False, "plural": False}
    for raw in open(path, encoding="utf-8"):
        l = raw.strip()
        if l.startswith("#,"):
            if cur["str"] is not None: flush()
            if "fuzzy" in l: cur["fuzzy"] = True
        elif l.startswith("#~") or (l.startswith("#") and not l.startswith("#,")):
            continue
        elif l.startswith("msgctxt"):
            if cur["str"] is not None: flush()
            key = None
        elif l.startswith("msgid_plural"):
            cur["plural"] = True
            key = None
        elif l.startswith("msgid "):
            if cur["str"] is not None: flush()
            cur["id"] = c_unescape(l[7:-1]); key = "id"
        elif l.startswith("msgstr[0] ") or l.startswith("msgstr "):
            q = l[l.index('"') + 1:-1]
            cur["str"] = c_unescape(q); key = "str"
        elif l.startswith('"') and key:
            cur[key] += c_unescape(l[1:-1])
        elif not l:
            flush(); key = None
    flush()
    return entries, header

def update():
    """Like msgmerge: each .po gets the template's texts and references, keeping translations."""
    pot_text = open(POT, encoding="utf-8").read()
    pot_blocks = [b for b in pot_text.split("\n\n") if "msgid" in b][1:]
    pot_entries = read_po(POT)[0]
    for po in sorted(f for f in os.listdir(LANG_DIR) if f.endswith(".po")):
        path = os.path.join(LANG_DIR, po)
        text = open(path, encoding="utf-8").read()
        header = text.split("\n\n")[0]
        old = {m: (t, fz) for m, t, fz in read_po(path)[0]}
        out, used, new = [header], set(), 0
        for block, (msgid, _, _) in zip(pot_blocks, pot_entries):
            comments = [l for l in block.strip("\n").split("\n") if l.startswith("#")]
            t, fz = old.get(msgid, ("", False))
            if msgid in old: used.add(msgid)
            else: new += 1
            if fz: comments = [c + ", fuzzy" if c.startswith("#,") else c for c in comments] or ["#, fuzzy"]
            if fz and not any(c.startswith("#,") for c in comments): comments.insert(0, "#, fuzzy")
            out.append("\n".join(comments + ["msgid " + po_quote(msgid), "msgstr " + po_quote(t)]))
        obsolete = [(m, t) for m, (t, fz) in old.items() if m not in used and t]
        for m, t in obsolete:
            out.append("\n".join("#~ " + l for l in ("msgid " + po_quote(m) + "\nmsgstr " + po_quote(t)).split("\n")))
        open(path, "w", encoding="utf-8").write("\n\n".join(out) + "\n")
        print(f"{po}: {new} new, {len(obsolete)} obsolete")

SPEC = re.compile(r"%(?:%|[-+ #0]*[0-9]*(?:\.[0-9]+)?(?:hh|h|ll|l|z)?[diouxXfFeEgGcsp])")

def specs(s):
    return [x for x in SPEC.findall(s) if x != "%%"]

def version_tag():
    m = re.search(r'#define FW_VERSION "([^"]*)"', open(os.path.join(ROOT, "app.h"), encoding="utf-8").read())
    return f"esp32-S3-ws4-caravan v{m.group(1)}" if m else "esp32-S3-ws4-caravan"

def build():
    pos = sorted(f for f in os.listdir(LANG_DIR) if f.endswith(".po"))
    pot_ids = set(t for t, _, _ in read_po(POT)[0]) if os.path.exists(POT) else None
    out = ["// " + version_tag(), "/* Built-in translations, generated by tools/i18n.py from the lang/<code>.po files - do not edit.",
           "   Change the .po files with Poedit and run: python3 tools/i18n.py build */", ""]
    langs, problems = [], 0
    for po in pos:
        code = po[:-3]
        entries, header = read_po(os.path.join(LANG_DIR, po))
        name = header.get("X-Language-Name") or LANG_NAMES.get(code, code)
        ok, fuzzy, empty, bad = [], 0, 0, 0
        for msgid, msgstr, fz in entries:
            if not msgstr: empty += 1; continue
            if fz: fuzzy += 1; continue
            if specs(msgid) != specs(msgstr):
                print(f"  {po}: placeholders differ, left out:\n    {msgid!r}\n    {msgstr!r}")
                bad += 1; continue
            ok.append((msgid, msgstr))
        problems += bad
        missing = len(pot_ids - set(m for m, _ in ok)) if pot_ids is not None else 0
        print(f"{po}: {name}, {len(ok)} translated, {fuzzy} fuzzy, {empty} empty, {bad} bad, {missing} of the template not translated")
        ident = "tr_" + re.sub(r"\W", "_", code)
        out.append(f"static const TrPair {ident}[] = {{")
        for msgid, msgstr in ok:
            out.append(f"  {{ {c_quote(msgid)}, {c_quote(msgstr)} }},")
        if not ok: out.append('  { "", "" },')
        out.append("};")
        out.append("")
        langs.append((code, name, ident, max(len(ok), 1)))
    out.append("static const BuiltIn builtin[] = {")
    for code, name, ident, n in langs:
        out.append(f"  {{ {c_quote(code)}, {c_quote(name)}, {ident}, {n} }},")
    if not langs: out.append('  { "", "", NULL, 0 },')
    out.append("};")
    out.append(f"static const int BUILTIN_COUNT = {len(langs)};")
    open(OUT_H, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print(f"{OUT_H}: {len(langs)} language(s)")
    return problems

if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("pot", "all"): write_pot(extract())
    if what in ("update", "all"): update()
    if what in ("build", "all"): sys.exit(1 if build() else 0)
