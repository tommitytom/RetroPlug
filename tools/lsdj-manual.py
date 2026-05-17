#!/usr/bin/env python3
"""LSDj manual indexer + searcher.

One-shot extraction of `../resources/manuals/LSDj_9_2_6.pdf` (outside the
repo — see [resources/](../resources/) sibling directory) into:

  - `../resources/manuals/lsdj_manual.md`       structured markdown w/ image refs
  - `../resources/manuals/lsdj_manual_images/`  extracted page images (PNG)
  - `../resources/manuals/lsdj_index.db`        SQLite (FTS5 + sqlite-vec)

Query the index with the `search` subcommand. Hybrid by default (reciprocal-
rank fusion of FTS5 BM25 and sqlite-vec cosine).

  tools/lsdj-manual.py index
  tools/lsdj-manual.py search "midi sync mode"
  tools/lsdj-manual.py search --mode vec "how do two units stay in time"
  tools/lsdj-manual.py search --show-images "PROJECT screen"

This script must run under the repo's tools/.venv (created by
`tools/lsdj-manual-setup.sh`, which also pip-installs the deps). Invoking
through the `tools/lsdj-search` shell wrapper takes care of that.
"""

from __future__ import annotations

import argparse
import os
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT       = Path(__file__).resolve().parents[1]
# Manuals live outside the repo (sibling `resources/` dir). Override via the
# `RETROPLUG_RESOURCES_DIR` env var if your layout differs.
RESOURCES_DIR   = Path(os.environ.get("RETROPLUG_RESOURCES_DIR",
                                       REPO_ROOT.parent / "resources"))
MANUAL_PDF      = RESOURCES_DIR / "manuals" / "LSDj_9_2_6.pdf"
MANUAL_DIR      = RESOURCES_DIR / "manuals"
MANUAL_MD       = MANUAL_DIR / "lsdj_manual.md"
MANUAL_IMG_DIR  = MANUAL_DIR / "lsdj_manual_images"
MANUAL_DB       = MANUAL_DIR / "lsdj_index.db"

EMBED_MODEL     = "BAAI/bge-small-en-v1.5"  # 384-dim, ONNX, ~80MB
EMBED_DIM       = 384
CHUNK_TOKEN_CAP = 500  # rough; words ~= tokens for this corpus
CHUNK_OVERLAP   = 50


# ---------------------------------------------------------------------------
# Index pipeline
# ---------------------------------------------------------------------------

@dataclass
class Chunk:
    page: int
    heading: str
    text: str
    image_refs: list[str]


def looks_like_heading(line: str) -> bool:
    """LSDj manual headings are short, all-caps-or-titlecase, no trailing
    period, and don't end with punctuation that suggests a sentence. This is
    a heuristic — good enough to seed chunk boundaries; FTS still searches
    the full text inside each chunk."""
    s = line.strip()
    if not s or len(s) > 80:
        return False
    if s.endswith((".", ",", ";", ":", "!", "?")):
        return False
    # Common section markers in the LSDj manual.
    if re.match(r"^\d+(\.\d+)*\s+\S", s):
        return True
    # Mostly-uppercase short lines (e.g. "MIDI SYNC", "PROJECT SCREEN").
    letters = [c for c in s if c.isalpha()]
    if letters and sum(c.isupper() for c in letters) / len(letters) > 0.7:
        return True
    return False


def chunk_text(blocks: list[tuple[int, str]]) -> list[Chunk]:
    """`blocks` is a list of (page, text) tuples in document order. We slice
    into Chunks at heading boundaries, capped at CHUNK_TOKEN_CAP words with
    CHUNK_OVERLAP-word overlap when a section is longer."""
    chunks: list[Chunk] = []
    cur_heading = ""
    cur_page = 1
    cur_words: list[tuple[int, str]] = []  # (page, word)

    def flush(heading: str):
        if not cur_words:
            return
        # Cap into windows of CHUNK_TOKEN_CAP words with overlap.
        i = 0
        while i < len(cur_words):
            window = cur_words[i : i + CHUNK_TOKEN_CAP]
            if not window:
                break
            text = " ".join(w for _, w in window).strip()
            page = window[0][0]
            chunks.append(Chunk(page=page, heading=heading, text=text, image_refs=[]))
            if i + CHUNK_TOKEN_CAP >= len(cur_words):
                break
            i += CHUNK_TOKEN_CAP - CHUNK_OVERLAP

    for page, block in blocks:
        for raw_line in block.splitlines():
            line = raw_line.rstrip()
            if not line.strip():
                # Treat blank lines as soft breaks; let chunk cap handle it.
                cur_words.append((page, ""))
                continue
            if looks_like_heading(line) and cur_words:
                flush(cur_heading)
                cur_heading = line.strip()
                cur_words = []
                cur_page = page
                continue
            for word in line.split():
                cur_words.append((page, word))

    flush(cur_heading)
    return chunks


def extract_pdf(pdf_path: Path) -> tuple[list[tuple[int, str]], dict[int, list[str]]]:
    """Returns (per-page text blocks, page -> list of image file paths)."""
    import pymupdf

    MANUAL_IMG_DIR.mkdir(parents=True, exist_ok=True)

    doc = pymupdf.open(pdf_path)
    page_text: list[tuple[int, str]] = []
    page_images: dict[int, list[str]] = {}

    for page_no, page in enumerate(doc, start=1):
        page_text.append((page_no, page.get_text("text")))

        imgs: list[str] = []
        for img_idx, img_info in enumerate(page.get_images(full=True), start=1):
            xref = img_info[0]
            try:
                pix = pymupdf.Pixmap(doc, xref)
                # Convert CMYK / alpha to RGB.
                if pix.n - pix.alpha >= 4:
                    pix = pymupdf.Pixmap(pymupdf.csRGB, pix)
                out_path = MANUAL_IMG_DIR / f"p{page_no:03d}-{img_idx}.png"
                pix.save(str(out_path))
                imgs.append(out_path.relative_to(MANUAL_DIR).as_posix())
            except Exception as e:
                print(f"  [warn] page {page_no} image {img_idx}: {e}", file=sys.stderr)
        if imgs:
            page_images[page_no] = imgs

    return page_text, page_images


def write_markdown(blocks: list[tuple[int, str]], images: dict[int, list[str]]) -> None:
    lines: list[str] = ["# LSDj 9.2.6 — extracted manual\n",
                        "",
                        "Auto-generated by `tools/lsdj-manual.py index`. Source: "
                        "`../resources/manuals/LSDj_9_2_6.pdf`.\n",
                        ""]
    for page, text in blocks:
        lines.append(f"## Page {page}\n")
        if page in images:
            for ref in images[page]:
                lines.append(f"![]({ref})")
            lines.append("")
        lines.append(text.rstrip())
        lines.append("")
    MANUAL_MD.write_text("\n".join(lines), encoding="utf-8")


def open_db(path: Path) -> sqlite3.Connection:
    import sqlite_vec

    conn = sqlite3.connect(str(path))
    conn.enable_load_extension(True)
    sqlite_vec.load(conn)
    conn.enable_load_extension(False)
    return conn


def build_db(chunks: list[Chunk], embeddings) -> None:
    import struct

    if MANUAL_DB.exists():
        MANUAL_DB.unlink()
    conn = open_db(MANUAL_DB)
    cur = conn.cursor()
    cur.executescript(f"""
        CREATE TABLE chunks(
            id          INTEGER PRIMARY KEY,
            page        INTEGER NOT NULL,
            heading     TEXT,
            text        TEXT NOT NULL,
            image_refs  TEXT
        );
        CREATE VIRTUAL TABLE chunks_fts USING fts5(
            heading, text,
            content='chunks', content_rowid='id',
            tokenize='porter unicode61'
        );
        CREATE VIRTUAL TABLE chunks_vec USING vec0(
            embedding float[{EMBED_DIM}]
        );
    """)

    for idx, (chunk, vec) in enumerate(zip(chunks, embeddings), start=1):
        cur.execute(
            "INSERT INTO chunks(id, page, heading, text, image_refs) VALUES (?, ?, ?, ?, ?)",
            (idx, chunk.page, chunk.heading, chunk.text, "\n".join(chunk.image_refs)),
        )
        cur.execute(
            "INSERT INTO chunks_fts(rowid, heading, text) VALUES (?, ?, ?)",
            (idx, chunk.heading, chunk.text),
        )
        blob = struct.pack(f"{EMBED_DIM}f", *vec)
        cur.execute(
            "INSERT INTO chunks_vec(rowid, embedding) VALUES (?, ?)",
            (idx, blob),
        )

    conn.commit()
    conn.close()


def cmd_index(args: argparse.Namespace) -> int:
    if not MANUAL_PDF.exists():
        print(f"error: manual not found at {MANUAL_PDF}", file=sys.stderr)
        return 1

    print(f"==> Extracting {MANUAL_PDF.name} ...", file=sys.stderr)
    blocks, images = extract_pdf(MANUAL_PDF)
    print(f"    {len(blocks)} pages, "
          f"{sum(len(v) for v in images.values())} images extracted to "
          f"{MANUAL_IMG_DIR.relative_to(REPO_ROOT)}/", file=sys.stderr)

    print("==> Writing structured markdown ...", file=sys.stderr)
    write_markdown(blocks, images)
    print(f"    {MANUAL_MD.relative_to(REPO_ROOT)}", file=sys.stderr)

    print("==> Chunking ...", file=sys.stderr)
    chunks = chunk_text(blocks)
    # Attach the page's image refs to every chunk that originated from that page.
    pages_in_chunk: dict[int, list[int]] = {}
    for i, c in enumerate(chunks):
        pages_in_chunk.setdefault(c.page, []).append(i)
    for page, refs in images.items():
        for i in pages_in_chunk.get(page, []):
            chunks[i].image_refs.extend(refs)
    print(f"    {len(chunks)} chunks", file=sys.stderr)

    print(f"==> Embedding with {EMBED_MODEL} (first run downloads ~80MB) ...", file=sys.stderr)
    from fastembed import TextEmbedding
    model = TextEmbedding(model_name=EMBED_MODEL)
    embeddings = list(model.embed([c.text for c in chunks]))
    if embeddings and len(embeddings[0]) != EMBED_DIM:
        print(f"error: expected {EMBED_DIM}-dim embeddings, got {len(embeddings[0])}",
              file=sys.stderr)
        return 1

    print(f"==> Building SQLite index at {MANUAL_DB.relative_to(REPO_ROOT)} ...",
          file=sys.stderr)
    build_db(chunks, embeddings)

    print("==> Done.", file=sys.stderr)
    return 0


# ---------------------------------------------------------------------------
# Search
# ---------------------------------------------------------------------------

def fts_search(conn: sqlite3.Connection, query: str, limit: int) -> list[tuple[int, float]]:
    cur = conn.cursor()
    # MATCH wants a literal FTS5 query; escape user input by wrapping each
    # token as a phrase so quotes / operators don't blow up.
    tokens = [t for t in re.findall(r"\w+", query) if t]
    if not tokens:
        return []
    match = " OR ".join(f'"{t}"' for t in tokens)
    cur.execute(
        "SELECT rowid, bm25(chunks_fts) AS score "
        "FROM chunks_fts WHERE chunks_fts MATCH ? ORDER BY score LIMIT ?",
        (match, limit),
    )
    return [(int(rid), float(s)) for rid, s in cur.fetchall()]


def vec_search(conn: sqlite3.Connection, query: str, limit: int) -> list[tuple[int, float]]:
    import struct
    from fastembed import TextEmbedding

    model = TextEmbedding(model_name=EMBED_MODEL)
    qvec = next(iter(model.query_embed([query])))
    blob = struct.pack(f"{EMBED_DIM}f", *qvec)
    cur = conn.cursor()
    cur.execute(
        "SELECT rowid, distance FROM chunks_vec "
        "WHERE embedding MATCH ? ORDER BY distance LIMIT ?",
        (blob, limit),
    )
    return [(int(rid), float(d)) for rid, d in cur.fetchall()]


def rrf_combine(fts: list[tuple[int, float]],
                vec: list[tuple[int, float]],
                k: int = 60) -> list[int]:
    """Reciprocal-rank fusion of two ranked lists. Returns deduped ids in
    fused order (best first). k is the standard RRF damping constant."""
    scores: dict[int, float] = {}
    for rank, (rid, _) in enumerate(fts, start=1):
        scores[rid] = scores.get(rid, 0.0) + 1.0 / (k + rank)
    for rank, (rid, _) in enumerate(vec, start=1):
        scores[rid] = scores.get(rid, 0.0) + 1.0 / (k + rank)
    return [rid for rid, _ in sorted(scores.items(), key=lambda kv: -kv[1])]


def format_hit(conn: sqlite3.Connection, rid: int, show_images: bool) -> str:
    cur = conn.cursor()
    cur.execute("SELECT page, heading, text, image_refs FROM chunks WHERE id = ?", (rid,))
    row = cur.fetchone()
    if not row:
        return f"[{rid}] (missing)"
    page, heading, text, image_refs = row
    snippet = re.sub(r"\s+", " ", text).strip()
    if len(snippet) > 360:
        snippet = snippet[:360].rsplit(" ", 1)[0] + " …"
    out = [f"--- page {page}  {heading or '(no heading)'}",
           snippet]
    if show_images and image_refs:
        out.append("images: " + ", ".join(image_refs.split("\n")))
    return "\n".join(out)


def cmd_search(args: argparse.Namespace) -> int:
    if not MANUAL_DB.exists():
        print(f"error: index not built yet. Run `tools/lsdj-manual.py index` first.",
              file=sys.stderr)
        return 1
    conn = open_db(MANUAL_DB)
    if args.mode == "fts":
        ranked = [rid for rid, _ in fts_search(conn, args.query, args.limit)]
    elif args.mode == "vec":
        ranked = [rid for rid, _ in vec_search(conn, args.query, args.limit)]
    else:  # hybrid
        fts = fts_search(conn, args.query, args.limit * 2)
        vec = vec_search(conn, args.query, args.limit * 2)
        ranked = rrf_combine(fts, vec)[: args.limit]

    if not ranked:
        print("(no hits)", file=sys.stderr)
        return 0

    for rid in ranked:
        print(format_hit(conn, rid, args.show_images))
        print()
    return 0


# ---------------------------------------------------------------------------
# Entrypoint
# ---------------------------------------------------------------------------

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("index", help="extract PDF, build markdown + SQLite index")

    s = sub.add_parser("search", help="query the index")
    s.add_argument("--mode", choices=("hybrid", "fts", "vec"), default="hybrid")
    s.add_argument("--limit", type=int, default=5)
    s.add_argument("--show-images", action="store_true",
                   help="include image paths in output for hits whose page had figures")
    s.add_argument("query", nargs="+", help="query string (joined with spaces)")

    args = p.parse_args()
    if args.cmd == "search":
        args.query = " ".join(args.query)
        return cmd_search(args)
    if args.cmd == "index":
        return cmd_index(args)
    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
