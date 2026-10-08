#!/usr/bin/env python3
# esp32-S3-ws4-caravan v1.0
"""Sets the version of esp32-S3-ws4-caravan everywhere.

  python3 tools/set_version.py 1.1

- FW_VERSION in app.h (shown under Settings -> Device -> About, on the web page
  and in the first log line)
- a tag "esp32-S3-ws4-caravan v1.1" on the first line of every source file,
  document and tool, as a comment of the right kind
Without an argument it re-tags every file with the version already in app.h
(useful after adding a file). Run it from the sketch folder or anywhere else.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "esp32-S3-ws4-caravan"
TAG = re.compile(re.escape(NAME) + r" v[0-9][0-9.]*")

COMMENT = {".cpp": "// {}", ".h": "// {}", ".ino": "// {}", ".c": "// {}",
           ".py": "# {}", ".po": "# {}", ".pot": "# {}", ".md": "<!-- {} -->"}

def files():
    for d in ("", "lang", "tools"):
        for f in sorted(os.listdir(os.path.join(ROOT, d))):
            if os.path.splitext(f)[1] in COMMENT:
                yield os.path.join(ROOT, d, f)

def tag(path, version):
    ext = os.path.splitext(path)[1]
    text = open(path, encoding="utf-8").read()
    want = COMMENT[ext].format(f"{NAME} v{version}")
    lines = text.split("\n")
    at = 1 if lines[0].startswith("#!") else 0  # after a script's #! line
    if at < len(lines) and TAG.search(lines[at]):
        if lines[at] == want: return False
        lines[at] = want
    else:
        lines.insert(at, want)
    open(path, "w", encoding="utf-8").write("\n".join(lines))
    return True

def main():
    app = os.path.join(ROOT, "app.h")
    src = open(app, encoding="utf-8").read()
    if len(sys.argv) > 1:
        version = sys.argv[1].lstrip("v")
        src = re.sub(r'#define FW_VERSION "[^"]*"', f'#define FW_VERSION "{version}"', src)
        open(app, "w", encoding="utf-8").write(src)
    else:
        version = re.search(r'#define FW_VERSION "([^"]*)"', src).group(1)
    n = sum(tag(p, version) for p in files())
    print(f"version {version}: {n} file(s) tagged")

if __name__ == "__main__":
    main()
