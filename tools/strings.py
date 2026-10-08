# esp32-S3-ws4-caravan v1.0
"""Finds string-literal groups in C++ sources and the call each one is an argument of.
A group is adjacent string literals and macro names (LV_SYMBOL_OK "  Text" DEG ...).

  python3 strings.py list FILE...         file:line  call  group
  python3 strings.py wrap CALLS FILE...   wraps groups that are arguments of CALLS (comma list) in TR(...)
  python3 strings.py keys FILE...         every group inside TR(...) or a translated call, one per line
"""
import re, sys

LIT = r'"(?:[^"\\\n]|\\.)*"'
MAC = r'(?:LV_SYMBOL_[A-Z0-9_]+|DEG|FW_VERSION|__DATE__|__TIME__|MDNS_NAME|NO_NET_TEXT|NO_NET_FOUND|PCF_ADDR_OPTS)'
GROUP = re.compile(r'(?:' + LIT + r'|' + MAC + r')(?:\s*(?:' + LIT + r'|' + MAC + r'))*')
# calls whose text goes through tr() already (macros in app.h or helpers)
TRANSLATED = {"lv_label_set_text", "lv_label_set_text_fmt", "lv_textarea_set_placeholder_text",
              "lv_dropdown_set_options", "lv_dropdown_set_options_static", "lv_tabview_add_tab",
              "make_btn", "make_section", "make_heading", "make_switch_row", "make_ta", "set_label",
              "settings_page", "TR", "tr", "confirm"}

def strip_comments(src):
    # keep offsets: replace comments by spaces
    out = list(src)
    for m in re.finditer(r'//[^\n]*|/\*.*?\*/|' + LIT + r"|'(?:[^'\\]|\\.)'", src, re.S):
        t = m.group(0)
        if t.startswith("//") or t.startswith("/*"):
            for i in range(m.start(), m.end()):
                if out[i] != "\n": out[i] = " "
    return "".join(out)

def groups(src):
    clean = strip_comments(src)
    for m in GROUP.finditer(clean):
        g = m.group(0)
        if '"' not in g: continue
        # skip #include / #define lines (but not text marked N_() in a #define)
        ls = clean.rfind("\n", 0, m.start()) + 1
        line = clean[ls:clean.find("\n", m.start())]
        if line.lstrip().startswith("#") and "N_(" not in line: continue
        # enclosing call, and which argument of it (0-based) the group is in
        depth, i = 0, m.start() - 1
        call, arg = "", 0
        while i >= 0:
            c = clean[i]
            if c == "," and depth == 0: arg += 1
            if c == ")": depth += 1
            elif c == "(":
                if depth == 0:
                    j = i - 1
                    while j >= 0 and clean[j] == " ": j -= 1
                    k = j
                    while k >= 0 and (clean[k].isalnum() or clean[k] in "_:."): k -= 1
                    call = clean[k + 1:j + 1]
                    break
                depth -= 1
            elif c in ";{}" and depth == 0:
                call = "{" if c == "{" else ""
                break
            i -= 1
        yield m.start(), m.end(), g, call, src.count("\n", 0, m.start()) + 1, arg

def has_words(g):
    text = "".join(re.findall(LIT, g))
    return re.search(r'[A-Za-z]{2,}', re.sub(r'%[-+ #0-9.]*[a-zA-Z]', '', text)) is not None

if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "list":
        for f in sys.argv[2:]:
            for s, e, g, call, ln, arg in groups(open(f).read()):
                if has_words(g): print(f"{f}:{ln}\t{call}\t{g}")
    elif mode == "wrap":
        # CALLS: name or name:N (only arguments N and later)
        calls = {}
        for c in sys.argv[2].split(","):
            name, _, n = c.partition(":")
            calls[name] = int(n or 0)
        for f in sys.argv[3:]:
            src = open(f).read()
            edits = [(s, e, g) for s, e, g, call, ln, arg in groups(src) if call in calls and arg >= calls[call] and has_words(g)]
            for s, e, g in reversed(edits):
                src = src[:s] + "TR(" + g + ")" + src[e:]
            open(f, "w").write(src)
            print(f, len(edits))
    elif mode == "keys":
        seen = []
        for f in sys.argv[2:]:
            for s, e, g, call, ln, arg in groups(open(f).read()):
                if call in TRANSLATED and has_words(g) and g not in seen: seen.append(g)
        for g in seen: print(g)
