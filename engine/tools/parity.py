#!/usr/bin/env python3
"""Parité télé ↔ web, par les mots : chaque texte que la télé affiche (tr(), trf(), N_() dans app/src) doit exister aussi
dans le web (T() ou data-i18n dans engine/web), puisque les mêmes fonctions emploient les mêmes mots.

Exceptions : les textes propres à la télé (aides de manette, touches, fenêtres qui n'existent qu'à la télé…), listés avec
leur raison dans build-system/parite-tele-seulement.txt (« texte<TAB>raison », une ligne par texte).

    python3 engine/tools/parity.py            liste les textes télé absents du web et non listés comme « télé seulement »
    python3 engine/tools/parity.py --all      liste aussi ceux de la liste (pour la revoir)
    python3 engine/tools/parity.py --nettoyer retire de la liste les lignes devenues inutiles

Code de sortie 1 s'il en manque : qa/publish_update.py refuse alors d'installer.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ALLOW = ROOT / "build-system/parite-tele-seulement.txt"
STR = r'"((?:[^"\\]|\\.)*)"'


def unescape(s):
    return bytes(s, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def norm(s):  # « %lld », « %zu »… = « %d » : la même phrase des deux côtés
    return re.sub(r"%(?:ll|l|z)?[duxi]", "%d", s)


def tv_keys():
    keys = {}
    for f in sorted((ROOT / "app/src").glob("*.cpp")):
        text = f.read_text()
        for m in re.finditer(r'\b(?:tr|trf|N_)\(\s*' + STR, text):
            k = unescape(m.group(1))
            if k.strip():
                keys.setdefault(k, f"{f.name}:{text.count(chr(10), 0, m.start()) + 1}")
    return keys


def web_keys():
    keys = set()
    for f in list((ROOT / "engine/web").glob("*.js")) + [ROOT / "engine/web/index.html"]:
        text = f.read_text()
        for m in re.finditer(r'\b(?:T|N_)\(\s*' + STR, text):
            keys.add(unescape(m.group(1)))
        for m in re.finditer(r"\bT\(\s*'((?:[^'\\]|\\.)*)'", text):
            keys.add(unescape(m.group(1)))
        for m in re.finditer(r'data-i18n[^>]*>([^<]+)<', text):
            keys.add(m.group(1).strip())
    return keys


# Seules raisons acceptées (audit du 18/11 : des lignes « à revoir » avaient laissé passer des manques) :
#   « télé seulement : <raison> »   aide de manette, fenêtre qui n'existe qu'à la télé…
#   « même sens : web « … » »        le web dit la même chose avec d'autres mots (le texte web est cité)
# Toute autre raison (« à revoir », « à faire »…) compte comme un manque.
GOOD = ("télé seulement :", "même sens :")


def allowed():
    out = {}
    if ALLOW.exists():
        for line in ALLOW.read_text().splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            k, _, why = line.partition("\t")
            out[k] = why
    return out


def main():
    tv, web, listed = tv_keys(), web_keys(), allowed()
    ok = {k: why for k, why in listed.items() if why.startswith(GOOD)}
    webn = {norm(k) for k in web}
    missing = {k: w for k, w in tv.items() if norm(k) not in webn and k not in ok}
    # La liste reste juste : une ligne pour un texte que la télé n'emploie plus, ou que le web a maintenant, est à retirer.
    stale = sorted(k for k in listed if k not in tv or norm(k) in webn)
    if "--all" in sys.argv:
        for k, w in sorted(tv.items(), key=lambda x: x[1]):
            if norm(k) not in webn:
                print(f"{'TÉLÉ SEULEMENT' if k in ok else 'MANQUE AU WEB '}  {w:28} {k}")
    else:
        for k, w in sorted(missing.items(), key=lambda x: x[1]):
            print(f"{w:28} {k}")
    if stale and "--nettoyer" in sys.argv:  # retire les lignes en trop de la liste
        drop = set(stale)
        lines = [l for l in ALLOW.read_text().splitlines() if l.startswith("#") or not l.strip() or l.partition("\t")[0] not in drop]
        ALLOW.write_text("\n".join(lines) + "\n")
        print(f"{len(stale)} ligne(s) retirée(s) de {ALLOW.name}")
        stale = []
    for k in stale:
        print(f"{'LIGNE EN TROP':28} {k}  (retirez-la de build-system/parite-tele-seulement.txt, ou --nettoyer)")
    print(f"{len(tv)} textes télé · {len(missing)} absents du web · {len(stale)} ligne(s) en trop (justifiés : {len(ok)})")
    # Dates : le web écrit les dates comme la télé (date_text : « 17/11/2026 14:36 », toutes langues), par mgDate. Une date
    # au format du navigateur (« 2026-11-17 » en français canadien) est un écart de détail (audit des cartes, 18/11).
    import re as _re
    dates = []
    for f in sorted((ROOT / "engine/web").glob("*.js")):
        for n, line in enumerate(f.read_text().splitlines(), 1):
            if _re.search(r"new Date\([^;]*?\)\.toLocale(Date|Time)?String\(", line):
                dates.append(f"{f.name}:{n}")
    for d in dates:
        print(f"{'DATE HORS FORMAT TÉLÉ':28} {d}  (utilisez mgDate)")
    return 1 if missing or stale or dates else 0


if __name__ == "__main__":
    sys.exit(main())
