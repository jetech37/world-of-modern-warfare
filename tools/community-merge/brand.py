"""The branding scrub for the server's text outputs (80_community_zones.sql, scripts.tsv,
displays.txt): the rules and the text rewrite of src/brand.rs (`Rules::scrub_text`), so a spell's
name in spell_template and in Spell.dbc agree. The words are private: they are read from
private-assets/brand-rules.tsv (or --brand-rules <file>); see src/brand.rs for the format.

    python -I tools/community-merge/brand.py [--brand-rules <file>] <file>...   # scrub, then gate
"""

import os
import sys

RULES_FILE = os.path.join("private-assets", "brand-rules.tsv")
URL_CHARS = "./:-_?=&%#~+"
_WS = " \t\n\r\f"


def _lower(s):
    """ASCII-only case folding, as Rust's to_ascii_lowercase."""
    return "".join(c.lower() if c.isascii() else c for c in s)


def _alnum(c):
    return c.isascii() and c.isalnum()


def _keep_case(orig, repl):
    return "".join(r.upper() if o.isascii() and o.isupper() else r for o, r in zip(orig, repl))


def _replace_same_length(s, needle, repl):
    out, i, n = [], 0, len(needle)
    low = _lower(s)
    while i < len(s):
        if low.startswith(needle, i):
            out.append(_keep_case(s[i:i + n], repl))
            i += n
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def _pathish(t):
    return ("\\" in t or "_" in t) and "/" not in t


def _token_at(s, i):
    st = i
    while st > 0 and s[st - 1] not in _WS:
        st -= 1
    en = i
    while en < len(s) and s[en] not in _WS:
        en += 1
    return s[st:en]


class Rules:
    def __init__(self, text):
        self.banned, self.prose, self.addr, self.word, self.same = [], [], [], [], []
        for n, line in enumerate(text.splitlines(), 1):
            if not line.strip() or line.startswith("#"):
                continue
            f = line.split("\t")
            frm = _lower(f[1]) if len(f) > 1 else ""
            if not frm:
                raise ValueError(f"brand rules line {n}: no pattern")
            kind = f[0]
            if kind in ("prose", "word", "same") and len(f) < 3:
                raise ValueError(f"brand rules line {n}: no replacement")
            if kind == "banned":
                self.banned.append(frm)
            elif kind == "addr":
                self.addr.append(frm)
            elif kind == "prose":
                self.prose.append((frm, f[2]))
            elif kind == "word":
                self.word.append((frm, f[2]))
            elif kind == "same":
                if len(f[2]) != len(frm) or not f[2].isascii() or not frm.isascii():
                    raise ValueError(f"brand rules line {n}: a same-length rule must keep its length")
                self.same.append((frm, f[2].lower()))
            elif kind == "screen":
                pass  # archive files only (brand.rs); no text to rewrite here
            else:
                raise ValueError(f"brand rules line {n}: unknown kind {kind!r}")
        for e in [p for p, _ in self.prose + self.word + self.same] + self.addr:
            if e not in self.banned:
                self.banned.append(e)
        if not self.banned:
            raise ValueError("brand rules: no banned strings")

    @classmethod
    def find(cls, path=None):
        """--brand-rules <file>, else private-assets/brand-rules.tsv under the working directory or
        one of its parents. None found is an error."""
        if path:
            with open(path, encoding="utf-8") as f:
                return cls(f.read())
        d = os.getcwd()
        while True:
            p = os.path.join(d, RULES_FILE)
            if os.path.isfile(p):
                with open(p, encoding="utf-8") as f:
                    return cls(f.read())
            up = os.path.dirname(d)
            if up == d:
                raise SystemExit(f"no branding rules: pass --brand-rules <file> or put them at {RULES_FILE} (private)")
            d = up

    def scrub_same_length(self, s):
        for f, t in self.same:
            s = _replace_same_length(s, f, t)
        return s

    def _token(self, t):
        if _pathish(t):
            return self.scrub_same_length(t)
        low = _lower(t)
        if any(k in low for k in self.addr):
            st = next((i for i, c in enumerate(t) if _alnum(c)), len(t))
            en = st
            while en < len(t) and (_alnum(t[en]) or t[en] in URL_CHARS):
                en += 1
            while en > st and t[en - 1] in ".:?!":
                en -= 1
            core = t[st:en]
            if "." in core or "/" in core:
                if st == 0:
                    return None if en >= len(t) else "\0" + t[en:]
                return t[:st] + t[en:]
            b = t
            name = self.prose[0][1] if self.prose else ""
            for k in self.addr:
                while k in _lower(b):
                    i = _lower(b).index(k)
                    b = b[:i] + name + b[i + len(k):]
            r = self._token(b)
            return b if r is None else r
        b = t
        for w, r in self.word:
            frm = 0
            while True:
                i = _lower(b).find(w, frm)
                if i < 0:
                    break
                before = i == 0 or not _alnum(b[i - 1])
                after = i + len(w) >= len(b) or not _alnum(b[i + len(w)])
                if before and after:
                    b = b[:i] + r + b[i + len(w):]
                    frm = i + len(r)
                else:
                    frm = i + len(w)
        words = {w for w, _ in self.word}
        for f, r in self.same:
            if f not in words:
                b = _replace_same_length(b, f, r)
        return b

    def scrub_text(self, s):
        for p, r in self.prose:
            frm = 0
            while True:
                i = _lower(s).find(p, frm)
                if i < 0:
                    break
                tail = s[i + len(p):i + len(p) + 2]
                addr = (any(k in p for k in self.addr) and tail[:1] in (".", "/")
                        and len(tail) == 2 and _alnum(tail[1]))
                if addr or _pathish(_token_at(s, i)):
                    frm = i + len(p)
                    continue
                s = s[:i] + r + s[i + len(p):]
                frm = i + len(r)
        out, i, n = [], 0, len(s)
        ws = _WS
        while i < n:
            if s[i] in ws:
                out.append(s[i])
                i += 1
                continue
            st = i
            while i < n and s[i] not in ws:
                i += 1
            t = self._token(s[st:i])
            if t is None:
                if out and out[-1] == " ":
                    out.pop()
                elif i < n and s[i] == " ":
                    i += 1
            elif t.startswith("\0") and s[st] != "\0":
                if out and out[-1] == " ":
                    out.pop()
                out.append(t[1:])
            else:
                out.append(t)
        return "".join(out)

    def find_banned(self, s):
        low = _lower(s)
        hits = [(low.find(w), w) for w in self.banned if w in low]
        return min(hits) if hits else None

    def scrub_file(self, path):
        """Scrub a text file in place; the first banned string still in it (offset, word) or None."""
        with open(path, encoding="utf-8", newline="") as f:
            s = f.read()
        t = self.scrub_text(s)
        if t != s:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(t)
        return self.find_banned(t)


def main(argv):
    rules_path = None
    if "--brand-rules" in argv:
        i = argv.index("--brand-rules")
        rules_path = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    rules = Rules.find(rules_path)
    bad = 0
    for p in argv:
        h = rules.scrub_file(p)
        if h:
            bad += 1
            print(f"BRANDING LEFT: {p}: {h[1]!r}")
    if bad:
        sys.exit(f"{bad} files still carry branding")
    print(f"brand gate: clean ({len(argv)} files)")


if __name__ == "__main__":
    main(sys.argv[1:])
