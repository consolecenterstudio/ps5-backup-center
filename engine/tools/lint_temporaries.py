#!/usr/bin/env python3
"""Refuse les boucles « for (… : f()….items()) » sur un Json rendu par valeur : la liste temporaire est détruite avant
d'être parcourue (lecture de mémoire libérée ; deux fois déjà : shares_out_load, links). Gardez-la dans une variable.
Les fonctions qui rendent une référence (const Json&) sont listées dans OK."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
OK = {"catalog"}  # const Json& catalog()
pat = re.compile(r"for \(.*: ([A-Za-z_>.:-]*?)([A-Za-z_]\w*)\([^()]*\)(\[[^]]*\])*\.items\(\)\)")
bad = []
for d in ("engine/src", "app/src"):
    for f in sorted((ROOT / d).rglob("*.cpp")):
        for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
            m = pat.search(line)
            if m and m.group(2) not in OK:
                bad.append(f"{f.relative_to(ROOT)}:{n}: {line.strip()}")
if bad:
    print("boucle sur une liste temporaire (gardez-la dans une variable) :\n  " + "\n  ".join(bad))
    sys.exit(1)
