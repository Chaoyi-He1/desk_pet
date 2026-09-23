#!/usr/bin/env python3
"""Read a ship's official lines from the bilibili Azur Lane wiki.

Used by tools/build_ships.py. As a script it prints what it finds:
  python3 tools/fetch_voice.py 贝尔法斯特 [--html saved.html]

Every voiced line becomes a row: title (the skin's voice table, '' for the default
skin), key (scene: login, touch, touch2, headtouch, main, home, ...), index, oath,
zh and jp text. The lines are copyrighted game content: keep them local.
"""
import argparse
import sys
import time
import urllib.parse
import urllib.request
from html.parser import HTMLParser

HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36",
    "Referer": "https://wiki.biligame.com/blhx/",
    "Accept-Language": "zh-CN,zh;q=0.9",
}
MIN_GAP = 0.6  # seconds between requests, to be polite to the wiki


class Node:
    __slots__ = ("tag", "attrs", "children", "parent")

    def __init__(self, tag, attrs, parent):
        self.tag, self.attrs, self.children, self.parent = tag, dict(attrs), [], parent

    def classes(self):
        return (self.attrs.get("class") or "").split()


VOID = {"br", "img", "hr", "meta", "link", "input", "source", "wbr", "area", "base", "col", "embed", "param", "track"}


class TreeBuilder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("root", [], None)
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur)
        self.cur.children.append(n)
        if tag not in VOID:
            self.cur = n

    def handle_endtag(self, tag):
        c = self.cur
        while c is not None and c.tag != tag:
            c = c.parent
        if c is not None and c.parent is not None:
            self.cur = c.parent

    def handle_data(self, data):
        self.cur.children.append(data)


def text(n):
    return n if isinstance(n, str) else "".join(text(c) for c in n.children)


def walk(n):
    if isinstance(n, str):
        return
    yield n
    for c in n.children:
        yield from walk(c)


def find(n, pred):
    return [x for x in walk(n) if pred(x)]


def clean(s):
    return " ".join(s.split())


def page_url(ship_cn):
    return "https://wiki.biligame.com/blhx/" + urllib.parse.quote(ship_cn)


def parse(html):
    tb = TreeBuilder()
    tb.feed(html)
    rows = []
    for table in find(tb.root, lambda x: "table-ShipWordsTable" in x.classes()):
        title = table.attrs.get("data-title", "").strip()
        for tr in find(table, lambda x: x.tag == "tr" and x.attrs.get("data-key")):
            key = tr.attrs["data-key"]
            for block in find(tr, lambda x: "ship_word_block" in x.classes()):
                zh = jp = ""
                oath = False
                for line in find(block, lambda x: "ship_word_line" in x.classes()):
                    lang = line.attrs.get("data-lang", "")
                    t = clean(text(line))
                    if find(line, lambda x: x.tag == "span" and "誓约" in (x.attrs.get("title") or "")):
                        oath = True
                    if lang == "zh":
                        zh = t
                    elif lang == "jp":
                        jp = t
                    elif not zh:
                        zh = t
                audio = [a.attrs["href"] for a in find(block, lambda x: x.tag == "a" and (x.attrs.get("href") or "").endswith(".mp3"))]
                if not audio or not (zh or jp):
                    continue
                rows.append({"title": title, "key": key, "index": block.attrs.get("data-key-i", "1"), "oath": int(oath),
                             "zh": zh or jp, "jp": jp})
    return rows


class Throttle:
    def __init__(self):
        self.last = 0.0

    def wait(self):
        dt = time.time() - self.last
        if dt < MIN_GAP:
            time.sleep(MIN_GAP - dt)
        self.last = time.time()


def request(url, method="GET", throttle=None):
    if throttle:
        throttle.wait()
    req = urllib.request.Request(url, headers=HEADERS, method=method)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.headers, (r.read() if method == "GET" else b"")
        except urllib.error.HTTPError as e:
            if e.code in (403, 429, 567) and attempt < 3:
                time.sleep(2 * (attempt + 1))
                continue
            raise


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ship", help="ship name as used by the wiki, e.g. 贝尔法斯特")
    ap.add_argument("--html", help="use a saved copy of the wiki page")
    a = ap.parse_args()
    html = open(a.html, encoding="utf-8").read() if a.html else request(page_url(a.ship), throttle=Throttle())[1].decode("utf-8")
    rows = parse(html)
    per = {}
    for r in rows:
        per[r["title"] or "(默认)"] = per.get(r["title"] or "(默认)", 0) + 1
    print(f"{len(rows)} lines", per)
    return 0


if __name__ == "__main__":
    sys.exit(main())
