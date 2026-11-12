#!/usr/bin/env python3
"""Intègre engine/web/* dans le binaire du moteur : génère src/http/web_assets.gen.cpp."""
import pathlib

root = pathlib.Path(__file__).resolve().parent.parent
web = root / "web"
types = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "application/javascript; charset=utf-8",
         ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png"}
out = ['// Généré par tools/embed_web.py depuis engine/web. Ne pas éditer.', '#include "http/web_assets.hpp"',
       "namespace pbc::web {", "namespace {"]
entries = []
for i, f in enumerate(sorted(p for p in web.rglob("*") if p.is_file())):
    data = f.read_bytes()
    out.append(f"const unsigned char a{i}[] = {{{','.join(str(b) for b in data)}}};")
    entries.append(f'{{"/{f.relative_to(web).as_posix()}", "{types.get(f.suffix, "application/octet-stream")}", a{i}, {len(data)}}}')
out.append("}  // namespace")
out.append(f"const Asset assets[] = {{{', '.join(entries)}}};")
out.append(f"const unsigned asset_count = {len(entries)};")
out.append("}  // namespace pbc::web")
target = root / "src/http/web_assets.gen.cpp"
text = "\n".join(out) + "\n"
if not target.exists() or target.read_text() != text:
    target.write_text(text)
