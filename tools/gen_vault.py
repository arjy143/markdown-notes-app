#!/usr/bin/env python3
"""Generate a synthetic Obsidian-style vault for benchmarks.

Example:
    python3 tools/gen_vault.py --out /tmp/vault5k --notes 5000 --attachments 1000

Notes have frontmatter, headings, paragraphs, wikilinks (some unresolved),
Markdown links, tags (some nested), embeds, task lists, code blocks and math.
Output is deterministic for a given --seed.
"""
import argparse
import os
import random
import struct
import zlib

WORDS = (
    "alpha beta gamma delta epsilon zeta theta lambda sigma omega river mountain forest ocean desert valley "
    "planet orbit signal vector matrix tensor kernel thread process memory cache index query search parse "
    "render layout widget editor note vault link tag heading block embed table list task code math graph "
    "coffee garden window market history science music language travel recipe project meeting idea draft "
    "review design system network server client storage backup archive journal summary research report"
).split()
FOLDERS = ["Projects", "Areas", "Resources", "Archive", "Journal", "Inbox", "Projects/Alpha", "Projects/Beta",
           "Resources/Books", "Resources/Papers", "Areas/Health", "Areas/Finance", "Journal/2023", "Journal/2024"]


def tiny_png(seed):
    # A valid 2x2 RGB PNG with a seed-dependent colour.
    w = h = 2
    r, g, b = (seed * 37) % 256, (seed * 91) % 256, (seed * 53) % 256
    raw = b"".join(b"\x00" + bytes([r, g, b]) * w for _ in range(h))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def sentence(rng, n=None):
    n = n or rng.randint(6, 18)
    s = " ".join(rng.choice(WORDS) for _ in range(n))
    return s[0].upper() + s[1:] + "."


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--notes", type=int, default=5000)
    ap.add_argument("--attachments", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--big-note-lines", type=int, default=5000, help="lines in 'Big note.md' (editor benchmark)")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    os.makedirs(args.out, exist_ok=True)

    names = []
    for i in range(args.notes):
        folder = rng.choice(FOLDERS + [""] * 4)
        title = " ".join(rng.choice(WORDS) for _ in range(rng.randint(1, 4))).title() + f" {i}"
        names.append((folder, title))
    attach = []
    for i in range(args.attachments):
        folder = rng.choice(["attachments", "attachments/img", "Resources/assets"])
        attach.append((folder, f"image {i}.png"))

    for folder, name in attach:
        d = os.path.join(args.out, folder)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, name), "wb") as f:
            f.write(tiny_png(zlib.crc32(name.encode()) & 0xFFFF))

    tags = [rng.choice(WORDS) for _ in range(60)] + [f"{rng.choice(WORDS)}/{rng.choice(WORDS)}" for _ in range(40)]
    for i, (folder, title) in enumerate(names):
        lines = ["---", f"tags: [{', '.join(rng.sample(tags, rng.randint(0, 3)))}]"]
        if rng.random() < 0.2:
            lines.append(f"aliases: [{rng.choice(WORDS).title()} {i}]")
        lines += ["---", f"# {title}", ""]
        for _ in range(rng.randint(2, 6)):
            lines.append(f"## {sentence(rng, 3)[:-1]}")
            for _ in range(rng.randint(1, 4)):
                parts = [sentence(rng)]
                r = rng.random()
                if r < 0.6:
                    tf, tt = rng.choice(names)
                    parts.append(f"See [[{tt}]].")
                if r < 0.15:
                    parts.append(f"Also [[Missing {rng.choice(WORDS)} {rng.randint(0, 999)}]].")
                if rng.random() < 0.3:
                    parts.append("#" + rng.choice(tags))
                if rng.random() < 0.1:
                    tf, tt = rng.choice(names)
                    parts.append(f"[md link]({tt.replace(' ', '%20')}.md)")
                lines.append(" ".join(parts))
                lines.append("")
            if rng.random() < 0.15:
                lines += ["- [ ] " + sentence(rng, 4), "- [x] " + sentence(rng, 4), ""]
            if rng.random() < 0.1:
                lines += ["```python", "def f(x):", "    return x * 2  # [[not a link]]", "```", ""]
            if rng.random() < 0.08:
                lines += ["$$", "\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}", "$$", ""]
            if rng.random() < 0.1 and attach:
                af, an = rng.choice(attach)
                lines += [f"![[{an}]]", ""]
        d = os.path.join(args.out, folder)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, title + ".md"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")

    # One big note for the editor keystroke benchmark.
    with open(os.path.join(args.out, "Big note.md"), "w", encoding="utf-8", newline="\n") as f:
        out = ["# Big note", ""]
        while len(out) < args.big_note_lines:
            out.append(f"## Section {len(out)}")
            for _ in range(8):
                out.append(sentence(rng) + f" [[{rng.choice(names)[1]}]] #" + rng.choice(tags) + " **bold** `code`")
            out.append("")
        f.write("\n".join(out[: args.big_note_lines]) + "\n")

    # A typical note for the reading-view benchmark.
    with open(os.path.join(args.out, "Typical note.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("---\ntags: [bench]\n---\n# Typical note\n\n" +
                "\n\n".join(sentence(rng, 30) + f" [[{rng.choice(names)[1]}]] #" + rng.choice(tags)
                            for _ in range(25)) +
                "\n\n> [!note] A callout\n> With text.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
                "Inline $x^2$ and $\\alpha + \\beta$.\n\n$$\n\\int_0^1 x\\,dx\n$$\n\n- [ ] task\n- [x] done\n")
    print(f"wrote {args.notes} notes and {args.attachments} attachments to {args.out}")


if __name__ == "__main__":
    main()
