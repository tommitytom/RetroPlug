#!/usr/bin/env python3
"""LSDj multi-version manual + changelog indexer.

Scans `../resources/manuals/` (outside the repo — see [resources/](../resources/)
sibling directory) for every `LSDj_*.pdf` plus `CHANGELOG.txt`, extracts the
text, chunks it, and writes a unified SQLite (FTS5 + sqlite-vec) index. Each
chunk is tagged with its source — a manual version like `9.2.6`, or the
changelog. Search picks the most-recent-not-newer-than-ROM manual when given
`--lsdj-version`.

Build the index:  `tools/lsdj-manual.py index`
Search:           `tools/lsdj-manual.py search [--lsdj-version VER] "query"`
List sources:     `tools/lsdj-manual.py versions`

The wrapper `tools/lsdj-search` activates the venv and forwards to `search`.
Run `tools/lsdj-manual-setup.sh` once to create `tools/.venv` and pip-install
deps (`pymupdf sqlite-vec numpy fastembed`).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sqlite3
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT      = Path(__file__).resolve().parents[1]
RESOURCES_DIR  = Path(os.environ.get("RETROPLUG_RESOURCES_DIR",
                                      REPO_ROOT.parent / "resources"))
MANUAL_DIR     = RESOURCES_DIR / "manuals"
MANUAL_MD      = MANUAL_DIR / "lsdj_manual.md"           # latest version only
MANUAL_IMG_DIR = MANUAL_DIR / "lsdj_manual_images"       # per-version subdirs
MANUAL_DB      = MANUAL_DIR / "lsdj_index.db"
EMBED_CACHE_DB = MANUAL_DIR / "lsdj_embed_cache.db"
CHANGELOG_TXT  = MANUAL_DIR / "CHANGELOG.txt"

EMBED_MODEL     = "BAAI/bge-small-en-v1.5"  # 384-dim, ONNX, ~80MB on first run
EMBED_DIM       = 384
CHUNK_TOKEN_CAP = 500
CHUNK_OVERLAP   = 50

# Suffix sentinel used in version_key. Sorts AFTER lowercase a-z so a bare
# `9.2.0` ranks higher than a lettered pre-release `9.2.0d`.
NO_SUFFIX_KEY = "~"


# ---------------------------------------------------------------------------
# Version parsing
# ---------------------------------------------------------------------------

# Matches `LSDj_<M>_<m>[_<p>][<letter>].pdf` and the lone-letter variants like
# `LSDj_3_9_h.pdf` (treated as M=3, m=9, p=0, suffix='h').
_FNAME_RX = re.compile(
    r"^LSDj_(\d+)_(\d+)(?:_(\d+))?([a-z])?(?:_[A-Za-z]+)?\.pdf$"
)

# Free-form "X.Y[.Z][letter]" — used for changelog headers and CLI version.
_FREE_RX = re.compile(
    r"^(\d+)\.(\d+)(?:\.(\d+))?([a-z])?$"
)


def parse_filename_version(name: str) -> tuple[int, int, int, str] | None:
    """Parse a manual filename. Returns (M, m, p, suffix) or None."""
    m = _FNAME_RX.match(name)
    if not m:
        return None
    return (int(m.group(1)), int(m.group(2)),
            int(m.group(3) or 0), m.group(4) or "")


def parse_free_version(s: str) -> tuple[int, int, int, str] | None:
    m = _FREE_RX.match(s.strip().lower())
    if not m:
        return None
    return (int(m.group(1)), int(m.group(2)),
            int(m.group(3) or 0), m.group(4) or "")


def version_key(parts: tuple[int, int, int, str]) -> str:
    """Two-digit zero-padded sortable string. LSDj will never go past 99.99."""
    M, m, p, suffix = parts
    s = suffix if suffix else NO_SUFFIX_KEY
    return f"{M:02d}.{m:02d}.{p:02d}.{s}"


def format_version(parts: tuple[int, int, int, str]) -> str:
    M, m, p, suffix = parts
    return f"{M}.{m}.{p}{suffix}"


# ---------------------------------------------------------------------------
# Source discovery
# ---------------------------------------------------------------------------

@dataclass
class ManualSource:
    path: Path
    version_str: str       # '9.2.6'
    version_key: str       # '09.02.06.~'


def discover_manuals(manual_dir: Path) -> list[ManualSource]:
    """Find every English LSDj_*.pdf. Defensive filter on _jp / _FR even
    though the downloader skips them too."""
    found: list[ManualSource] = []
    for p in sorted(manual_dir.glob("LSDj_*.pdf")):
        low = p.name.lower()
        if "_jp" in low or "_fr" in low or "manual_jp" in low:
            continue
        parts = parse_filename_version(p.name)
        if not parts:
            print(f"  [warn] cannot parse version from {p.name}; skipping",
                  file=sys.stderr)
            continue
        found.append(ManualSource(path=p,
                                  version_str=format_version(parts),
                                  version_key=version_key(parts)))
    found.sort(key=lambda s: s.version_key)
    return found


# ---------------------------------------------------------------------------
# PDF extraction & chunking (per-manual variant of the original code)
# ---------------------------------------------------------------------------

@dataclass
class Chunk:
    source_id: int
    page: int | None
    heading: str
    text: str
    image_refs: list[str] = field(default_factory=list)
    section_version: str | None = None


def looks_like_heading(line: str) -> bool:
    """LSDj manual headings: short, all-caps-or-titlecase, no trailing
    punctuation. Heuristic — good enough to seed chunk boundaries."""
    s = line.strip()
    if not s or len(s) > 80:
        return False
    if s.endswith((".", ",", ";", ":", "!", "?")):
        return False
    if re.match(r"^\d+(\.\d+)*\s+\S", s):
        return True
    letters = [c for c in s if c.isalpha()]
    if letters and sum(c.isupper() for c in letters) / len(letters) > 0.7:
        return True
    return False


def chunk_blocks(blocks: list[tuple[int, str]], source_id: int) -> list[Chunk]:
    """`blocks` is a list of (page, text). Slice into Chunks at heading
    boundaries, cap at CHUNK_TOKEN_CAP words with CHUNK_OVERLAP overlap."""
    chunks: list[Chunk] = []
    cur_heading = ""
    cur_words: list[tuple[int, str]] = []

    def flush(heading: str):
        if not cur_words:
            return
        i = 0
        while i < len(cur_words):
            window = cur_words[i: i + CHUNK_TOKEN_CAP]
            if not window:
                break
            text = " ".join(w for _, w in window).strip()
            page = window[0][0]
            chunks.append(Chunk(source_id=source_id, page=page,
                                heading=heading, text=text))
            if i + CHUNK_TOKEN_CAP >= len(cur_words):
                break
            i += CHUNK_TOKEN_CAP - CHUNK_OVERLAP

    for page, block in blocks:
        for raw in block.splitlines():
            line = raw.rstrip()
            if not line.strip():
                cur_words.append((page, ""))
                continue
            if looks_like_heading(line) and cur_words:
                flush(cur_heading)
                cur_heading = line.strip()
                cur_words = []
                continue
            for word in line.split():
                cur_words.append((page, word))

    flush(cur_heading)
    return chunks


def extract_pdf(pdf_path: Path, img_dir: Path
                ) -> tuple[list[tuple[int, str]], dict[int, list[str]]]:
    """Returns (per-page text blocks, page -> list of image paths relative
    to MANUAL_DIR)."""
    import pymupdf

    img_dir.mkdir(parents=True, exist_ok=True)
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
                if pix.n - pix.alpha >= 4:
                    pix = pymupdf.Pixmap(pymupdf.csRGB, pix)
                out_path = img_dir / f"p{page_no:03d}-{img_idx}.png"
                pix.save(str(out_path))
                imgs.append(out_path.relative_to(MANUAL_DIR).as_posix())
            except Exception as e:
                print(f"    [warn] {pdf_path.name} p{page_no} img{img_idx}: {e}",
                      file=sys.stderr)
        if imgs:
            page_images[page_no] = imgs

    return page_text, page_images


def write_latest_markdown(latest: ManualSource,
                          blocks: list[tuple[int, str]],
                          images: dict[int, list[str]]) -> None:
    """Regenerate lsdj_manual.md from the highest-version manual. The fallback
    Read+grep path documented in AGENTS.md only needs the canonical version."""
    lines = [
        f"# LSDj {latest.version_str} — extracted manual\n",
        "",
        f"Auto-generated by `tools/lsdj-manual.py index`. Source: "
        f"`{latest.path.relative_to(MANUAL_DIR.parent)}`.\n",
        "",
    ]
    for page, text in blocks:
        lines.append(f"## Page {page}\n")
        if page in images:
            for ref in images[page]:
                lines.append(f"![]({ref})")
            lines.append("")
        lines.append(text.rstrip())
        lines.append("")
    MANUAL_MD.write_text("\n".join(lines), encoding="utf-8")


# ---------------------------------------------------------------------------
# Changelog parsing
# ---------------------------------------------------------------------------

# Matches lines like `2025-06-05: v9.4.2` (verified format). Defensive
# fallback to bare `v9.4.2:` or `9.4.2:` if needed.
_CL_PRIMARY  = re.compile(
    r"^(?P<date>\d{4}-\d{2}-\d{2})\s*:\s*v(?P<v>\d+\.\d+(?:\.\d+)?[a-z]?)\s*$"
)
_CL_FALLBACK = re.compile(
    r"^(?P<v>\d+\.\d+(?:\.\d+)?[a-z]?)\s*:?\s*$"
)


def parse_changelog(path: Path) -> list[tuple[str, str]]:
    """Return list of (version_str, body). Empty list if no sections found."""
    try:
        raw = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        raw = path.read_text(encoding="latin-1")

    sections: list[tuple[str, list[str]]] = []
    current_ver: str | None = None
    current_body: list[str] = []

    def match_header(line: str) -> str | None:
        m = _CL_PRIMARY.match(line)
        if m:
            return m.group("v")
        m = _CL_FALLBACK.match(line)
        if m:
            return m.group("v")
        return None

    for line in raw.splitlines():
        v = match_header(line)
        if v is not None:
            if current_ver is not None:
                sections.append((current_ver, current_body))
            current_ver = v
            current_body = []
        else:
            if current_ver is not None:
                current_body.append(line)
    if current_ver is not None:
        sections.append((current_ver, current_body))

    return [(v, "\n".join(body).strip()) for v, body in sections
            if "\n".join(body).strip()]


def changelog_chunks(path: Path, source_id: int) -> list[Chunk]:
    sections = parse_changelog(path)
    if len(sections) < 3:
        # Defensive fallback: index the whole file as one chunk.
        print("    [warn] changelog parsing yielded few sections; "
              "indexing whole file as one chunk", file=sys.stderr)
        text = path.read_text(encoding="utf-8", errors="replace")
        return [Chunk(source_id=source_id, page=None, heading="CHANGELOG",
                      text=text, section_version=None)]

    chunks: list[Chunk] = []
    for version_str, body in sections:
        words = body.split()
        if not words:
            continue
        if len(words) <= CHUNK_TOKEN_CAP:
            chunks.append(Chunk(source_id=source_id, page=None,
                                heading=f"v{version_str}", text=body,
                                section_version=version_str))
            continue
        # Window long sections (rare for a changelog, but defensive).
        i = 0
        while i < len(words):
            piece = " ".join(words[i: i + CHUNK_TOKEN_CAP])
            chunks.append(Chunk(source_id=source_id, page=None,
                                heading=f"v{version_str}", text=piece,
                                section_version=version_str))
            if i + CHUNK_TOKEN_CAP >= len(words):
                break
            i += CHUNK_TOKEN_CAP - CHUNK_OVERLAP
    return chunks


# ---------------------------------------------------------------------------
# Embedding cache
# ---------------------------------------------------------------------------

def text_hash(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def open_embed_cache() -> sqlite3.Connection:
    conn = sqlite3.connect(str(EMBED_CACHE_DB))
    conn.execute(
        "CREATE TABLE IF NOT EXISTS embeddings("
        "hash TEXT PRIMARY KEY, vec BLOB NOT NULL)"
    )
    return conn


def embed_with_cache(texts: list[str], use_cache: bool
                     ) -> list[tuple[float, ...]]:
    """Embed `texts` via fastembed, caching by sha256(text). Returns a list
    of float tuples in the same order as `texts`."""
    n = len(texts)
    out: list[tuple[float, ...] | None] = [None] * n
    hashes = [text_hash(t) for t in texts]

    cache = open_embed_cache() if use_cache else None
    if cache is not None:
        cur = cache.cursor()
        for i, h in enumerate(hashes):
            row = cur.execute("SELECT vec FROM embeddings WHERE hash = ?",
                              (h,)).fetchone()
            if row is not None:
                out[i] = struct.unpack(f"{EMBED_DIM}f", row[0])

    missing_idx = [i for i, v in enumerate(out) if v is None]
    if missing_idx:
        from fastembed import TextEmbedding
        print(f"    embedding {len(missing_idx)}/{n} chunks "
              f"({n - len(missing_idx)} cache hits)",
              file=sys.stderr)
        model = TextEmbedding(model_name=EMBED_MODEL)
        vectors = list(model.embed([texts[i] for i in missing_idx]))
        for idx, vec in zip(missing_idx, vectors):
            tup = tuple(float(x) for x in vec)
            out[idx] = tup
            if cache is not None:
                blob = struct.pack(f"{EMBED_DIM}f", *tup)
                cache.execute(
                    "INSERT OR REPLACE INTO embeddings(hash, vec) VALUES (?, ?)",
                    (hashes[idx], blob),
                )
        if cache is not None:
            cache.commit()
    else:
        print(f"    embedding cache hit for all {n} chunks", file=sys.stderr)

    if cache is not None:
        cache.close()

    assert all(v is not None for v in out)
    return [v for v in out if v is not None]  # type: ignore[return-value]


# ---------------------------------------------------------------------------
# Index pipeline
# ---------------------------------------------------------------------------

def open_db(path: Path) -> sqlite3.Connection:
    import sqlite_vec

    conn = sqlite3.connect(str(path))
    conn.enable_load_extension(True)
    sqlite_vec.load(conn)
    conn.enable_load_extension(False)
    return conn


def build_db(sources: list[dict], chunks: list[Chunk],
             embeddings: list[tuple[float, ...]]) -> None:
    if MANUAL_DB.exists():
        MANUAL_DB.unlink()
    conn = open_db(MANUAL_DB)
    cur = conn.cursor()
    cur.executescript(f"""
        CREATE TABLE sources(
            id          INTEGER PRIMARY KEY,
            kind        TEXT    NOT NULL,
            version     TEXT,
            version_key TEXT    NOT NULL,
            path        TEXT    NOT NULL
        );
        CREATE TABLE chunks(
            id              INTEGER PRIMARY KEY,
            source_id       INTEGER NOT NULL REFERENCES sources(id),
            page            INTEGER,
            heading         TEXT,
            text            TEXT NOT NULL,
            image_refs      TEXT,
            section_version TEXT
        );
        CREATE VIRTUAL TABLE chunks_fts USING fts5(
            heading, text, section_version,
            content='chunks', content_rowid='id',
            tokenize='porter unicode61'
        );
        CREATE VIRTUAL TABLE chunks_vec USING vec0(
            embedding float[{EMBED_DIM}]
        );
    """)

    for s in sources:
        cur.execute(
            "INSERT INTO sources(id, kind, version, version_key, path) "
            "VALUES (?, ?, ?, ?, ?)",
            (s["id"], s["kind"], s["version"], s["version_key"], s["path"]),
        )

    for idx, (chunk, vec) in enumerate(zip(chunks, embeddings), start=1):
        cur.execute(
            "INSERT INTO chunks(id, source_id, page, heading, text, "
            "image_refs, section_version) VALUES (?, ?, ?, ?, ?, ?, ?)",
            (idx, chunk.source_id, chunk.page, chunk.heading, chunk.text,
             "\n".join(chunk.image_refs), chunk.section_version),
        )
        cur.execute(
            "INSERT INTO chunks_fts(rowid, heading, text, section_version) "
            "VALUES (?, ?, ?, ?)",
            (idx, chunk.heading, chunk.text, chunk.section_version),
        )
        blob = struct.pack(f"{EMBED_DIM}f", *vec)
        cur.execute(
            "INSERT INTO chunks_vec(rowid, embedding) VALUES (?, ?)",
            (idx, blob),
        )

    conn.commit()
    conn.close()


def cmd_index(args: argparse.Namespace) -> int:
    if not MANUAL_DIR.exists():
        print(f"error: manuals dir not found at {MANUAL_DIR}", file=sys.stderr)
        return 1

    manuals = discover_manuals(MANUAL_DIR)
    if args.only_version:
        manuals = [m for m in manuals if m.version_str == args.only_version]
        if not manuals:
            print(f"error: no manual matching {args.only_version}",
                  file=sys.stderr)
            return 1
    if not manuals:
        print(f"error: no LSDj_*.pdf in {MANUAL_DIR}", file=sys.stderr)
        return 1

    print(f"==> Found {len(manuals)} manual(s):", file=sys.stderr)
    for m in manuals:
        print(f"    LSDj {m.version_str}  ({m.path.name})", file=sys.stderr)

    # Sweep stale flat-layout PNGs left over from the previous single-PDF
    # incarnation. Per-version images now live under lsdj_manual_images/<ver>/.
    if MANUAL_IMG_DIR.exists():
        for p in MANUAL_IMG_DIR.iterdir():
            if p.is_file() and p.suffix.lower() == ".png":
                p.unlink()

    have_changelog = CHANGELOG_TXT.exists()
    print(f"==> Changelog: {'yes' if have_changelog else 'no'} "
          f"({CHANGELOG_TXT.name})", file=sys.stderr)

    # Build sources + chunks across everything.
    sources: list[dict] = []
    all_chunks: list[Chunk] = []
    next_source_id = 1

    latest_manual: ManualSource | None = manuals[-1] if manuals else None
    latest_blocks: list[tuple[int, str]] | None = None
    latest_images: dict[int, list[str]] | None = None

    for m in manuals:
        print(f"==> Extracting {m.path.name} (LSDj {m.version_str}) ...",
              file=sys.stderr)
        img_dir = MANUAL_IMG_DIR / m.version_str
        blocks, images = extract_pdf(m.path, img_dir)
        print(f"    {len(blocks)} pages, "
              f"{sum(len(v) for v in images.values())} images",
              file=sys.stderr)

        if m is latest_manual:
            latest_blocks, latest_images = blocks, images

        src_id = next_source_id
        next_source_id += 1
        sources.append({
            "id": src_id, "kind": "manual",
            "version": m.version_str, "version_key": m.version_key,
            "path": str(m.path.relative_to(RESOURCES_DIR)),
        })

        chunks = chunk_blocks(blocks, src_id)
        pages_in_chunk: dict[int, list[int]] = {}
        for i, c in enumerate(chunks):
            if c.page is not None:
                pages_in_chunk.setdefault(c.page, []).append(i)
        for page, refs in images.items():
            for i in pages_in_chunk.get(page, []):
                chunks[i].image_refs.extend(refs)
        all_chunks.extend(chunks)
        print(f"    {len(chunks)} chunks", file=sys.stderr)

    if have_changelog:
        print(f"==> Parsing {CHANGELOG_TXT.name} ...", file=sys.stderr)
        src_id = next_source_id
        next_source_id += 1
        # Changelog version_key sentinel: sorts ABOVE all manuals so an
        # ORDER BY version_key DESC LIMIT 1 of kind='manual' still gives
        # the right answer without filtering.
        sources.append({
            "id": src_id, "kind": "changelog", "version": None,
            "version_key": "ZZ.99.99.~",
            "path": str(CHANGELOG_TXT.relative_to(RESOURCES_DIR)),
        })
        ck = changelog_chunks(CHANGELOG_TXT, src_id)
        print(f"    {len(ck)} changelog chunks", file=sys.stderr)
        all_chunks.extend(ck)

    print(f"==> Embedding {len(all_chunks)} chunks total "
          f"(first run downloads {EMBED_MODEL}, ~80MB) ...", file=sys.stderr)
    embeddings = embed_with_cache([c.text for c in all_chunks],
                                  use_cache=not args.no_cache)
    if embeddings and len(embeddings[0]) != EMBED_DIM:
        print(f"error: expected {EMBED_DIM}-dim, got {len(embeddings[0])}",
              file=sys.stderr)
        return 1

    print(f"==> Building SQLite index at "
          f"{MANUAL_DB.relative_to(REPO_ROOT.parent)} ...", file=sys.stderr)
    build_db(sources, all_chunks, embeddings)

    if latest_manual and latest_blocks is not None and latest_images is not None:
        print(f"==> Writing markdown for latest "
              f"(LSDj {latest_manual.version_str}) ...", file=sys.stderr)
        write_latest_markdown(latest_manual, latest_blocks, latest_images)

    print("==> Done.", file=sys.stderr)
    return 0


# ---------------------------------------------------------------------------
# Search
# ---------------------------------------------------------------------------

def resolve_allowed_sources(conn: sqlite3.Connection,
                             lsdj_version: str | None,
                             include_changelog: bool,
                             only_changelog: bool) -> set[int]:
    cur = conn.cursor()
    allowed: set[int] = set()

    if only_changelog:
        for (rid,) in cur.execute(
                "SELECT id FROM sources WHERE kind = 'changelog'"):
            allowed.add(int(rid))
        return allowed

    if lsdj_version is not None:
        parts = parse_free_version(lsdj_version)
        if parts is None:
            print(f"error: cannot parse --lsdj-version {lsdj_version!r}",
                  file=sys.stderr)
            sys.exit(2)
        key = version_key(parts)
        row = cur.execute(
            "SELECT id FROM sources WHERE kind = 'manual' "
            "AND version_key <= ? ORDER BY version_key DESC LIMIT 1",
            (key,),
        ).fetchone()
        if row is None:
            print(f"warning: no manual found at or below LSDj "
                  f"{lsdj_version}; falling back to earliest available",
                  file=sys.stderr)
            row = cur.execute(
                "SELECT id FROM sources WHERE kind = 'manual' "
                "ORDER BY version_key ASC LIMIT 1"
            ).fetchone()
        if row is not None:
            allowed.add(int(row[0]))
    else:
        for (rid,) in cur.execute(
                "SELECT id FROM sources WHERE kind = 'manual'"):
            allowed.add(int(rid))

    if include_changelog:
        for (rid,) in cur.execute(
                "SELECT id FROM sources WHERE kind = 'changelog'"):
            allowed.add(int(rid))

    return allowed


def fts_search(conn: sqlite3.Connection, query: str, limit: int,
               allowed: set[int]) -> list[tuple[int, float]]:
    cur = conn.cursor()
    tokens = [t for t in re.findall(r"\w+", query) if t]
    if not tokens or not allowed:
        return []
    match = " OR ".join(f'"{t}"' for t in tokens)
    placeholders = ",".join("?" * len(allowed))
    sql = (
        "SELECT chunks_fts.rowid, bm25(chunks_fts) AS score "
        "FROM chunks_fts JOIN chunks ON chunks.id = chunks_fts.rowid "
        f"WHERE chunks_fts MATCH ? AND chunks.source_id IN ({placeholders}) "
        "ORDER BY score LIMIT ?"
    )
    cur.execute(sql, (match, *sorted(allowed), limit))
    return [(int(rid), float(s)) for rid, s in cur.fetchall()]


def vec_search(conn: sqlite3.Connection, query: str, limit: int,
               allowed: set[int]) -> list[tuple[int, float]]:
    # sqlite-vec MATCH composes poorly with WHERE filters, so over-fetch and
    # post-filter in Python.
    if not allowed:
        return []
    from fastembed import TextEmbedding
    model = TextEmbedding(model_name=EMBED_MODEL)
    qvec = next(iter(model.query_embed([query])))
    blob = struct.pack(f"{EMBED_DIM}f", *qvec)
    cur = conn.cursor()
    cur.execute(
        "SELECT rowid, distance FROM chunks_vec "
        "WHERE embedding MATCH ? ORDER BY distance LIMIT ?",
        (blob, limit * 4),
    )
    raw = cur.fetchall()
    if not raw:
        return []
    ids = [int(r[0]) for r in raw]
    placeholders = ",".join("?" * len(ids))
    src_map = dict(cur.execute(
        f"SELECT id, source_id FROM chunks WHERE id IN ({placeholders})",
        ids,
    ).fetchall())
    out = [(rid, float(d)) for rid, d in raw
           if src_map.get(int(rid)) in allowed]
    return out[:limit]


def rrf_combine(fts: list[tuple[int, float]],
                vec: list[tuple[int, float]],
                k: int = 60) -> list[int]:
    scores: dict[int, float] = {}
    for rank, (rid, _) in enumerate(fts, start=1):
        scores[rid] = scores.get(rid, 0.0) + 1.0 / (k + rank)
    for rank, (rid, _) in enumerate(vec, start=1):
        scores[rid] = scores.get(rid, 0.0) + 1.0 / (k + rank)
    return [rid for rid, _ in sorted(scores.items(), key=lambda kv: -kv[1])]


def format_hit(conn: sqlite3.Connection, rid: int, show_images: bool) -> str:
    cur = conn.cursor()
    row = cur.execute(
        "SELECT c.page, c.heading, c.text, c.image_refs, c.section_version, "
        "s.kind, s.version "
        "FROM chunks c JOIN sources s ON s.id = c.source_id "
        "WHERE c.id = ?", (rid,)
    ).fetchone()
    if not row:
        return f"[{rid}] (missing)"
    page, heading, text, image_refs, sect_v, kind, source_v = row
    snippet = re.sub(r"\s+", " ", text).strip()
    if len(snippet) > 360:
        snippet = snippet[:360].rsplit(" ", 1)[0] + " …"

    if kind == "changelog":
        tag = f"CHANGELOG {sect_v}" if sect_v else "CHANGELOG"
    else:
        loc = f"page {page}" if page else ""
        tag = (f"LSDj {source_v} manual"
               + (f", {loc}" if loc else "")
               + (f"  {heading}" if heading else ""))

    out = [f"--- {tag}", snippet]
    if show_images and image_refs:
        out.append("images: " + ", ".join(image_refs.split("\n")))
    return "\n".join(out)


def cmd_search(args: argparse.Namespace) -> int:
    if not MANUAL_DB.exists():
        print("error: index not built. Run `tools/lsdj-manual.py index` first.",
              file=sys.stderr)
        return 1
    conn = open_db(MANUAL_DB)
    allowed = resolve_allowed_sources(
        conn,
        lsdj_version=args.lsdj_version,
        include_changelog=not args.no_changelog,
        only_changelog=args.only_changelog,
    )

    if args.mode == "fts":
        ranked = [rid for rid, _ in fts_search(conn, args.query, args.limit, allowed)]
    elif args.mode == "vec":
        ranked = [rid for rid, _ in vec_search(conn, args.query, args.limit, allowed)]
    else:
        fts = fts_search(conn, args.query, args.limit * 2, allowed)
        vec = vec_search(conn, args.query, args.limit * 2, allowed)
        ranked = rrf_combine(fts, vec)[: args.limit]

    if not ranked:
        print("(no hits)", file=sys.stderr)
        return 0

    for rid in ranked:
        print(format_hit(conn, rid, args.show_images))
        print()
    return 0


# ---------------------------------------------------------------------------
# Versions subcommand
# ---------------------------------------------------------------------------

def cmd_versions(args: argparse.Namespace) -> int:
    if not MANUAL_DB.exists():
        print("error: index not built. Run `tools/lsdj-manual.py index` first.",
              file=sys.stderr)
        return 1
    conn = open_db(MANUAL_DB)
    cur = conn.cursor()
    rows = cur.execute(
        "SELECT s.kind, s.version, s.path, COUNT(c.id) "
        "FROM sources s LEFT JOIN chunks c ON c.source_id = s.id "
        "GROUP BY s.id ORDER BY s.version_key"
    ).fetchall()
    if not rows:
        print("(no sources indexed)", file=sys.stderr)
        return 0
    width = max(len(r[1] or "") for r in rows)
    for kind, ver, path, n_chunks in rows:
        label = ver if ver else "-"
        print(f"  {kind:9s}  {label:<{max(width, 8)}}  "
              f"chunks={n_chunks:<5d}  {path}")
    return 0


# ---------------------------------------------------------------------------
# Entrypoint
# ---------------------------------------------------------------------------

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    i = sub.add_parser("index", help="extract PDFs + changelog, build SQLite index")
    i.add_argument("--no-cache", action="store_true",
                   help="ignore the embedding cache (force re-embed everything)")
    i.add_argument("--only-version", type=str, default=None,
                   help="only re-index this manual version (debugging)")

    s = sub.add_parser("search", help="query the index")
    s.add_argument("--mode", choices=("hybrid", "fts", "vec"), default="hybrid")
    s.add_argument("--limit", type=int, default=5)
    s.add_argument("--show-images", action="store_true")
    s.add_argument("--lsdj-version", type=str, default=None,
                   help="constrain manual hits to the most-recent manual "
                        "whose version is <= this (e.g. 9.4.2)")
    s.add_argument("--no-changelog", action="store_true")
    s.add_argument("--only-changelog", action="store_true")
    s.add_argument("query", nargs="+")

    sub.add_parser("versions", help="list indexed sources")

    args = p.parse_args()
    if args.cmd == "index":
        return cmd_index(args)
    if args.cmd == "search":
        args.query = " ".join(args.query)
        return cmd_search(args)
    if args.cmd == "versions":
        return cmd_versions(args)
    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
