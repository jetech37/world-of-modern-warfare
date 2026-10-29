#!/usr/bin/env python3
"""Turn the devlog's Markdown posts (docs/devlog/*.md) into the site's static pages.

    python tools/build-devlog.py          # writes docs/devlog/<slug>.html and docs/devlog/index.html
    python tools/build-devlog.py --check  # exit 1 if a page is out of date (nothing written)

GitHub Pages serves docs/ as it is (no build step there): run this after editing a post and
commit the Markdown with the HTML it makes. Standard library only.

A post is `docs/devlog/<slug>.md` (the slug is its URL: `YYYY-MM-DD-some-words`), starting with
a header block:

    ---
    title: The story so far
    date: 2026-10-19
    summary: One or two sentences for the index.
    draft: true
    ---

A draft gets a DRAFT banner and `noindex`, and is left out of the index (and so of every link):
only someone given its address sees it. Set `draft: false` (or remove the line) to publish it.

The Markdown is a small, plain subset: `#`-`###` headings, paragraphs, `-` and `1.` lists,
`>` quotes, ``` fenced code, `**bold**`, `_italic_`, `` `code` ``, `[text](url)` and
`![alt](src)` images, and `---` rules. Raw HTML is escaped, never passed through.
"""
import html
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVLOG = os.path.join(ROOT, "docs", "devlog")
SITE = "World of Modern Warfare"


def inline(text: str) -> str:
    """Escape, then the inline marks: code first (nothing inside it is marked), images, links,
    bold, italics."""
    parts = re.split(r"(`[^`]+`)", text)
    out = []
    for part in parts:
        if part.startswith("`") and part.endswith("`") and len(part) > 1:
            out.append(f"<code>{html.escape(part[1:-1])}</code>")
            continue
        s = html.escape(part, quote=True)
        s = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", r'<img src="\2" alt="\1" loading="lazy">', s)
        s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", r'<a href="\2">\1</a>', s)
        s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
        s = re.sub(r"(?<![\w*])_([^_]+)_(?![\w])", r"<em>\1</em>", s)
        out.append(s)
    return "".join(out)


def markdown(text: str) -> str:
    """The subset above, block by block."""
    lines = text.replace("\r\n", "\n").split("\n")
    out = []
    para: list[str] = []
    i = 0

    def flush():
        if para:
            out.append(f"<p>{inline(' '.join(s.strip() for s in para))}</p>")
            para.clear()

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            flush()
            i += 1
            continue
        if stripped.startswith("```"):
            flush()
            code = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i])
                i += 1
            out.append(f"<pre><code>{html.escape(chr(10).join(code))}</code></pre>")
            i += 1
            continue
        m = re.match(r"^(#{1,3})\s+(.*)$", stripped)
        if m:
            flush()
            level = max(2, len(m.group(1)))  # the page's title is the h1: # and ## are h2
            out.append(f"<h{level}>{inline(m.group(2))}</h{level}>")
            i += 1
            continue
        if re.match(r"^(-{3,}|\*{3,})$", stripped):
            flush()
            out.append("<hr>")
            i += 1
            continue
        if stripped.startswith(">"):
            flush()
            quote = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                quote.append(lines[i].strip()[1:].strip())
                i += 1
            out.append(f"<blockquote>{markdown(chr(10).join(quote))}</blockquote>")
            continue
        m = re.match(r"^([-*]|\d+\.)\s+(.*)$", stripped)
        if m:
            flush()
            tag = "ol" if m.group(1)[0].isdigit() else "ul"
            items = []
            while i < len(lines):
                m = re.match(r"^\s*([-*]|\d+\.)\s+(.*)$", lines[i])
                if m:
                    items.append(m.group(2).strip())
                elif lines[i].startswith("  ") and lines[i].strip() and items:
                    items[-1] += " " + lines[i].strip()  # a wrapped item
                else:
                    break
                i += 1
            out.append(f"<{tag}>" + "".join(f"<li>{inline(it)}</li>" for it in items) + f"</{tag}>")
            continue
        para.append(line)
        i += 1
    flush()
    return "\n".join(out)


def read_post(path: str) -> dict:
    text = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    meta = {}
    if text.startswith("---\n"):
        end = text.index("\n---\n", 4)
        for line in text[4:end].split("\n"):
            if ":" in line:
                k, v = line.split(":", 1)
                meta[k.strip().lower()] = v.strip()
        text = text[end + 5:]
    slug = os.path.splitext(os.path.basename(path))[0]
    if not re.match(r"^\d{4}-\d{2}-\d{2}-[a-z0-9-]+$", slug):
        sys.exit(f"{path}: a post's file name is YYYY-MM-DD-words.md")
    return {
        "slug": slug,
        "title": meta.get("title") or slug,
        "date": meta.get("date") or slug[:10],
        "summary": meta.get("summary", ""),
        "draft": meta.get("draft", "false").lower() in ("true", "yes", "1"),
        "body": markdown(text),
    }


def page(title: str, body: str, description: str = "", noindex: bool = False) -> str:
    robots = '<meta name="robots" content="noindex, nofollow">\n' if noindex else ""
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title>
{robots}<meta name="description" content="{html.escape(description)}">
<link rel="icon" href="../favicon.ico" sizes="any">
<link rel="icon" type="image/png" href="../icon.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Cinzel:wght@600;800&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@500&display=swap" rel="stylesheet">
<link rel="stylesheet" href="devlog.css">
</head>
<body>
<div class="wrap">
{body}
  <footer>A non-commercial fan made project · <a href="../">womw.net</a> · <a href="https://discord.gg/PgzdqFmHv">Discord</a> · <a href="https://www.reddit.com/r/womw/">r/womw</a></footer>
</div>
</body>
</html>
"""


def nice_date(d: str) -> str:
    months = ["January", "February", "March", "April", "May", "June", "July", "August",
              "September", "October", "November", "December"]
    m = re.match(r"^(\d{4})-(\d{2})-(\d{2})$", d)
    return f"{int(m.group(3))} {months[int(m.group(2)) - 1]} {m.group(1)}" if m else d


def post_page(p: dict) -> str:
    banner = ('  <div class="draft">DRAFT: not published. This page is not linked from the site '
              'and is waiting for approval.</div>\n') if p["draft"] else ""
    body = f"""  <nav class="crumbs"><a href="../">World of Modern Warfare</a> · <a href="./">Devlog</a></nav>
{banner}  <article>
    <header>
      <div class="kicker">Devlog · {html.escape(nice_date(p["date"]))}</div>
      <h1>{inline(p["title"])}</h1>
    </header>
{p["body"]}
  </article>"""
    title = ("DRAFT: " if p["draft"] else "") + f'{p["title"]} · {SITE} devlog'
    return page(title, body, p["summary"], noindex=p["draft"])


def index_page(posts: list[dict]) -> str:
    shown = [p for p in posts if not p["draft"]]
    if shown:
        items = "\n".join(
            f"""    <a class="post" href="{p["slug"]}.html">
      <div class="when">{html.escape(nice_date(p["date"]))}</div>
      <div class="title">{inline(p["title"])}</div>
      <div class="summary">{inline(p["summary"])}</div>
    </a>""" for p in sorted(shown, key=lambda p: (p["date"], p["slug"]), reverse=True))
    else:
        items = '    <p class="empty">No posts yet. The first one is on its way.</p>'
    body = f"""  <nav class="crumbs"><a href="../">World of Modern Warfare</a></nav>
  <header class="top">
    <div class="kicker">WOMW · devlog</div>
    <h1>Devlog</h1>
    <p class="tag">How World of Modern Warfare is built: the systems, the decisions and what's next.</p>
  </header>
  <section class="posts">
{items}
  </section>"""
    return page(f"Devlog · {SITE}", body, "The World of Modern Warfare devlog.")


def main() -> int:
    check = "--check" in sys.argv
    posts = [read_post(os.path.join(DEVLOG, n)) for n in sorted(os.listdir(DEVLOG)) if n.endswith(".md")]
    pages = {f'{p["slug"]}.html': post_page(p) for p in posts}
    pages["index.html"] = index_page(posts)
    stale = []
    for name, text in pages.items():
        path = os.path.join(DEVLOG, name)
        try:
            old = open(path, encoding="utf-8").read()
        except OSError:
            old = None
        if old != text:
            stale.append(name)
            if not check:
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
    for name in stale:
        print(("out of date: " if check else "wrote ") + f"docs/devlog/{name}")
    drafts = [p["slug"] for p in posts if p["draft"]]
    if drafts:
        print("drafts (not in the index): " + ", ".join(drafts))
    return 1 if check and stale else 0


if __name__ == "__main__":
    sys.exit(main())
