// Onglet Backups (web) : même écran que la télé (app/src/ui_manage.cpp) — « Où sont mes copies, et sont-elles saines ? ».
// En haut, les ENDROITS (Partout, chaque disque USB, chaque NAS, Corbeille) ; puis les compteurs (Doublons sûrs ·
// Pas encore comparés · À vérifier) et, par jeu, où sont ses copies, une phrase qui dit quoi faire, ce qu'on libère.
// Vues Tous / Avec doublons / Pas encore comparés / À vérifier. Remplace « Par disque » et « Gérer les backups ».
// « Toutes les versions » d'un jeu : ses copies du plus récent au plus ancien (endroit, date, date d'écriture du jeu,
// avancée, contenu, taille, état), avec Restaurer sur chaque version, et la règle unique de sélection
// (case par ligne, case « Tout », barre d'actions collante, menu « ⋯ »).
// Le résumé vient du moteur (GET /backups/summary) : gardé, relu à la demande et après chaque action.
"use strict";
const mgViews = () => [T("Tous"), T("Avec doublons"), T("Pas encore comparés"), T("À vérifier")];
const mg = { sum: null, account: null, error: "", loading: false, loadP: null, view: 0, place: "", game: "", back: "", gsel: new Set(), csel: new Map(), prot: new Set(),
  trash: {}, trashLoading: new Set(), comparing: 0, task: "", taskGame: "", disk: {} };
const mgCk = (c) => c.place + "|" + c.bundle;
const mgGk = (g) => (g.full ? "full|" + (g.item || g.title) : g.account + "|" + (g.title_id || g.title));
const mgBytes = (c) => c.content_bytes || c.bytes || 0;  // contenu comparé (sauvegarde), sinon taille du backup complet
const mgKind = (k) => (k === "app" ? T("Appli") : k === "rom" ? "ROM" : T("Jeu"));
// Heure seule, comme la télé : « 14:36 ».
const mgTime = (t) => { const d = new Date(t * 1000), z = (n) => String(n).padStart(2, "0"); return `${z(d.getHours())}:${z(d.getMinutes())}`; };
// Même format que la télé (date_text) dans toutes les langues : « 17/11/2026 14:36 ».
const mgDate = (t, hour = true) => {
  if (!(t > 0)) return "?";
  const d = new Date(t * 1000), z = (n) => String(n).padStart(2, "0");
  const day = `${z(d.getDate())}/${z(d.getMonth() + 1)}/${d.getFullYear()}`;
  return hour ? `${day} ${z(d.getHours())}:${z(d.getMinutes())}` : day;
};
const mgPlay = (s) => (s > 0 ? (s < 3600 ? `${Math.max(1, Math.floor(s / 60))} min` : `${Math.floor(s / 3600)} h ${String(Math.floor(s / 60) % 60).padStart(2, "0")}`) : "");
const mgAccount = () => (((state.overview && state.overview.accounts) || []).find((a) => a.on_console && a.platform === "PS5") || {}).key || "";
// Copie « à vérifier » : incomplète, ou vide (contenu de 0 octet) — mise à part, rien n'est supprimé.
// « 0 octet lu » n'est douteux que si la copie est vraiment presque vide (une grosse copie illisible n'est pas vide), comme la télé.
const mgSuspect = (c) => !c.other_account && !c.on_peer && (c.complete === false || (c.content_bytes === 0 && (c.bytes || 0) < 1048576));
// Endroit choisi : un disque (id exact), ou toute une autre PS5 (« peer:<instance>: », toutes ses copies).
const mgIn = (c, place) => !place || c.place === place || (place.startsWith("peer:") && place.endsWith(":") && (c.place || "").startsWith(place));
const mgPeerKey = (c) => "peer:" + c.peer + ":";
const mgGame = (key) => ((mg.sum && mg.sum.games) || []).find((g) => mgGk(g) === key);

// Copies proposées à la corbeille : le choix fait à la main, sinon la proposition du moteur ; jamais une
// copie protégée ni une copie d'un autre compte. « place » : seulement celles de cet endroit.
function mgChosen(g, place = "") {
  const own = mg.csel.get(mgGk(g)), out = new Set();
  g.copies.forEach((c, i) => {
    const k = mgCk(c);
    if (mg.prot.has(k) || c.other_account || c.on_peer || !mgIn(c, place)) return;
    if (own ? own.has(k) : (g.remove || []).includes(i)) out.add(k);
  });
  return out;
}

// Même partie à plusieurs endroits (comme la télé) : des copies voulues, triées seulement si on le demande.
const spotName = (c) => { if (c.on_peer) return c.peer_name; const l = c.place_label || "", p = l.indexOf(" ("); return p > 0 && l.slice(0, p).includes(l.slice(p + 2, -1)) ? l.slice(0, p) : l; };
function spreadOf(g) {
  const by = new Map(), add = (L, n) => { if (!L) return; const v = by.get(L) || []; if (!v.includes(n)) v.push(n); by.set(L, v); };
  add(g.console && g.console.group, T("Cette PS5"));
  for (const c of g.copies) if (!c.other_account) add(c.group, spotName(c));
  return by;
}
function mainSpread(g) {
  const by = spreadOf(g), cg = g.console && g.console.group;
  if (cg && by.has(cg)) return by.get(cg);
  let best = [];
  for (const v of by.values()) if (v.length > best.length) best = v;
  return best;
}
const spreadCopy = (c, by) => !c.other_account && !c.on_peer && c.complete !== false && (by.get(c.group) || []).length >= 2;
const spreadLine = (g) => { const sp = g.full ? [] : mainSpread(g); return sp.length >= 2 ? T("Même partie à %d endroits : %s", sp.length, sp.join(" · ")) : ""; };
function mgTidyUndo() { for (const k of mg.tidyGames || []) mg.csel.delete(k); mg.tidyGames = []; }
// Un jeu (g) : chaque copie dont la partie existe ailleurs, cochée = gardée. Sans g : chaque endroit, pour tous les jeux de la vue.
// Copie rangée dans un partage reçu (lecture seule) : on la lit, on la restaure, on ne la supprime pas.
const roPlace = (id) => !!(state.places || []).find((p) => p.id === id && p.readonly);
function mgTidy(g, list) {
  const rows = [];
  if (g) {
    const by = spreadOf(g);
    for (const c of g.copies) if (spreadCopy(c, by) && !mg.prot.has(mgCk(c)) && !roPlace(c.place)) rows.push({ key: mgCk(c), label: spotName(c), letter: c.group, sub: T("copiée le %s", mgDate(c.created_unix, false)) + " · " + T("même partie à %d endroits", by.get(c.group).length), keep: true });
  } else {
    const at = new Map();
    for (const x of list) { const by = spreadOf(x); for (const c of x.copies) if (spreadCopy(c, by) && !mg.prot.has(mgCk(c)) && !roPlace(c.place)) { const r = at.get(c.place) || { key: c.place, label: spotName(c), n: 0, keep: true }; r.n++; at.set(c.place, r); } }
    for (const r of at.values()) { r.sub = T("%d copie(s) d'une partie rangée aussi ailleurs", r.n); rows.push(r); }
  }
  if (!rows.length) { toast(T("Rien à trier ici")); return; }
  const locked = (r) => {
    if (!g || !r.keep) return false;
    const others = (g.console && g.console.group === r.letter ? 1 : 0) + g.copies.filter((c) => c.on_peer && c.group === r.letter).length + rows.filter((o) => o !== r && o.keep && o.letter === r.letter).length;
    return others === 0;
  };
  const box = document.createElement("div");
  box.className = "modal";
  const shut = () => box.parentNode && box.parentNode.removeChild(box);
  const draw = () => {
    box.innerHTML = `<div class="dialog wide"><h2>${esc(g ? T("Faire le tri : %s", g.title) : T("Garder seulement sur…"))}</h2>
      <p class="hint">${esc(g ? T("Cochée = gardée. Ce sont des copies de la même partie ; la dernière copie d'une partie ne peut pas être décochée.") : T("Décochez un endroit : les copies d'une partie rangée aussi ailleurs y seront retirées. La dernière copie d'une partie reste toujours."))}</p>
      <div class="list">${rows.map((r, i) => { const lk = locked(r); return `<label class="row"><input type="checkbox" data-ti="${i}" ${r.keep ? "checked" : ""} ${lk ? "disabled" : ""}>
        <div class="grow"><div class="t">${esc(r.label)}</div><div class="d">${esc(r.sub)}</div></div><span class="tag ${r.keep ? "ok" : ""}">${esc(lk ? T("dernière copie") : r.keep ? T("gardée") : T("à la corbeille"))}</span></label>`; }).join("")}</div>
      <div class="actions"><button id="ti-go">${T("Appliquer (aperçu avant)")}</button><button class="secondary" id="ti-cancel">${T("Annuler")}</button></div></div>`;
    $$("[data-ti]", box).forEach((b) => b.onchange = () => { rows[+b.dataset.ti].keep = b.checked; draw(); });
    $("#ti-cancel", box).onclick = () => { shut(); toast(T("Rien n'a été modifié")); };
    $("#ti-go", box).onclick = () => {
      const touched = [];
      if (g) {
        const drop = new Set(rows.filter((r) => !r.keep).map((r) => r.key));
        if (drop.size) { mg.csel.set(mgGk(g), drop); touched.push(g); }
      } else {
        const off = new Set(rows.filter((r) => !r.keep).map((r) => r.key));
        for (const x of list) {
          const by = spreadOf(x), stays = new Set([x.console && x.console.group]);
          for (const c of x.copies) if (!c.other_account && (c.on_peer || !off.has(c.place) || mg.prot.has(mgCk(c)))) stays.add(c.group);
          const drop = new Set(x.copies.filter((c) => spreadCopy(c, by) && off.has(c.place) && !mg.prot.has(mgCk(c)) && stays.has(c.group)).map(mgCk));
          if (drop.size) { mg.csel.set(mgGk(x), drop); touched.push(x); }
        }
      }
      shut();
      if (!touched.length) { toast(T("Tout est gardé : rien n'a changé")); return; }
      mg.tidyGames = touched.map(mgGk);
      mgPreview(touched);
    };
  };
  draw();
  box.onclick = (e) => { if (e.target === box) { shut(); toast(T("Rien n'a été modifié")); } };
  document.body.appendChild(box);
}

// Phrase simple tirée des nombres (mêmes textes que la télé).
function mgSummary(g) {
  if (g.full) {  // backup complet (jeu, appli, homebrew, ROM) : « 3 copies complètes (jeu + données) · 1,2 Go »
    const parts = [...new Set(g.copies.flatMap((c) => c.parts || []).map((p) => (PARTS[p] || p).toLowerCase()))].join(" + ");
    const bytes = g.copies.reduce((a, c) => a + mgBytes(c), 0);
    return parts ? T("%d copie(s) complète(s) (%s) · %s", g.copies.length, parts, size(bytes)) : T("%d copie(s) complète(s) · %s", g.copies.length, size(bytes));
  }
  const mine = g.copies.filter((c) => !c.other_account && !c.on_peer && c.complete !== false);
  const onPeer = g.copies.filter((c) => c.on_peer);
  if (!mine.length && onPeer.length) return T("%d copie(s) rangée(s) sur %s : récupérables ici.", onPeer.length, onPeer[0].peer_name);
  const n = mine.length, rm = (g.remove || []).length;  // les copies d'un autre compte sont comptées à part
  const groups = new Set(mine.map((c) => c.group || ""));
  const cg = g.console && g.console.group;
  const allConsole = !!cg && mine.every((c) => c.group === cg);
  const keep = Math.max(0, mine.length - rm);
  if (!g.compared) return n === 1 ? T("1 copie, pas encore comparée.") : T("%d copies, pas encore comparées.", n);
  if (rm) {
    if (groups.size === 1 && allConsole) return T("%d copies, toutes identiques à la console : garder %d, %d doublon(s).", n, keep, rm);
    if (groups.size === 1) return T("%d copies, toutes identiques : garder %d, %d doublon(s).", n, keep, rm);
    return T("%d copies, %d contenus différents : garder %d, %d doublon(s).", n, groups.size, keep, rm);
  }
  if (n === 1) return T("1 copie, rien à nettoyer.");
  if (groups.size === mine.length && mine.length > 1) return T("%d copies, toutes différentes : rien à nettoyer.", n);
  return T("%d copies à garder : rien à nettoyer.", n);
}

// Avancée : celle de la console, sinon celle de la copie la plus avancée ; temps de jeu ; copies d'un autre compte.
function mgProgress(g) {
  const pg = (g.console && g.console.progress) || {};
  let t = pg.text || "", play = pg.play_time_seconds || 0;
  if (!t) {
    let best = null;
    for (const c of g.copies) {
      if (!c.progress_text) continue;
      const p = c.percent ?? -1, bp = best ? best.percent ?? -1 : -2;
      if (!best || p > bp || (p === bp && (c.play_time_seconds || 0) > (best.play_time_seconds || 0))) best = c;
    }
    if (best) { t = best.progress_text; if (play <= 0) play = best.play_time_seconds || 0; }
  }
  if (play <= 0) play = g.play_time_tracked || 0;
  if (play >= 60 && !t.includes(":")) t += (t ? " · " : "") + T("%s de jeu", mgPlay(play));
  const others = g.copies.filter((c) => c.other_account).length;
  if (others) t += (t ? " · " : "") + T("%d copie(s) d'un autre compte", others);
  return t;
}

function mgPlaces(g) {
  const seen = g.console ? [T("Console")] : [];
  for (const c of g.copies) { const l = spotName(c); if (!seen.includes(l)) seen.push(l); }  // l'autre PS5 une seule fois, sans redite du service
  return seen.join(" · ");
}

function mgCover(g) {
  const tid = g.title_id || ((g.item || "").startsWith("title:") ? g.item.slice(6) : "");  // cartes « À faire » : seulement la clé
  const it = state.items.find((i) => i.key === g.item) || (tid && state.items.find((i) => i.title_id === tid && i.has_cover))
    || (tid && /^[0-9A-Fa-f]{16}$/.test(tid) ? { key: "tid:switch:" + tid, title: g.title, kind: "rom", platform: "Switch", has_cover: true, apps: [] } : null)  // Switch absent d'ici
    || (tid && /^[A-Z]{4}\d{5}$/.test(tid) ? { key: (tid.startsWith("CUSA") ? "tid:ps4:" : "tid:ps5:") + tid, title: g.title, kind: g.kind === "app" ? "app" : tid.startsWith("CUSA") ? "game_ps4" : "game_ps5", platform: tid.startsWith("CUSA") ? "PS4" : "PS5", has_cover: true, apps: [] }
    : { key: g.item, title: g.title, kind: g.kind === "app" ? "app" : "game_ps5", platform: "PS5", has_cover: (g.item || "").startsWith("tid:"), apps: [] });  // une appli reste une appli
  return `<div class="thumb">${coverHtml(it, true)}</div>`;
}

// Lecture du résumé ; une seule à la fois (les demandes pendant une lecture en relancent une après).
function mgLoad() {
  if (mg.loadP) { mg.again = true; return mg.loadP; }
  mg.loading = true;
  mg.loadP = (async () => {
    try {
      mg.sum = await api(`/backups/summary?account=${encodeURIComponent(mg.account || "")}`); mg.error = "";
      for (const g of mg.sum.games || []) for (const c of g.copies || []) if (c.protected) mg.prot.add(mgCk(c));  // protégées connues du moteur
    } catch (e) { mg.error = e.message; }
    mg.loading = false; mg.loadP = null;
    if (mg.sum && mg.sum.updating) setTimeout(() => mgLoad(), 3000);  // le moteur recompte encore : redemander bientôt
    if (mg.again) { mg.again = false; return mgLoad(); }
    mgRedraw();
  })();
  return mg.loadP;
}
// Le résumé sert aussi à l'Accueil (« À faire ») : on redessine l'onglet affiché.
function mgRedraw() {
  if (state.tab === "backups" || (state.tab === "home" && !$(".modal"))) render();
}
// Résumé prêt (Accueil, fiche → Toutes les versions, Parties) : lu une fois, puis gardé.
async function mgReady() {
  if (!state.linkPeers) mgLoadPeers();  // consoles jumelées, pour « Envoyer à … »
  await loadOverview();
  const acc = mgAccount();
  if (mg.account !== acc) { mg.account = acc; mg.sum = null; mg.error = ""; }
  if (!mg.sum) await mgLoad();
  return mg.sum;
}
// « Toutes les versions » d'un élément de la bibliothèque, d'une partie ou d'un jeu : le détail de l'onglet Backups.
async function openVersions(ref) {
  const sum = await mgReady();
  const g = ((sum && sum.games) || []).find((x) => (ref.key && x.item === ref.key) || (ref.title_id && x.title_id === ref.title_id));
  if (!g) { toast(T("Aucune version sauvegardée pour cet élément")); return; }
  closeSheet();
  mg.back = state.tab === "backups" ? "" : state.tab;
  mg.game = mgGk(g);
  setTab("backups");
}
// « Comparer » d'un jeu (Bibliothèque, comme la télé) : l'outil Comparer · trier, ouvert sur ce jeu ; on prévient s'il
// n'a de copies qu'à un seul endroit.
async function openCompare(ref) {
  const sum = await mgReady();
  closeSheet();
  mg.back = ""; mg.game = ""; mg.place = "compare"; mg.cfilter = 0;
  const g = ((sum && sum.games) || []).find((x) => (ref.title_id && x.title_id === ref.title_id) || (ref.key && x.item === ref.key));
  if (sum && (!g || cmpPlaces(g) <= 1)) toast(T("Ce jeu n'a pas de copies à plusieurs endroits."));
  mg.cmpFocus = g && cmpPlaces(g) > 1 ? mgGk(g) : "";
  setTab("backups");
}

// Endroits : Partout, chaque disque USB, chaque NAS (et l'interne s'il garde des copies), Corbeille.
const TRASH_ICON = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" class="trash-ico" aria-hidden="true"><path d="M4 7h16M9 7V4.5h6V7M6.5 7l1 13h9l1-13M10 11v6M14 11v6"/></svg>`;
const COMPARE_ICON = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="4" width="10" height="14" rx="2"/><rect x="11" y="7" width="10" height="14" rx="2"/><path d="M14 14l2 2 3-4"/></svg>`;
function mgPlaceList() {
  const count = new Map();
  let total = 0;
  const peers = new Map();  // autre PS5 : une carte par console (toutes ses copies, disques et NAS)
  for (const g of (mg.sum && mg.sum.games) || []) for (const c of g.copies) {
    total++;
    if (c.on_peer) { const k = mgPeerKey(c); const x = peers.get(k) || { name: c.peer_name, n: 0 }; x.n++; peers.set(k, x); continue; }
    count.set(c.place, (count.get(c.place) || 0) + 1);
  }
  const own = ((mg.sum && mg.sum.games) || []).reduce((n, g) => n + g.copies.filter((c) => !c.other_account).length, 0);
  const out = [{ id: "", name: T("Partout"), sub: T("%d copie(s)", own), sub2: T("%d jeu(x)", mg.sum.games.length), icon: ICONS.consoles }];  // comme la télé : deux lignes
  for (const p of state.places) {
    const n = count.get(p.id) || 0;
    if (!(p.type === "usb" || p.type === "ftp" || n) || p.use === "sync" || p.same_backups_as) continue;  // NAS « Synchro seulement » : pas ici ; même NAS en FTP et WebDAV : une fois
    if (p.type === "switch" && !n) continue;
    if (!n && p.type === "ftp" && ["s3", "gdrive", "onedrive", "dropbox", "mega"].includes(p.protocol)) continue;  // nuage sans copie : pas de carte (comme la télé)
    const free = p.type === "usb" || p.type === "internal" ? " · " + T("%s libres", size(p.free)) : "";
    out.push({ id: p.id, n, name: p.label, sub: (n ? T("%d copie(s)", n) : T("vide")) + free, icon: p.type === "ftp" && ["s3", "gdrive", "onedrive", "dropbox", "mega"].includes(p.protocol) ? cloudIcon(p.protocol, p.service) : ICONS[p.type === "usb" ? "usb" : p.type === "internal" ? "ps5" : p.type === "switch" ? "switch" : "nas"] });  // nuage : logo du service
  }
  for (const [id, n] of count) if (!out.some((p) => p.id === id)) {  // disque absent : ses copies restent visibles
    const g = mg.sum.games.find((x) => x.copies.some((c) => c.place === id)), c = g && g.copies.find((x) => x.place === id);
    out.push({ id, n, name: c ? c.place_label : id, sub: T("%d copie(s)", n) + " · " + T("Disque absent"), icon: ICONS.usb });
  }
  for (const [id, x] of peers) out.push({ id, n: x.n, name: x.name, sub: T("%d copie(s)", x.n), icon: ICONS.ps5, peer: true });
  // Même ordre que la télé : Partout, cette PS5 (interne, USB), les autres PS5, le NAS, le nuage, puis la Corbeille.
  const placeOf = (id) => state.places.find((p) => p.id === id);
  const rank = (o) => {
    if (o.id === "") return 0;
    if (o.peer) return 2;
    const p = placeOf(o.id);
    if (!p || p.type === "usb" || p.type === "internal") return 1;
    return p.type === "ftp" && ["s3", "gdrive", "onedrive", "dropbox", "mega"].includes(p.protocol) ? 4 : 3;
  };
  const sorted = out.map((o, i) => [o, i]).sort((a, b) => rank(a[0]) - rank(b[0]) || a[1] - b[1]).map((x) => x[0]);
  // Famille de chaque endroit (comme la télé) : cette PS5 (interne, USB), chaque autre PS5, le NAS, le nuage.
  for (const o of sorted) { const r = rank(o); o.n = o.n || 0; o.fam = r === 0 ? "all" : r === 1 ? "ps5" : r === 2 ? "peer:" + o.id : r === 3 ? "nas" : "cloud"; }
  const left = (mg.sum && mg.sum.to_compare) || 0;  // l'outil dit où on en est (comme la télé)
  sorted.push({ id: "compare", name: T("Comparer · trier"), sub: left ? T("%d à comparer", left) : T("Tout est comparé"), tone: left ? "warn" : "ok", icon: COMPARE_ICON });
  sorted.push({ id: "trash", name: T("Corbeille"), sub: T("gardée 30 jours"), icon: TRASH_ICON });
  return sorted;
}

// Compteurs de l'endroit choisi : doublons sûrs (et la place libérée), jeux pas encore comparés, copies à vérifier.
function mgStats(place) {
  let dup = 0, freed = 0, cmp = 0, chk = 0;
  for (const g of (mg.sum && mg.sum.games) || []) {
    if (!g.copies.some((c) => mgIn(c, place))) continue;
    g.copies.forEach((c, i) => {
      if (!mgIn(c, place)) return;
      if ((g.remove || []).includes(i)) { dup++; freed += mgBytes(c); }
      if (mgSuspect(c)) chk++;
    });
    if (!g.compared && !g.full) cmp++;
  }
  return { dup, freed, cmp, chk };
}

function backupsTab() {
  // Comparaisons en cours (Tout comparer) : le résumé est relu à chaque jeu terminé.
  const comparing = (state.tasks || []).filter((t) => t.type === "inspect_save" && ["queued", "running"].includes(t.state)).length;
  if (comparing < mg.comparing && mg.sum) mgLoad();
  mg.comparing = comparing;
  if (mg.taskGame) {
    const t = (state.tasks || []).find((x) => x.id === mg.task);
    if (t && ["done", "failed", "cancelled"].includes(t.state)) {
      toast(t.state === "done" ? T("Contenu comparé : les versions identiques ont la même lettre") : (t.error || T("Comparaison impossible")));
      mg.task = mg.taskGame = ""; mgLoad();
    }
  }
  const view = $("#view");
  if (!state.overview) {  // le compte de la console vient de /saves/overview
    view.innerHTML = `<h1>${T("Sauvegardes")}</h1>${loading(T("Lecture des sauvegardes…"))}<p class="hint">${esc(T("Copies sur les disques et le NAS, et ce que dit leur contenu."))}</p>`;
    selBar(0);
    loadOverview().then(() => { if (state.tab === "backups") backupsTab(); });
    return;
  }
  const acc = mgAccount();
  if (mg.account !== acc) { mg.account = acc; mg.sum = null; mg.error = ""; }
  if (!mg.sum && !mg.loadP && !mg.error) mgLoad();
  if (mg.game && mgGame(mg.game)) return mgGameView(view);
  if (mg.game && !mg.sum) { view.innerHTML = `<h1>${T("Détails")}</h1>${loading(T("Lecture des sauvegardes…"))}`; selBar(0); return; }
  mg.game = "";
  const places = mgPlaceList();
  if (!places.some((p) => p.id === mg.place)) mg.place = "";
  // Comme la télé : une famille de deux endroits ou plus tient en une carte (« Cette PS5 · 40 copie(s) · 3 endroits › ») ;
  // un clic l'ouvre, une seule famille ouverte à la fois, et celle de l'endroit choisi reste ouverte.
  const famSize = {};
  for (const p of places) if (p.fam) famSize[p.fam] = (famSize[p.fam] || 0) + 1;
  const chosenFam = (places.find((p) => p.id === mg.place) || {}).fam;
  if (chosenFam && chosenFam !== "all" && famSize[chosenFam] > 1) mg.famOpen = chosenFam;
  const famName = (f) => f === "ps5" ? T("Cette PS5") : f === "cloud" ? T("Nuage") : "NAS";
  const famIcon = (f) => f === "ps5" ? ICONS.ps5 : f === "cloud" ? ICONS.cloud : ICONS.nas;
  const famCopies = (f) => places.filter((p) => p.fam === f).reduce((n, p) => n + (p.n || 0), 0);
  const cards = [], seenFam = new Set();
  for (const p of places.filter((x) => x.id !== "compare" && x.id !== "trash")) {
    if (p.fam && p.fam !== "all" && famSize[p.fam] > 1 && mg.famOpen !== p.fam) {
      if (seenFam.has(p.fam)) continue;
      seenFam.add(p.fam);
      cards.push(`<button class="tr-dev place-card fam-card" data-fam="${esc(p.fam)}"><span class="dev-ico">${famIcon(p.fam)}</span><span class="grow"><b>${esc(famName(p.fam))}</b><span class="hint">${esc(T("%d copie(s)", famCopies(p.fam)))}</span><span class="hint accent">${esc(T("%d endroits ›", famSize[p.fam]))}</span></span></button>`);
      continue;
    }
    cards.push(`<button class="tr-dev place-card ${mg.place === p.id ? "chosen" : ""} ${p.fam && mg.famOpen === p.fam && famSize[p.fam] > 1 ? "fam-open" : ""}" data-place="${esc(p.id)}">
      <span class="dev-ico">${p.icon}</span><span class="grow"><b>${esc(p.name)}</b><span class="hint">${esc(p.sub)}</span>${p.sub2 ? `<span class="hint">${esc(p.sub2)}</span>` : ""}</span></button>`);
  }
  view.innerHTML = `<h1>${T("Sauvegardes")}${mg.loading ? " " + SPIN : ""}</h1><p class="hint tab-help">${esc(T("Vos copies : sur la console, le NAS, le nuage et l'autre PS5."))}</p>
    <div class="tr-label">${T("ENDROITS")}</div>
    <div class="places-wrap"><div class="places" data-scroll="bk-places">${cards.join("")}</div>
      <div class="place-tools">${places.filter((p) => p.id === "compare" || p.id === "trash").map((p) => `<button class="tr-dev place-card ${p.id === "trash" ? "trash-card" : ""} ${mg.place === p.id ? "chosen" : ""}" data-place="${esc(p.id)}">
        <span class="dev-ico tool-ico">${p.icon}</span><span class="grow"><b>${esc(p.name)}</b><span class="hint ${p.tone || ""}">${esc(p.sub)}</span></span></button>`).join("")}</div></div>
    <div id="bk-main"></div>`;
  $$("[data-fam]", view).forEach((b) => b.onclick = () => { mg.famOpen = b.dataset.fam; backupsTab(); });
  $$("[data-place]", view).forEach((b) => b.onclick = () => { mg.place = b.dataset.place; mg.gsel.clear(); mg.csel.clear(); if (mg.place === "trash") mg.trash = {}; backupsTab(); });
  const main = $("#bk-main");
  if (!mg.sum) {
    main.innerHTML = mg.error
      ? `<div class="empty"><p class="bad">${esc(T("Lecture impossible : %s", mg.error))}</p><p class="hint">${T("Il faut le moteur 2.33 ou plus récent pour cet écran.")}</p><button class="secondary" id="mg-retry">${T("Actualiser")}</button></div>`
      : loading(T("Lecture des sauvegardes…")) + `<p class="hint">${esc(T("Copies sur les disques et le NAS, et ce que dit leur contenu."))}</p>`;
    const r = $("#mg-retry");
    if (r) r.onclick = () => { mg.error = ""; backupsTab(); };
    selBar(0);
    return;
  }
  if (mg.place === "trash") { selBar(0); main.innerHTML = `<div id="mg-trash"></div>`; return mgTrashView($("#mg-trash")); }
  if (mg.place === "compare") { selBar(0); return mgCompareView(main); }
  mgOverview(main);
  if (mg.place.startsWith("peer:")) {  // une autre PS5 : synchroniser les copies avec elle
    const inst = mg.place.slice(5, -1), card = places.find((p) => p.id === mg.place);
    main.insertAdjacentHTML("afterbegin", `<div class="actions"><button class="primary" id="mg-sync">${esc(T("Synchroniser les copies avec %s…", card ? card.name : inst))}</button></div>`);
    $("#mg-sync").onclick = (ev) => syncCopiesDialog(inst, card ? card.name : inst, false);
  }
}

// Synchro des copies (comme la télé) : aperçu de ce qui manque de chaque côté, puis validation.
async function syncCopiesDialog(peer, name, full) {
  let plan;
  toast(T("Calcul de ce qui manque de chaque côté…"));  // comme la télé : un retour pendant le calcul
  try { plan = await api(`/sync/copies/plan?peer=${encodeURIComponent(peer)}${full ? "&full=1" : ""}`); } catch (e) { toast(e.message || T("réponse illisible")); return; }
  if (plan.error) { toast(plan.error); return; }
  const np = plan.pull.length, ns = plan.push.length, jf = (plan.pull_full || 0) + (plan.push_full || 0);
  let note = np + ns === 0 ? T("Rien ne manque : les deux consoles ont les mêmes copies.") + " "
    : T("Ici ← %d copie(s) (%s) depuis %s, rangées sur %s. %s ← %d copie(s) (%s), rangées sur %s.", np, size(plan.pull_bytes || 0), name, plan.here_label || "", name, ns, size(plan.push_bytes || 0), plan.peer_label || "") + " ";
  note += T("On ajoute seulement ce qui manque : rien n'est écrasé ni supprimé, et rien ne change dans les parties des consoles.");
  if (!full && jf) note += " " + T("%d jeu(x) complet(s) laissé(s) de côté (gros) : à inclure si vous voulez.", jf);
  if (!np && !ns) {
    if (!full && jf) { if (await ask(T("Synchroniser les copies avec %s", name), `<p>${esc(note)}</p>`, T("Inclure aussi les jeux complets (%d)…", jf))) syncCopiesDialog(peer, name, true); }
    else await ask(T("Synchroniser les copies avec %s", name), `<p>${esc(note)}</p>`, T("Compris"), "");
    return;
  }
  if (!(await ask(T("Synchroniser les copies avec %s", name), `<p>${esc(note)}</p>`, T("Synchroniser : %d ici, %d là-bas", np, ns)))) return;
  try { await post("/sync/copies/start", { peer, include_full: full }); toast(T("Synchro des copies avec %s lancée (suivie sur l'accueil)", name)); syncState.at = 0; } catch (e) { toast(e.message); }
}

// Synchroniser les jeux suivis (comme la télé) : seulement eux ; aucun suivi → on propose de les choisir.
async function syncFollowedDialog(peer, name) {
  let plan;
  try { plan = await api(`/sync/copies/plan?peer=${encodeURIComponent(peer)}&full=1`); } catch (e) { toast(e.message); return; }
  if (plan.error) { toast(plan.error); return; }
  const np = plan.followed_pull || 0, ns = plan.followed_push || 0, none = plan.mode !== "all" && !plan.followed_games;
  const note = none ? T("Aucun jeu suivi pour l'instant : choisissez ceux à garder au même niveau sur les deux consoles.")
    : np + ns === 0 ? T("Vos jeux suivis ont les mêmes copies des deux côtés.") : T("Seulement vos jeux suivis. On ajoute ce qui manque : rien n'est écrasé ni supprimé.");
  if (none || np + ns === 0) {
    if (await ask(T("Synchroniser les jeux suivis avec %s", name), `<p>${esc(note)}</p>`, T("Choisir les jeux suivis…"))) { SY.dev = peer; setTab("synchro"); synchroOpenPick(peer); }
    return;
  }
  if (!(await ask(T("Synchroniser les jeux suivis avec %s", name), `<p>${esc(note)}</p>`, T("Synchroniser : %d ici, %d là-bas", np, ns)))) return;
  try { await post("/sync/copies/start", { peer, include_full: true, followed: true }); toast(T("Synchro des copies avec %s lancée (suivie sur l'accueil)", name)); syncState.at = 0; } catch (e) { toast(e.message); }
}

// État de la synchro des copies (bulles de l'accueil) : avancement, et ce qui manque avec chaque autre PS5.
const syncState = { status: {}, plans: [], at: 0, plansAt: 0 };
async function loadSyncState() {
  const running = syncState.status.state === "running";
  if (Date.now() - syncState.at > (running ? 2000 : 20000)) {
    syncState.at = Date.now();
    try { const s = await api("/sync/copies/status"); const was = syncState.status.state; syncState.status = s; if (was === "running" && s.state !== "running") { syncState.plansAt = 0; mg.sum = null; } } catch (e) { /* rien */ }
  }
  const nasPlaces = (state.places || []).filter((p) => p.type === "ftp" && p.use !== "backups" && !p.same_as);  // NAS qui servent à la synchro (un dossier une fois)
  if (Date.now() - syncState.plansAt > 60000 && ((typeof peerGames !== "undefined" && peerGames.length) || nasPlaces.length)) {  // PS5 jumelées et NAS
    syncState.plansAt = Date.now();
    const out = [];
    for (const p of (typeof peerGames !== "undefined" ? peerGames : [])) { try { const pl = await api(`/sync/copies/plan?peer=${encodeURIComponent(p.instance)}&full=1`); if (!pl.error) out.push({ name: p.name, plan: pl }); } catch (e) { /* hors ligne */ } }
    for (const n of nasPlaces) { try { const pl = await api(`/sync/copies/plan?peer=${encodeURIComponent(n.id)}&full=1`); if (!pl.error) out.push({ name: nasSyncName(n), plan: pl }); } catch (e) { /* injoignable */ } }
    syncState.plans = out;
  }
}

function mgOverview(main) {
  const s = mg.sum, games = s.games, place = mg.place;
  // Les trois bulles disent la même chose que la télé : pour toutes les copies (pas seulement l'endroit choisi) ;
  // « À vérifier » compte les jeux qui ont au moins une copie à vérifier.
  const st0 = mgStats("");
  const st = { ...st0, freed: s.freed_bytes != null ? Math.max(0, s.freed_bytes) : st0.freed, cmp: s.to_compare != null ? s.to_compare : st0.cmp,
    chk: ((s && s.games) || []).filter((g) => g.copies.some(mgSuspect)).length };
  const here = games.filter((g) => g.copies.some((c) => mgIn(c, place)));
  const rank = (g) => (mgChosen(g, place).size && !mg.csel.has(mgGk(g)) ? 0 : !g.compared && !g.full ? 1 : 2);
  const rows = here.filter((g) => (mg.view === 1 ? g.copies.some((c, i) => mgIn(c, place) && (g.remove || []).includes(i)) : mg.view === 2 ? !g.compared && !g.full
    : mg.view === 3 ? g.copies.some((c) => mgIn(c, place) && mgSuspect(c)) : true))
    .sort((a, b) => rank(a) - rank(b) || (b.freed_bytes || 0) - (a.freed_bytes || 0) || a.title.localeCompare(b.title));
  for (const k of [...mg.gsel]) if (!games.some((g) => mgGk(g) === k)) mg.gsel.delete(k);
  const picked = games.filter((g) => mg.gsel.has(mgGk(g)));
  let nc = 0, bytes = 0;
  for (const g of picked) { const ch = mgChosen(g, place); nc += ch.size; for (const c of g.copies) if (ch.has(mgCk(c))) bytes += mgBytes(c); }
  const shownOn = rows.filter((g) => mg.gsel.has(mgGk(g))).length;
  // Les trois bulles de la télé (mêmes textes, mêmes couleurs) ; un clic : nettoyer, tout comparer, voir les copies à vérifier.
  main.innerHTML = `<div class="mg-pills">
      <button class="mg-pill ok" id="mg-clean" ${st.dup ? "" : "disabled"} title="${esc(T("Nettoyer les doublons…"))}">${esc(T("Doublons sûrs : %s à libérer", size(st.freed)))}</button>
      <button class="mg-pill ${s.to_compare ? "warn" : "ok"}" id="mg-compare" ${s.to_compare ? "" : "disabled"} title="${esc(T("Tout comparer"))}">${mg.comparing ? SPIN + esc(T("comparaison en cours : %d jeu(x)", mg.comparing)) : esc(T("Pas encore comparés : %lld jeu(x)").replace("%lld", st.cmp))}</button>
      <button class="mg-pill ${st.chk ? "warn" : "ok"}" data-goview="3" title="${esc(T("vides ou incomplètes : mises à part, rien n'est supprimé"))}">${esc(T("À vérifier : %d", st.chk))}</button>
    </div>
    <div class="filters mg-bar"><div class="pills">${mgViews().map((l, i) => `<button data-v="${i}" class="${mg.view === i ? "active" : ""}">${esc(l)}</button>`).join("")}</div></div>
    <div class="list-head">${allBoxHtml("mg-all", shownOn, rows.length)}<span class="hint">${esc(T("%d jeu(x)", rows.length))}${place ? " · " + esc(placeLabel(place)) : ""}</span></div>
    <div class="list">${rows.length ? rows.map((g) => {
      const k = mgGk(g), ch = mgChosen(g, place);
      const rm = g.copies.filter((c, i) => mgIn(c, place) && (g.remove || []).includes(i)).length;
      const bad = g.copies.filter((c) => mgIn(c, place) && mgSuspect(c)).length;
      const on = mg.gsel.has(k);
      const [badge, tone] = on && ch.size ? [T("Sélectionné · %d copie(s)", ch.size), "info"]
        : bad && mg.view === 3 ? [T("%d copie(s) à vérifier", bad), "warn"]
        : g.full ? [mgKind(g.kind) + " · " + size(g.copies.reduce((a, c) => a + mgBytes(c), 0)), ""]
        : rm ? [g.freed_bytes > 0 ? T("Libère %s", size(g.freed_bytes)) : T("%d doublon(s)", rm), "ok"] : !g.compared ? [T("À comparer"), "warn"]
        : bad ? [T("%d copie(s) à vérifier", bad), "warn"] : [T("Rien à faire"), "muted"];
      const pl = mgProgress(g);
      // Un endroit choisi : combien de copies ici, la dernière, et combien d'autres endroits gardent ce jeu.
      const hereC = place ? g.copies.filter((c) => mgIn(c, place)) : [];
      const nPlaces = new Set([...(g.console ? ["console"] : []), ...g.copies.map((c) => (c.on_peer ? "peer:" + c.peer_name : c.place))]).size;
      let line = mgSummary(g), tag = [badge, tone];
      if (hereC.length) {
        const last = Math.max(...hereC.map((c) => c.created_unix || 0));
        line = hereC[0].on_peer  // carte d'une autre PS5 (comme la télé) : ses copies se gèrent depuis cette console-là
          ? T("%d copie(s) sur %s · la dernière le %s · gérées depuis cette console-là", hereC.length, hereC[0].peer_name || spotName(hereC[0]), mgDate(last, false))
          : T("%d copie(s) sur %s · la dernière le %s", hereC.length, spotName(hereC[0]), mgDate(last, false)) + (nPlaces > 1 ? " · " + T("aussi à %d autre(s) endroit(s)", nPlaces - 1) : "");
        if (!on && !g.full && !hereC[0].on_peer && !(bad && mg.view === 3)) tag = ch.size ? [T("%d doublon(s) ici", ch.size), "ok"] : !g.compared ? [T("À comparer"), "warn"] : [T("Rien à faire ici"), "muted"];
      }
      return `<div class="row mg-game ${on ? "on" : ""}" data-g="${esc(k)}">${ckHtml(`data-c="${esc(k)}"`, on)}${mgCover(g)}
        <div class="grow"><div class="t">${esc(g.title)} <span class="hint">${esc(mgPlaces(g))}</span></div><div>${esc(line)}</div>${pl ? `<div class="d progress-text">${esc(pl)}</div>` : ""}</div>
        <span class="tag ${tag[1]}">${esc(tag[0])}</span>${moreBtn(`data-m="${esc(k)}"`)}</div>`;
    }).join("") : `<div class="empty">${mg.view === 1 ? T("Aucun doublon sûr pour l'instant.") : mg.view === 2 ? T("Tout est comparé.") : mg.view === 3 ? T("Aucune copie à vérifier.") : T("Aucune sauvegarde trouvée.")}</div>`}</div>
    ${place && place !== "trash" && state.places.some((p) => p.id === place && (p.type === "usb" || p.type === "ftp")) ? `<div id="bk-extra"></div>` : ""}`;
  $$("[data-v]", main).forEach((b) => b.onclick = () => { mg.view = +b.dataset.v; backupsTab(); });
  $$("[data-goview]", main).forEach((b) => b.onclick = () => { mg.view = +b.dataset.goview; backupsTab(); });
  $("#mg-compare").onclick = mgCompareAll;
  // Comme R2 sur la télé : les jeux cochés, sinon tous ceux qui ont des doublons proposés → aperçu → corbeille.
  $("#mg-clean").onclick = () => {
    const picked = rows.filter((g) => mg.gsel.has(mgGk(g)));
    const list = picked.length ? picked : rows.filter((g) => (g.remove || []).length && mgChosen(g, place).size);
    if (list.length) mgPreview(list, place); else toast(T("Aucun doublon sûr pour l'instant."));
  };
  $$(".mg-game", main).forEach((r) => r.onclick = (e) => { if (e.target.closest(".ck, .more")) return; mg.back = ""; mg.game = r.dataset.g; window.scrollTo(0, 0); backupsTab(); });
  $$(".mg-game .ck", main).forEach((i) => i.onchange = () => { i.checked ? mg.gsel.add(i.dataset.c) : mg.gsel.delete(i.dataset.c); backupsTab(); });
  const all = $("#mg-all");
  setAllBox(all, shownOn, rows.length);
  all.onchange = () => { for (const g of rows) all.checked ? mg.gsel.add(mgGk(g)) : mg.gsel.delete(mgGk(g)); backupsTab(); };
  $$("[data-m]", main).forEach((b) => b.onclick = (e) => { e.stopPropagation(); mgRowMenu(b, mgGame(b.dataset.m)); });
  // Barre d'actions : Supprimer… (aperçu → corbeille) · Ignorer… · Protéger · Annuler.
  const prot = picked.flatMap((g) => g.copies.filter((c) => mgIn(c, place) && !c.other_account && !c.on_peer));
  const allProt = prot.length && prot.every((c) => mg.prot.has(mgCk(c)));
  selBar(picked.length, [
    { label: T("Supprimer…"), cls: "danger", run: () => mgPreview(picked, place) },
    { label: T("Ignorer…"), run: () => ignoreDialog(picked.map((g) => mgIgnoreTarget(g)).filter(Boolean), picked.length === 1 ? picked[0].copies.length : -1, () => { mg.gsel.clear(); mgLoad(); }) },
    ...(prot.length ? [{ label: allProt ? T("Retirer la protection") : T("Protéger"), run: () => mgProtect(prot, !allProt) }] : []),
  ], () => { mg.gsel.clear(); mg.csel.clear(); backupsTab(); }, nc ? T("%d copie(s) · libère %s", nc, size(bytes)) : "");
  if ($("#bk-extra")) mgDiskExtras($("#bk-extra"), place);
}

// Menu « ⋯ » d'un jeu : agir sur lui seul.
function mgRowMenu(anchor, g) {
  if (!g) return;
  const own = g.copies.filter((c) => !c.other_account && !c.on_peer && mgIn(c, mg.place));
  const allProt = own.length && own.every((c) => mg.prot.has(mgCk(c)));
  const k = mgGk(g);
  openMenu(anchor, [
    { label: T("Détails"), run: () => { mg.back = ""; mg.game = k; window.scrollTo(0, 0); backupsTab(); } },
    ...(g.on_peer_only ? [] : [{ label: T("Supprimer…"), cls: "bad", run: () => {
      if (mgChosen(g, mg.place).size) return mgPreview([g], mg.place);
      mg.back = ""; mg.game = k; backupsTab(); toast(T("Cochez les versions à supprimer"));
    } }]),
    { label: g.full ? T("Ne plus sauvegarder…") : T("Ignorer…"), run: () => mgIgnore(g) },
    ...(hasConsoleSave(g.title_id) ? [{ label: T("Supprimer la partie de la console…"), cls: "bad", run: () => deleteConsoleSave(anchor, g.title_id, g.title, () => mgLoad()) }] : []),
    ...(own.length ? [{ label: allProt ? T("Retirer la protection") : T("Protéger"), run: () => mgProtect(own, !allProt) }] : []),
  ]);
}

async function mgCompareAll() {
  toast(T("Comparaison de toutes les copies…"));
  try {
    const r = await post("/backups/compare-all", { account: mg.account });
    let msg = r.queued ? T("Comparaison lancée : %d jeu(x)", r.queued) : T("Tout est déjà comparé");
    if (r.skipped_running > 0) msg += " · " + T("le jeu en cours est laissé de côté");
    toast(msg);
    refresh(true);
  } catch (e) { toast(e.message); }
}

// Protéger (ou non) des copies : la rotation et la suppression les laissent.
async function mgProtect(copies, on) {
  let ok = 0, err = "";
  for (const c of copies) {
    try { await post("/backups/protect", { place: c.place, bundle: c.bundle, on }); on ? mg.prot.add(mgCk(c)) : mg.prot.delete(mgCk(c)); ok++; }
    catch (e) { err = e.message; }
  }
  if (ok) toast(on ? T("Protégée : la rotation et la suppression la laisseront") : T("Protection retirée"));
  if (err) toast(err);
  if (state.tab === "backups") backupsTab();
}

// Toutes les versions d'un jeu : console d'abord, puis ses copies du plus récent au plus ancien.
function mgGameView(main) {
  const g = mgGame(mg.game), k = mgGk(g);
  const ch = mgChosen(g);
  const mine = g.copies.filter((c) => !c.other_account && !c.on_peer);
  const bytes = g.copies.filter((c) => ch.has(mgCk(c))).reduce((s, c) => s + (mgBytes(c)), 0);
  const comparing = mg.taskGame === k;
  const [banner, tone] = comparing ? [SPIN + esc(T("Comparaison du contenu… (lecture seule, sur des copies)")), "info"]
    : ch.size && g.full ? [esc(T("%d copie(s) cochée(s) sur %d — libère %s", ch.size, mine.length, size(bytes))), "ok"]
    : g.full ? [esc(T("Sauvegardes complètes : cochez celles à mettre à la corbeille")), "muted"]
    : ch.size && !mg.csel.has(k) ? [esc(T("Proposition : garder %d copie(s), en supprimer %d — libère %s", mine.length - ch.size, ch.size, size(bytes))), "ok"]
    : ch.size ? [esc(T("%d copie(s) cochée(s) sur %d — libère %s", ch.size, mine.length, size(bytes))), "ok"]
    : !g.compared ? [esc(T("Pas encore comparées : lancez la comparaison")), "warn"] : [esc(T("Rien à nettoyer")), "muted"];
  const order = (list) => list.map((c) => [c, g.copies.indexOf(c)]).sort((a, b) => b[0].created_unix - a[0].created_unix);
  const own = order(mine), others = order(g.copies.filter((c) => c.other_account && !c.on_peer)), onPeer = order(g.copies.filter((c) => c.on_peer));
  const cg = g.console && g.console.group;
  const selectable = mine.filter((c) => !mg.prot.has(mgCk(c)) && !roPlace(c.place));
  const canRestore = (c) => !c.on_peer && c.complete !== false && (g.full || (g.account || "").startsWith("ps5:"));
  const copyRow = ([c, i]) => {
    const ck = mgCk(c), prot = mg.prot.has(ck), other = !!c.other_account || !!c.on_peer;
    const [tag, t] = c.on_peer ? [T("Sur %s", c.peer_name), "muted"] : other ? [T("Autre compte"), "muted"] : c.needs_resign ? [T("Autre identifiant"), "info"] : c.complete === false ? [T("Incomplète"), "bad"] : c.content_bytes === 0 ? [T("Vide"), "bad"] : g.full ? ["", ""] : !c.group || c.same_bytes ? [T("À comparer"), "warn"]
      : (g.keep || []).includes(i) ? [T("Gardée"), "ok"] : (g.remove || []).includes(i) ? [T("Doublon"), "warn"] : ["", ""];
    const bits = [];
    if (g.full) bits.push((c.parts || []).map((p) => PARTS[p] || p).join(", "));
    else if (c.progress_text) bits.push(c.progress_text);
    const pt = mgPlay(c.play_time_seconds || 0);
    if (pt && !bits.join(" ").includes(":")) bits.push(T("%s de jeu", pt));
    const written = c.game_saved_at || c.save_mtime;
    if (written) bits.push(T("partie enregistrée le %s", mgDate(written)));
    if (mgBytes(c)) bits.push(size(mgBytes(c)));
    if (prot) bits.push(T("protégée"));
    if (c.legacy) bits.push(T("ancienne (script)"));
    if (c.settings_only) bits.unshift(T("réglages seulement, pas de partie"));
    if (c.progress_auto && bits.length) bits.push(T("détecté automatiquement"));
    if (c.needs_resign) bits.push(T("identifiant %s, absent de cette console", c.owner_name || String(c.owner || "").slice(0, 6).toUpperCase() + "…"));  // à qui elle est
    let l2 = bits.filter(Boolean).join(" · ");
    if (!l2) l2 = c.on_peer && !c.group ? T("pas encore lue par %s", c.peer_name) : c.same_bytes ? T("identique octet pour octet à une autre copie ; avancée pas encore lue") : c.group ? T("contenu lu, le jeu n'écrit pas son avancée") : T("contenu non comparé");
    const letter = c.group ? (c.group === cg ? T("contenu %s (= console)", cg) : T("contenu %s", c.group)) : "";
    return `<div class="row mg-copy ${ch.has(ck) ? "on" : ""}">${ckHtml(`data-k="${esc(ck)}" ${other || prot ? "disabled" : ""}`, ch.has(ck))}
      <div class="grow"><div class="t">${esc(c.place_label)} · ${esc(mgDate(c.created_unix))}</div><div class="d ${prot ? "warn" : ""}">${esc(l2)}</div></div>
      <span class="hint">${esc(letter)}</span>${tag ? `<span class="tag ${t}">${esc(tag)}</span>` : "<span></span>"}
      <span class="vbtns"><button class="secondary small" data-r="${esc(ck)}" ${canRestore(c) ? "" : "disabled"}>${c.needs_resign ? T("Reprendre sur mon compte…") : T("Restaurer…")}</button>${moreBtn(`data-vm="${esc(ck)}"`)}</span></div>`;
  };
  const con = g.console;
  main.innerHTML = `<div class="eyebrow tr-crumb"><button class="link" id="mg-back">‹ ${T("Retour")}</button> ${T("SAUVEGARDES")} · ${T("TOUTES LES VERSIONS")}</div>
    <div class="acc-head mg-title">${mgCover(g)}<div><h2>${esc(g.title)}</h2>${spreadLine(g) ? `<span class="hint ok">${esc(spreadLine(g))}</span>` : `<span class="hint">${esc(mgPlaces(g))}</span>`}</div></div>
    <div class="mg-banner ${tone}">${banner}</div>
    <div class="actions"><button class="secondary" id="mg-cmp" ${comparing || g.full ? "disabled" : ""}>${T("Comparer le contenu")}</button>
      <button class="secondary" id="mg-ign1">${g.full ? T("Ne plus sauvegarder…") : T("Ignorer ce jeu…")}</button></div>
    <div class="list-head">${allBoxHtml("mg-all1", ch.size, selectable.length)}<span class="hint">${esc(T("%d version(s)", g.copies.length))} · ${T("du plus récent au plus ancien")}</span></div>
    <div class="list">
      ${con ? `<div class="row mg-copy console"><span></span><div class="grow"><div class="t">${T("Console")} · ${size(con.bytes || 0)}${con.mtime ? " · " + esc(mgDate(con.mtime)) : ""}</div>
        <div class="d">${esc((con.progress && con.progress.text) || T("La sauvegarde du jeu sur la PS5 (jamais touchée)"))}</div></div><span class="hint">${cg ? esc(T("contenu %s", cg)) : ""}</span><span></span><span></span></div>` : ""}
      ${own.map(copyRow).join("")}
      ${onPeer.length ? `<h3 class="mg-other">${esc(T("Rangées sur %s : récupérables ici (menu ⋯)", onPeer[0][0].peer_name))}</h3>${onPeer.map(copyRow).join("")}` : ""}
      ${others.length ? `<h3 class="mg-other">${esc(T("Un autre compte (%s…) : gardées telles quelles", String(others[0][0].other_account).slice(0, 8)))}</h3>${others.map(copyRow).join("")}` : ""}
    </div>`;
  const setSel = (set) => { mg.csel.set(k, set); set.size ? mg.gsel.add(k) : mg.gsel.delete(k); backupsTab(); };
  $("#mg-back").onclick = () => { const back = mg.back; mg.game = ""; mg.back = ""; if (back) setTab(back); else { window.scrollTo(0, 0); backupsTab(); } };
  $("#mg-cmp").onclick = () => mgCompare(g);
  $("#mg-ign1").onclick = () => mgIgnore(g);
  const all = $("#mg-all1");
  setAllBox(all, ch.size, selectable.length);
  all.onchange = () => setSel(all.checked ? new Set(selectable.map(mgCk)) : new Set());
  $$(".mg-copy .ck", main).forEach((i) => i.onchange = () => { const set = mgChosen(g); i.checked ? set.add(i.dataset.k) : set.delete(i.dataset.k); setSel(set); });
  const copyOf = (ck) => g.copies.find((c) => mgCk(c) === ck);
  $$("[data-r]", main).forEach((b) => b.onclick = () => mgRestore(g, copyOf(b.dataset.r)));
  $$("[data-vm]", main).forEach((b) => b.onclick = (e) => {
    e.stopPropagation();
    const c = copyOf(b.dataset.vm), ck = b.dataset.vm, prot = mg.prot.has(ck);
    if (c.on_peer) {  // rangée sur l'autre PS5 : la récupérer ici ; rien ne s'efface d'ici (comme la télé)
      openMenu(b, [
        ...(!g.full && c.complete !== false ? [{ label: T("Récupérer ici (restaurer sur un compte de cette console)…"), run: () => pickAccount(b, "", T("cette console"), async (uid, who) => {
          try { await post("/link/pull-save", { peer: c.peer, place: c.place, bundle: c.bundle, title_id: g.title_id, user_id: uid }); toast(T("« %s » arrive de %s : restaurée sur %s", g.title, c.peer_name, who)); } catch (e) { toast(e.message); }
        }) }] : []),
        { label: (c.place || "").includes(":remote:") ? T("Cette copie est sur un NAS que cette console n'a pas : ajoutez-le dans Appareils pour la gérer d'ici, ou faites-le depuis %s.", c.peer_name)
          : T("Cette copie est rangée sur %s : pour la supprimer ou la protéger, faites-le depuis cette console.", c.peer_name), run: () => {} },
      ]);
      return;
    }
    openMenu(b, [
      ...(canRestore(c) ? [{ label: c.needs_resign ? T("Reprendre sur mon compte…") : T("Restaurer…"), run: () => mgRestore(g, c) }] : []),
      // Envoyer à une autre PS5 jumelée (restaurée là-bas, re-signée pour son compte)
      ...(!g.full && c.complete !== false ? (state.linkPeers || []).filter((p) => p.paired).map((p) => ({ label: T("Envoyer à %s…", p.name || p.host), run: async () => {
        try { await post("/link/send-save", { instance: p.instance, place: c.place, bundle: c.bundle, title_id: g.title_id }); toast(T("Envoi vers %s en cours : la sauvegarde y sera restaurée", p.name || p.host)); } catch (e) { toast(e.message); }
      } })) : []),
      // Installer ce jeu sur cette console depuis la copie : image sur le disque choisi, ShadowMountPlus, sauvegarde re-signée.
      ...(g.full && c.complete !== false && (c.parts || []).includes("app") ? state.places.filter((p) => p.type === "internal" || p.type === "usb").map((p) => ({ label: T("Installer sur %s · %s libres", p.type === "internal" ? T("Interne") : p.label, size(p.free)), run: async () => (await systemOk("/backups/install-check", { place: c.place, bundle: c.bundle }, g.title)) && chooseFormat(b, async (format) => {
        pickAccount(b, "", T("cette console"), async (uid) => {
        try { await post("/backups/install", { place: c.place, bundle: c.bundle, target: p.id, format, user_id: uid }); toast(T("Installation de « %s » en cours (suivie dans Tâches) ; le jeu apparaît sur l'accueil une fois l'application fermée", g.title)); } catch (e) { toast(e.message); }
        });
      }, T("Format de « %s » sur %s", g.title, p.type === "internal" ? T("Interne") : p.label)) })) : []),
      ...(c.complete !== false ? [{ label: T("Partager cette copie…"), run: () => shareCopyMenu(b, g, c) }] : []),
      ...(roPlace(c.place) ? [] : [{ label: prot ? T("Retirer la protection") : T("Protéger"), run: () => mgProtect([c], !prot) }]),  // partage reçu : lecture seule
      ...(!prot && !c.other_account && !roPlace(c.place) ? [{ label: T("Supprimer…"), cls: "bad", run: () => { mg.csel.set(k, new Set([ck])); mg.gsel.add(k); mgPreview([g]); } }] : []),
    ]);
  });
  const picked = g.copies.filter((c) => ch.has(mgCk(c)));
  const allProt = picked.length && picked.every((c) => mg.prot.has(mgCk(c)));
  selBar(ch.size, [
    { label: T("Supprimer…"), cls: "danger", run: () => mgPreview([g]) },
    { label: allProt ? T("Retirer la protection") : T("Protéger"), run: () => mgProtect(picked, !allProt) },
  ], () => setSel(new Set()), T("%d copie(s) · libère %s", ch.size, size(bytes)));
}

// Consoles jumelées (pour « Envoyer à … ») : lues une fois par passage dans Backups.
async function mgLoadPeers() { try { state.linkPeers = await api("/link/peers"); } catch (e) { state.linkPeers = []; } }
// Restaurer une version : une sauvegarde PS5 passe par la restauration de sauvegarde (vérifiée, sur un compte) ;
// un backup complet revient à sa place d'origine (rien n'est écrasé).
async function mgRestore(g, c) {
  if (!c || c.complete === false) { toast(T("Aucune version complète à restaurer.")); return; }
  if (!g.full && (g.account || "").startsWith("ps5:")) {
    const targets = ((state.overview && state.overview.accounts) || []).filter((a) => a.on_console);
    if (!targets.length) { toast(T("Aucun compte PS5 sur cette console")); return; }
    return restoreDialog({ title: g.title, title_id: g.title_id, backups: [c] }, targets);
  }
  const parts = (c.parts || []).filter((p) => p !== "save");
  if (!parts.length) { toast(T("Ce sont des saves : remettez-les sur un compte depuis l'onglet Parties.")); return; }
  const ok = await ask(T("Restaurer « %s »", g.title), `<p class="hint">${esc(c.place_label)} · ${esc(mgDate(c.created_unix))}</p>
    <p class="hint">${T("Jeu, mises à jour, applis : chaque fichier revient où il était, inscription comprise. Rien n'est écrasé.")}</p>`, T("Restaurer"));
  if (!ok) return;
  try { await post("/tasks", { type: "restore", place: c.place, bundle: c.bundle, parts, target: "", overwrite: false }); toast(T("Restauration ajoutée aux tâches")); refresh(true); }
  catch (e) { toast(e.message); }
}

// Comparer le contenu d'un jeu : console + toutes ses copies complètes (lecture seule, sur des copies).
async function mgCompare(g) {
  if (!g.title_id) { toast(T("Jeu sans identifiant : comparaison impossible")); return; }
  if (!g.account.includes(":")) { toast(T("Aucun compte PS5 sur cette console")); return; }
  const sources = (g.console ? [{ kind: "console" }] : []).concat(g.copies.filter((c) => c.complete !== false && !c.on_peer).map((c) => ({ place: c.place, bundle: c.bundle })));  // l'autre PS5 compare ses copies elle-même
  try {
    const r = await post("/tasks", { type: "inspect_save", title_id: g.title_id, user_id: g.account.split(":")[1], sources });
    mg.task = r.id; mg.taskGame = mgGk(g);
    toast(T("Comparaison du contenu… (lecture seule, sur des copies)"));
    refresh(true);
  } catch (e) { toast(e.message || T("Comparaison refusée")); }
  backupsTab();
}

// Comparer et trier (comme la télé) : toutes les copies d'un jeu côte à côte, jugées par leur contenu, jamais par la date.
// Traduits à l'affichage (manage.js est chargé avant app.js, où N_ est défini).
const cmpFilters = () => [T("Tout"), T("Sauvegardes"), T("Jeux et images"), T("Doublons"), T("Pas encore comparé"), T("Différents"), T("À vérifier"), T("Plusieurs endroits")];
function cmpGroups(g) { const s = new Set(); if (g.console && g.console.group) s.add(g.console.group); for (const c of g.copies) if (c.group) s.add(c.group); return s.size; }
function cmpPlaces(g) { const s = new Set(); if (g.console) s.add("console"); for (const c of g.copies) s.add(c.on_peer ? "peer:" + c.peer_name : c.place); return s.size; }
function cmpDay(t) { if (!t) return ""; const d = new Date(t * 1000); return String(d.getDate()).padStart(2, "0") + "/" + String(d.getMonth() + 1).padStart(2, "0"); }
function mgCompareView(main) {
  const games = mg.sum.games, f = mg.cfilter || 0;
  let multi = 0, dups = 0, newer = 0;
  for (const g of games) { multi += cmpPlaces(g) > 1; dups += g.remove.length; newer += g.status === "backup_newer"; }
  const rows = games.filter((g) => {
    const full = !!g.full;
    const ok = f === 1 ? !full : f === 2 ? full : f === 3 ? g.remove.length > 0 : f === 4 ? !full && !g.compared : f === 5 ? !full && g.compared && cmpGroups(g) > 1 : f === 6 ? g.copies.some(mgSuspect)
      : f === 7 ? !full && (() => { const by = spreadOf(g); return g.copies.some((c) => spreadCopy(c, by) && !mg.prot.has(mgCk(c))); })() : true;
    return ok && (cmpPlaces(g) > 1 || f !== 0);
  });
  const card = (g) => {
    const full = !!g.full, groups = cmpGroups(g);
    const [verdict, cls] = full ? [T("%d copie(s) complète(s)", g.copies.length), ""] : !g.compared ? [T("À comparer"), "warn"] : g.status === "backup_newer" ? [T("Une copie plus récente"), "warn"]
      : groups <= 1 ? [T("Même contenu partout"), "ok"] : g.remove.length ? [T("%d doublon(s)", g.remove.length), "ok"] : [T("Différentes"), "bad"];
    const cg = (g.console && g.console.group) || "", cm = (g.console && g.console.mtime) || 0;
    const cplayed = (g.console && g.console.progress && g.console.progress.mtime) || cm;
    const removing = new Set(g.remove), adv = g.most_advanced ? g.most_advanced.index : -1;
    const tiles = [];
    if (g.console) tiles.push({ tag: T("Partie active"), tone: "ok", same: true, where: T("Console"), line: T("partie du %s", cmpDay(cplayed)) + (cg ? T(" · contenu %s", cg) : "") });
    g.copies.forEach((c, i) => {
      const grp = c.group || "", saved = c.game_saved_at || 0;
      const t = { where: c.on_peer ? c.peer_name + " · " + c.place_label : c.place_label,
        line: (saved ? T("partie du %s", cmpDay(saved)) : T("copiée le %s", cmpDay(c.created_unix))) + (grp ? T(" · contenu %s", grp) : "") };
      if (c.on_peer && (!grp || full)) Object.assign(t, { tag: T("Sur %s", c.peer_name), tone: "", where: c.place_label });
      else if (full) Object.assign(t, { tag: T("Copie complète"), tone: "" });
      else if (mgSuspect(c)) Object.assign(t, { tag: T("À vérifier"), tone: "bad" });
      else if (c.restored_copy && !grp) Object.assign(t, { tag: T("Partie restaurée"), tone: "ok" });
      else if (!grp || c.same_bytes) Object.assign(t, { tag: T("Pas comparée"), tone: "warn" });
      else if (removing.has(i)) Object.assign(t, { tag: T("Doublon"), tone: "ok" });
      else if (cg && grp === cg) Object.assign(t, { tag: T("Identique"), tone: "ok", same: true });
      else if (adv === i) Object.assign(t, { tag: T("Plus avancée"), tone: "accent" });
      else if (cm && saved > cm + 60) Object.assign(t, { tag: T("Plus récente"), tone: "warn" });
      else Object.assign(t, { tag: T("Plus ancienne"), tone: "" });
      tiles.push(t);
    });
    const note = spreadLine(g) || (full ? T("Copie complète") : T("Sauvegarde")) + (g.console ? " · " + T("partie active sur cette console") : "");
    return `<div class="cmp-game" data-cmp-gk="${esc(mgGk(g))}"><div class="cmp-head">${mgCover(g)}<div class="grow"><b>${esc(g.title)}</b><div class="hint">${esc(note)}</div></div><span class="cmp-verdict ${cls}">${esc(verdict)}</span></div>
      <div class="cmp-copies">${tiles.map((t) => `<div class="cmp-copy ${t.same ? "same" : ""}"><span class="cmp-tag ${t.tone}">${esc(t.tag)}</span><b>${esc(t.where)}</b><small>${esc(t.line)}</small></div>`).join("")}</div>
      <div class="actions">${f === 7 ? `<button data-cmp-tidy="${esc(mgGk(g))}">${T("Faire le tri")}</button>` : ""}<button class="secondary" data-cmp-open="${esc(mgGk(g))}">${T("Détails")}</button>${full ? "" : `<button class="secondary" data-cmp-run="${esc(mgGk(g))}">${T("Comparer ce jeu")}</button>`}</div></div>`;
  };
  main.innerHTML = `<div class="cmp-sum">
      <div class="mg-stat"><b>${multi}</b><span class="hint">${T("jeux avec des copies à plusieurs endroits")}</span></div>
      <div class="mg-stat"><b>${dups}</b><span class="hint">${T("copies identiques (doublons)")}</span></div>
      <div class="mg-stat"><b class="${newer ? "warn" : ""}">${newer}</b><span class="hint">${T("copie(s) plus récente(s) ailleurs")}</span></div>
      <div class="mg-stat"><b>${esc(size(Math.max(0, mg.sum.freed_bytes || 0)))}</b><span class="hint">${T("récupérables en gardant une copie par contenu")}</span></div></div>
    <div class="cmp-filters">${cmpFilters().map((x, i) => `<button class="${i === f ? "on" : ""}" data-cf="${i}">${esc(x)}</button>`).join("")}<button class="secondary small" id="cmp-all">${T("Tout comparer")}</button></div>
    ${f === 7 && rows.length ? `<div class="actions"><button class="secondary" id="cmp-keep">${T("Garder seulement sur…")}</button></div>` : ""}
    ${rows.length ? rows.map(card).join("") : `<div class="empty"><p class="hint">${f === 0 ? T("Aucun jeu n'a de copies à plusieurs endroits.") : T("Rien dans cette vue.")}</p></div>`}`;
  if (mg.cmpFocus) {  // ouvert depuis « Comparer » d'un jeu : on va sur sa carte
    const el = $$("[data-cmp-gk]", main).find((x) => x.dataset.cmpGk === mg.cmpFocus);
    mg.cmpFocus = "";
    if (el) { el.classList.add("focus"); setTimeout(() => el.scrollIntoView({ block: "center" }), 0); }
  }
  $$("[data-cf]", main).forEach((b) => b.onclick = () => { mg.cfilter = +b.dataset.cf; backupsTab(); });
  $$("[data-cmp-open]", main).forEach((b) => b.onclick = () => { mg.back = ""; mg.game = b.dataset.cmpOpen; window.scrollTo(0, 0); backupsTab(); });
  if ($("#cmp-all", main)) $("#cmp-all", main).onclick = () => mgCompareAll();
  $$("[data-cmp-run]", main).forEach((b) => b.onclick = () => { const g = games.find((x) => mgGk(x) === b.dataset.cmpRun); if (g) mgCompare(g); });
  $$("[data-cmp-tidy]", main).forEach((b) => b.onclick = () => { const g = games.find((x) => mgGk(x) === b.dataset.cmpTidy); if (g) mgTidy(g); });
  if ($("#cmp-keep", main)) $("#cmp-keep", main).onclick = () => mgTidy(null, rows);
}

// Aperçu avant la corbeille, puis une copie après l'autre (le NAS répond en FTP). « place » : seulement cet endroit.
function mgPreview(list, place = "") {
  const games = [], places = new Map(), copies = [];
  for (const g of list) {
    const ch = mgChosen(g, place);
    if (!ch.size) continue;
    const pg = { title: g.title, keep: 0, remove: 0, bytes: 0 };
    for (const c of g.copies) {
      if (c.other_account || c.on_peer) continue;
      if (!ch.has(mgCk(c))) { pg.keep++; continue; }
      pg.remove++; pg.bytes += mgBytes(c);
      copies.push(c);
      const p = places.get(c.place) || { label: c.place_label, count: 0, bytes: 0 };
      p.count++; p.bytes += mgBytes(c);
      places.set(c.place, p);
    }
    games.push(pg);
  }
  if (!copies.length) { toast(T("Rien à nettoyer dans la sélection")); return; }
  const total = [...places.values()].reduce((s, p) => s + p.bytes, 0);
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog wide"><h2>${esc(games.length === 1 ? T("Nettoyer « %s »", games[0].title) : T("Nettoyer %d jeux", games.length))}</h2>
    <div class="mg-pv">${games.map((p) => `<div><b>${esc(p.title)}</b><span class="hint">${esc(T("garde %d, supprime %d — libère %s", p.keep, p.remove, size(p.bytes)))}</span></div>`).join("")}</div>
    <p class="hint">${T("Par emplacement")}</p>
    <div class="mg-pv">${[...places.values()].map((p) => `<div><span>${esc(p.label)}</span><span>${esc(T("%d copie(s) · %s", p.count, size(p.bytes)))}</span></div>`).join("")}</div>
    <p class="hint">${T("Les copies vont dans « PBC Backup/Corbeille » du même disque ou NAS et peuvent être remises pendant 30 jours. Les copies protégées et celles d'un autre compte ne sont jamais touchées.")}</p>
    <div id="mg-pv-status" class="hint"></div>
    <div class="actions"><button class="danger" id="mg-go">${esc(T("Mettre à la corbeille (%s)", size(total)))}</button><button class="secondary" id="mg-cancel">${T("Annuler")}</button></div></div>`;
  document.body.appendChild(box);
  let busy = false;
  const close = () => { if (!busy) box.remove(); };
  $("#mg-cancel", box).onclick = () => { close(); mgTidyUndo(); toast(T("Rien n'a été supprimé")); };
  box.onclick = (e) => { if (e.target === box && !busy) { close(); mgTidyUndo(); toast(T("Rien n'a été supprimé")); } };
  $("#mg-go", box).onclick = async () => {
    // Dernière étape : bouton maintenu 5 s (comme la télé et la sauvegarde de la console).
    const nas = copies.some((c) => (c.place || "").startsWith("remote:"));
    const note = `<p>${esc(T("%d copie(s), %s.", copies.length, size(total)))} ${esc(T("Ce sont des sauvegardes (des copies), pas la partie active sur la console. Elles vont à la corbeille de leur disque pendant 30 jours."))}${nas ? " " + esc(T("Une partie est sur le NAS : elle va dans la corbeille du NAS.")) : ""}</p>`;
    if (!(await holdAsk(T("Mettre ces copies à la corbeille ?"), note, T("Maintenir 5 s pour supprimer")))) return;
    busy = true;
    $$("button", box).forEach((b) => b.disabled = true);
    let done = 0, skipped = 0, failed = 0, err = "";
    for (const c of copies) {
      $("#mg-pv-status", box).innerHTML = SPIN + esc(T("Mise à la corbeille… %d / %d", done + skipped + failed, copies.length));
      try { await post("/backups/delete", { place: c.place, bundle: c.bundle, confirm: true }); done++; }
      catch (e) { if (/protég/.test(e.message)) skipped++; else { failed++; err = e.message; } }
    }
    busy = false; box.remove();
    let msg = T("%d copie(s) mise(s) à la corbeille (30 jours)", done);
    if (skipped) msg += " · " + T("%d protégée(s), gardée(s)", skipped);
    if (failed) msg += " · " + T("%d refusée(s) : %s", failed, err);
    toast(msg);
    mg.gsel.clear(); mg.csel.clear(); mg.trash = {}; mg.disk = {}; state.overview = null;
    mgLoad();
  };
}

// Ex-« Par disque » : sur un disque ou un NAS, les copies de son index que le résumé ne range sous aucun jeu
// (autres consoles, émulateurs…), avec Restaurer dans un dossier neuf et Corbeille. Liste gardée ; « Actualiser » relit.
async function mgDiskExtras(root, placeId, again = false) {
  const place = state.places.find((p) => p.id === placeId);
  if (!place) return;
  const known = new Set(mg.sum.games.flatMap((g) => g.copies.map((c) => c.bundle)));
  const draw = (idx) => {
    if (!root.isConnected) return;
    const list = (idx.backups || []).filter((b) => !known.has(b.bundle));
    const head = `<div class="shelf-head"><h2>${T("Autres copies sur cet endroit")} <span class="n">${idx.backups ? list.length : ""}</span></h2><button class="link" id="disk-refresh">${idx.loading ? SPIN : ""}${T("Actualiser")}</button></div>`;
    if (idx.loading && !idx.backups) { root.innerHTML = head + loading(place.type === "ftp" ? T("Lecture de « %s »…", place.label) + " " + T("Un NAS peut demander un moment : la liste sera gardée ensuite.") : T("Lecture de l'index…")); }
    else if (idx.error) root.innerHTML = head + `<div class="empty">${esc(idx.error)}</div>`;
    else if (!list.length) root.innerHTML = head + `<p class="hint">${T("Rien d'autre : toutes les copies de cet endroit sont rangées par jeu ci-dessus.")}</p>`;
    else {
      const low = (v) => (v || "").toLowerCase();
      const itemFor = (b) => state.items.find((i) => b.key && i.key === b.key) || (b.title_id && state.items.find((i) => i.title_id === b.title_id && i.has_cover))
        || state.items.find((i) => i.has_cover && low(i.title) === low(b.title))
        || { key: b.title_id ? (b.platform === "PS4" ? "tid:ps4:" : "tid:ps5:") + b.title_id : "", title: b.title || b.bundle, kind: b.kind || "game_ps5", platform: b.platform || "PS5", has_cover: !!b.title_id, apps: [] };
      root.innerHTML = head + `<div class="list">${list.map((b) => `<div class="row">
        <div class="thumb">${coverHtml(itemFor(b), true)}</div>
        <div><div class="t">${esc(b.title || b.bundle)}${b.complete === false ? `<span class="state failed">${T("Incomplète")}</span>` : ""}</div>
          <div class="d">${esc(mgDate(new Date(b.created).getTime() / 1000))} · ${b.bytes ? size(b.bytes) : T("taille ?")} · ${(b.parts || []).map((p) => PARTS[p] || p).join(", ")}</div></div>
        <div class="btns"><button data-bundle="${esc(b.bundle)}" ${b.complete !== false && place.type === "usb" ? "" : "disabled"}>${T("Restaurer dans un dossier neuf")}</button>${place.readonly ? "" : `<button class="bad" data-trash="${esc(b.bundle)}" data-name="${esc(b.title || b.bundle)}">${T("Corbeille…")}</button>`}</div></div>`).join("")}</div>`;
    }
    $("#disk-refresh", root).onclick = () => mgDiskExtras(root, placeId, true);
    $$("button[data-trash]", root).forEach((b) => b.onclick = async () => {
      const ok = await ask(T("Mettre cette copie à la corbeille ?"), `<p>${esc(b.dataset.name)}</p><p class="hint">${T("Elle va dans « PBC Backup/Corbeille » du même disque ou NAS et peut être remise pendant 30 jours.")}</p>`, T("Mettre à la corbeille"), T("Annuler"));
      if (!ok) { toast(T("Rien n'a été supprimé")); return; }
      b.disabled = true; b.innerHTML = SPIN;
      try { await post("/backups/delete", { place: placeId, bundle: b.dataset.trash, confirm: true }); toast(T("Copie mise à la corbeille (30 jours)")); }
      catch (e) { toast(T("Suppression refusée : %s", e.message)); }
      mgDiskExtras(root, placeId, true);
    });
    $$("button[data-bundle]", root).forEach((b) => b.onclick = async () => {
      const target = `${place.mount}/PBC Restaurations/${b.dataset.bundle.replace(/\.pbck$/, "")}`;
      // Le bouton propose, il ne lance pas d'un coup (charte §1), comme la télé.
      if (!await ask(T("Restaurer cette sauvegarde complète ?"), `<p class="hint">${esc(T("Les fichiers sont remis dans « PBC Restaurations », sur le même disque. Rien n'est écrasé."))}</p>`, T("Restaurer"))) return;
      try { await post("/tasks", { type: "restore", place: placeId, bundle: b.dataset.bundle, target }); toast(T("Restauration ajoutée aux tâches")); }
      catch (e) { toast(e.message); }
    });
  };
  // Disque USB : relu au bout d'une minute (la liste affichée reste) ; le NAS seulement sur « Actualiser ».
  const cached = mg.disk[placeId];
  if (cached && !again && (cached.loading || !(place.type === "usb" && Date.now() - (cached.at || 0) > 60000))) { draw(cached); return; }
  const cur = mg.disk[placeId] = { ...(cached || {}), loading: true };
  draw(cur);
  let idx;
  try { idx = await api("/backups?place=" + encodeURIComponent(placeId)); } catch (e) { idx = { error: e.message || T("réponse illisible"), backups: [] }; }
  idx.at = Date.now();
  mg.disk[placeId] = idx;
  const r = $("#bk-extra");
  if (r && mg.place === placeId) { root = r; draw(idx); }
}

// Corbeille de chaque disque USB et du NAS, par jour ; « Remettre » remet la copie à sa place.
async function mgTrashView(root) {
  const places = (state.places || []).filter((p) => (p.type === "usb" || p.type === "ftp") && p.use !== "sync" && !p.same_backups_as && !p.readonly);
  const draw = () => {
    if (!root.isConnected) return;
    const withAny = places.filter((p) => Array.isArray(mg.trash[p.id]) && mg.trash[p.id].length);
    const overview = withAny.length > 1 ? `<p class="hint">${esc(T("Dans la corbeille : %s", withAny.map((p) => p.label + " " + mg.trash[p.id].length).join(" · ")))}</p>` : "";
    root.innerHTML = places.length ? overview + places.map((p) => {
      const list = mg.trash[p.id];
      if (list === undefined) return `<h3>${esc(p.label)}</h3>${loading(T("lecture…"))}`;
      if (list.error) return `<h3>${esc(p.label)}</h3><p class="bad">${esc(list.error)}</p>`;
      if (!list.length) return `<h3>${esc(p.label)} <span class="hint">· ${T("corbeille vide")}</span></h3>`;
      const days = [...new Set(list.map((t) => t.day))].sort().reverse();
      return `<h3>${esc(p.label)} <span class="hint">· ${esc(T("%d copie(s)", list.length))}</span> <button class="secondary small danger" data-empty="${esc(p.id)}" data-label="${esc(p.label)}" data-count="${list.length}">${T("Vider la corbeille")}</button></h3>` + days.map((d) => {
        const [y, m, dd] = d.split("-");
        return `<p class="hint">${esc(T("Mise à la corbeille le %s", dd && m ? `${dd}/${m}/${y}` : d))}</p><div class="list">${list.filter((t) => t.day === d).map((t) => {
          const plain = (s) => (s || "").toLowerCase().replace(/[^\p{L}\p{N}]/gu, ""), pb = plain(t.bundle);
          const g = (mg.sum.games || []).find((x) => (x.title_id && t.bundle.includes(x.title_id)) || (plain(x.title) && pb.includes(plain(x.title))));
          return `<div class="row"><div><div class="t">${esc(g ? g.title : t.bundle.split("/").pop())}</div><div class="d">${esc(t.bundle)}</div></div>
            <div class="btns"><button data-place="${esc(p.id)}" data-path="${esc(t.path)}">${T("Remettre")}</button><button class="secondary danger" data-del-place="${esc(p.id)}" data-del-path="${esc(t.path)}" data-del-name="${esc(t.bundle)}">${T("Supprimer définitivement…")}</button></div></div>`;
        }).join("")}</div>`;
      }).join("");
    }).join("") + `<p class="hint">${T("Les copies restent 30 jours dans « PBC Backup/Corbeille » de leur disque ou NAS.")}</p>` : `<div class="empty">${T("Aucun disque USB ni NAS.")}</div>`;
    // Supprimer pour de bon (une copie, ou toute la corbeille d'un endroit) : bouton maintenu, comme sur la télé.
    const purge = async (place, body, title, note, done) => {
      if (!(await holdAsk(title, `<p>${esc(note)}</p>`, T("Maintenir 5 s pour supprimer")))) return;
      try { await post("/trash/delete", Object.assign({ place, confirm: true }, body)); toast(done); } catch (e) { toast(e.message); }
      mg.trash[place] = undefined;
      load(places.find((p) => p.id === place));
    };
    $$("[data-del-path]", root).forEach((b) => b.onclick = () => purge(b.dataset.delPlace, { path: b.dataset.delPath }, T("Supprimer définitivement ?"),
      T("« %s » sera effacée pour de bon : elle ne pourra plus être remise.", b.dataset.delName), T("Copie supprimée pour de bon")));
    $$("[data-empty]", root).forEach((b) => b.onclick = () => purge(b.dataset.empty, { all: true }, T("Vider la corbeille de %s ?", b.dataset.label),
      T("%d copie(s) seront effacées pour de bon : elles ne pourront plus être remises.", +b.dataset.count), T("Corbeille vidée")));
    $$("[data-path]", root).forEach((b) => b.onclick = async () => {
      b.disabled = true; b.innerHTML = SPIN;
      try { await post("/trash/restore", { place: b.dataset.place, path: b.dataset.path }); toast(T("Copie remise à sa place")); state.overview = null; }
      catch (e) { toast(e.message); }
      delete mg.trash[b.dataset.place];
      load(places.find((p) => p.id === b.dataset.place));
      mgLoad();
    });
  };
  const load = async (p) => {
    if (!p || mg.trashLoading.has(p.id)) return;
    mg.trashLoading.add(p.id);
    try { mg.trash[p.id] = await api(`/trash?place=${encodeURIComponent(p.id)}`); }
    catch (e) { mg.trash[p.id] = { error: e.message || T("réponse illisible") }; }
    mg.trashLoading.delete(p.id);
    draw();
  };
  draw();
  for (const p of places) if (mg.trash[p.id] === undefined) load(p);
}

// Ignorer un jeu : il n'est plus sauvegardé ni affiché ; ses copies (tous comptes, disques et NAS) peuvent aller
// à la corbeille (30 jours). Peut prendre un moment avec le NAS : roue pendant l'attente.
const mgIgnoreTarget = (g) => (g.title_id ? { field: "title_id", value: g.title_id, title: g.title } : g.item ? { field: "key", value: g.item, title: g.title } : null);
function mgIgnore(g) {
  if (!g) return;
  const t = mgIgnoreTarget(g);
  if (t) return ignoreDialog([t], g.copies.length, () => mgLoad());
  toast(T("Jeu sans identifiant : impossible de l'ignorer"));
}

// Élément de la bibliothèque → cible de /ignore : jeu ou appli par son identifiant, ROM / homebrew par sa clé.
const ignoreTarget = (it) => (it.title_id ? { field: "title_id", value: it.title_id, title: it.title } : { field: "key", value: it.key, title: it.title });

// Ignorer un ou plusieurs éléments : plus sauvegardés ni proposés ; leurs copies (tous comptes, disques et NAS,
// sauvegardes et backups complets) peuvent aller à la corbeille (30 jours). Une demande après l'autre, roue pendant l'attente.
function ignoreDialog(targets, copies, done) {
  if (!targets.length) return;
  const n = targets.length;
  const text = n > 1 ? T("Ces %d éléments ne seront plus sauvegardés ni proposés. Leurs sauvegardes (parties et copies complètes) peuvent aller à la corbeille (30 jours) sur tous les disques et le NAS.", n)
    : copies >= 0 ? T("Il ne sera plus sauvegardé ni affiché. Ses %d copie(s) peuvent aller à la corbeille (30 jours) sur tous les disques et le NAS.", copies)
    : T("Il ne sera plus sauvegardé ni proposé. Ses sauvegardes (parties et copies complètes) peuvent aller à la corbeille (30 jours) sur tous les disques et le NAS.");
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog"><h2>${esc(n > 1 ? T("Ignorer %d éléments ?", n) : T("Ignorer « %s » ?", targets[0].title))}</h2>
    <p>${esc(text)}</p>
    <div id="mg-ign-status" class="hint"></div>
    <div class="actions mg-ign-actions"><button class="danger" data-t="1">${T("Ignorer et mettre ses copies à la corbeille")}</button>
      <button class="secondary" data-t="0">${T("Ignorer seulement")}</button><button class="secondary" data-t="">${T("Annuler")}</button></div></div>`;
  document.body.appendChild(box);
  let busy = false;
  const cancel = () => { if (busy) return; box.remove(); toast(T("Rien n'a été modifié")); };
  box.onclick = (e) => { if (e.target === box) cancel(); };
  $$("[data-t]", box).forEach((b) => b.onclick = async () => {
    if (b.dataset.t === "") return cancel();
    const trash = b.dataset.t === "1";
    busy = true;
    $$("button", box).forEach((x) => x.disabled = true);
    const count = (v) => (Array.isArray(v) ? v.length : +v || 0);
    let ok = 0, trashed = 0, refused = 0, err = "";
    for (const t of targets) {
      $("#mg-ign-status", box).innerHTML = SPIN + esc(T("Un moment : le NAS peut être long…")) + (n > 1 ? ` ${ok + (err ? 1 : 0)} / ${n}` : "");
      try { const r = await post("/ignore", { [t.field]: t.value, on: true, trash }); ok++; trashed += count(r.trashed); refused += count(r.refused); }
      catch (e) { err = e.message; }
    }
    let msg = n === 1 && ok === 1 ? (trash ? T("« %s » ignoré · %d copie(s) mise(s) à la corbeille", targets[0].title, trashed) : T("« %s » ignoré", targets[0].title))
      : ok ? (trash ? T("%d élément(s) ignoré(s) · %d copie(s) mise(s) à la corbeille", ok, trashed) : T("%d élément(s) ignoré(s)", ok)) : "";
    if (refused > 0) msg += (msg ? " · " : "") + T("%d copie(s) gardée(s) (protégée(s) ou refusée(s))", refused);
    if (err) msg += (msg ? " · " : "") + err;
    toast(msg);
    mg.game = ""; mg.trash = {}; state.overview = null;
    for (const t of targets) mg.gsel.delete("full|" + t.value);
    try { state.settings = await api("/settings"); } catch (e) {}
    busy = false; box.remove();
    if (done) done();
  });
}

// ---- Partager ma copie (3.1.43), comme la télé : lien temporaire de la console (.zip), BitTorrent ou MEGA ----
function shareCopyMenu(anchor, g, c) {
  const pl = state.places.find((p) => p.id === c.place) || {};
  const local = !String(c.place).startsWith("remote:") && !String(c.place).startsWith("peer");
  const where = `${copyKindText(g, c)} · ${c.place_label || placeLabel(c.place)} · ${mgDate(c.created_unix)}`;
  const req = async (kind, hours) => {
    try {
      const r = await post("/shares/out", { place: c.place, bundle: c.bundle, kind, hours, title: g.title });
      if (kind === "bt") { showNewBtLink(r.job, g.title); return; }
      shareShow({ ...r, title: g.title, where });
    } catch (e) { toast(e.message); }
  };
  openMenu(anchor, [
    { label: T("Lien de la console (.zip) · maison ou Internet…"), run: () => openMenu(anchor, [
      { label: T("Pendant 1 heure"), run: () => req("local", 1) },
      { label: T("Pendant 24 heures"), run: () => req("local", 24) },
      { label: T("Pendant 7 jours"), run: () => req("local", 24 * 7) },
    ], T("Le lien s'arrête seul à la fin ; vous pouvez l'arrêter avant dans Téléchargements › Mes liens.")) },
    ...(local && !c.on_peer ? [{ label: T("Lien BitTorrent (magnet) · sans copie"), run: () => req("bt", 0) }] : []),
    ...(pl.protocol === "mega" ? [{ label: T("Lien %s · du service", "MEGA"), run: () => req("mega", 0) }] : []),
    // OneDrive et Dropbox (comme la télé) : Google Drive refuse de partager un dossier avec l'accès limité de l'appli ;
    // pour lui, le lien temporaire de la console.
    ...(pl.protocol === "onedrive" ? [{ label: T("Lien %s · du service", "OneDrive"), run: () => req("service", 0) }] : []),
    ...(pl.protocol === "dropbox" ? [{ label: T("Lien %s · du service", "Dropbox"), run: () => req("service", 0) }] : []),
    { label: T("Mes liens…"), run: () => myShares(anchor) },
  ], `${where} · ${T("Le lien donne cette copie seulement, en lecture : rien n'est effacé ni modifié.")}`);
}

// Depuis la fiche : la copie à partager (la plus récente d'abord).
// Ce qu'est une copie, en clair (comme la télé) : la sauvegarde de partie de quel compte, ou une copie du jeu.
function copyKindText(g, c) {
  if (g.full || (c.parts || []).includes("app")) return T("Copie du jeu");
  const a = ((state.overview && state.overview.accounts) || []).find((x) => x.key === g.account);
  const who = c.owner_name || (a && a.name) || "";
  return who ? T("Sauvegarde de partie · %s", who) : T("Sauvegarde de partie");
}
// « Que partager ? » (comme la télé) : le jeu lui-même (là où il est : interne, USB…, lien BitTorrent, sans copie), une
// sauvegarde de partie, ou une copie du jeu (le jeu et sa partie ensemble) ; puis la liste, la plus récente d'abord.
async function shareFromSheet(anchor, it) {
  let sum;
  toast(T("Un instant : lecture des copies…"));
  try { sum = await api("/backups/summary"); } catch (e) { toast(e.message); return; }
  const saves = [], games = [];
  for (const g of sum.games || []) {
    if (!((it.title_id && g.title_id === it.title_id) || g.item === it.key)) continue;
    for (const c of g.copies || []) if (c.complete !== false) (copyKindText(g, c) === T("Copie du jeu") ? games : saves).push([g, c]);
  }
  const newest = (a, b) => b[1].created_unix - a[1].created_unix;
  saves.sort(newest); games.sort(newest);
  const pick = (list, what) => openMenu(anchor, list.slice(0, 12).map(([g, c]) => ({
    label: `${copyKindText(g, c)} · ${c.place_label || placeLabel(c.place)} · ${mgDate(c.created_unix, false)}${mgBytes(c) ? " · " + size(mgBytes(c)) : ""}`,
    run: () => shareCopyMenu(anchor, g, c) })));
  let appComps = (it.components || []).filter((c) => c.part === "app");  // le jeu lui-même : « app », sinon « game » (ROM)
  if (!appComps.length) appComps = (it.components || []).filter((c) => c.part === "game");
  const appBytes = appComps.reduce((n, c) => n + (c.bytes || 0), 0);
  const acts = [];
  let where = placeLabel(it.place);  // comme la fiche : le vrai disque des données (une image sur une clé USB…)
  if (appComps.length) { try { const img = (await loadImages()).get(it.key); if (img && img.disk && img.disk.label) where = T(img.disk.label); } catch (e) {} }
  if (appComps.length) acts.push({ label: T("Le jeu · %s · %s · lien BitTorrent (magnet), sans copie", where, size(appBytes)), run: async () => {
    if (!(await ask(T("Créer un lien pour « %s » ?", it.title), `<p class="hint">${esc(T("La console calcule les empreintes (quelques minutes pour un gros jeu), puis le partage depuis là où il est : rien n'est copié. Le lien et son code QR restent dans Téléchargements › Mes liens."))}</p>`, T("Oui, créer le lien")))) return;
    try { const r = await post("/shares/out", { kind: "game", item: it.key }); showNewBtLink(r.job, it.title); } catch (e) { toast(e.message); }
  } });
  if (saves.length) acts.push({ label: T("Une sauvegarde de partie… (%d)", saves.length), run: () => pick(saves) });
  if (games.length) acts.push({ label: T("Une copie du jeu, avec sa partie… (%d)", games.length), run: () => pick(games) });
  if (!acts.length) { toast(T("Aucune copie de cet élément pour l'instant : sauvegardez-le d'abord")); return; }
  acts.push({ label: T("Mes liens…"), run: () => myShares(anchor) });
  openMenu(anchor, acts);
}

// Mes liens (comme la télé) : tous les liens donnés depuis la console — BitTorrent (en préparation ou partagé), .zip de la
// console, MEGA, lien du service (OneDrive…). Chacun s'ouvre avec son code QR, son adresse, et de quoi l'arrêter.
function linkLine(l) {
  const what = l.kind === "bt" ? "BitTorrent" : l.kind === "local" ? T("Lien de la console (.zip)") : l.kind === "mega" ? "MEGA" : (l.place_label || T("Lien du service"));
  let st;
  if (l.state === "preparing") st = l.total > 0 ? T("en préparation · %d %%", Math.floor((l.done || 0) * 100 / l.total)) : T("en préparation");
  else if (l.state === "error") st = T("erreur");
  else if (l.state === "paused") st = T("en pause");
  else if (l.state === "checking") st = T("vérification des fichiers");
  else if (l.state === "missing") st = T("fichiers introuvables");
  else if (l.kind === "bt") st = T("partagé · %d pair(s)", l.peers || 0);
  else if (l.kind === "local") st = T("jusqu'au %s · %d téléchargement(s)", mgDate(l.expires), l.downloads || 0);
  else st = T("ouvert");
  return `${l.title} · ${what} · ${st}`;
}
function linkShow(l) {
  const where = l.kind === "local" ? T("jusqu'au %s", mgDate(l.expires)) : l.kind === "bt" ? "BitTorrent (magnet)" + (l.total > 0 ? " · " + size(l.total) : "")
    : l.kind === "mega" ? "MEGA" : (l.place_label || "");
  shareShow({ ...l, where, match: l.kind === "bt" ? (l.file || l.title) : "" });
}
function showNewBtLink(job, title) {
  state.linksAt = 0;
  shareShow({ kind: "bt", id: job, title, state: "preparing", where: "BitTorrent (magnet)" });
}
async function myShares(anchor) {
  let r;
  try { r = await api("/links"); } catch (e) { toast(e.message || T("Liste impossible")); return; }
  state.links = r.links || [];
  const acts = state.links.map((l) => ({ label: linkLine(l), run: () => linkShow(l) }));
  if (!acts.length) { toast(T("Aucun lien pour l'instant : Partager… crée un lien pour un jeu, une ROM ou une sauvegarde")); return; }
  openMenu(anchor, acts, T("Tous les liens donnés depuis cette console : choisissez-en un pour son code QR, son adresse, ou pour l'arrêter."));
}

async function shareShow(s) {
  const local = s.kind === "local", bt = s.kind === "bt", stoppable = local || s.kind === "mega" || s.kind === "service" || (bt && !!s.id);
  if (bt && s.state === "preparing") return btPreparing(s);
  const text = bt ? (s.state === "error" ? T("Le lien n'a pas pu être fait : %s", s.error || "")
      : s.state === "missing" ? T("Les fichiers de ce lien ne sont plus là où ils étaient (déplacés ou retirés) : il ne partage rien. Arrêtez-le, puis refaites un lien avec Partager…")
      : T("Lien magnet : scannez-le avec le téléphone ou ouvrez-le dans un logiciel BitTorrent (une autre PS5, qBittorrent…). La console partage le jeu tant qu'elle est allumée et que le lien reste dans Mes liens."))
    : s.kind === "mega" ? T("Lien MEGA : il s'ouvre partout, dans un navigateur ou l'appli MEGA. Il contient la clé : ne le donnez qu'aux personnes voulues.")
    : s.kind === "service" ? T("Lien du service : il s'ouvre partout, pour toute personne qui a le lien, en lecture. La console peut être éteinte. Arrêtez-le quand il ne sert plus.")
    : T("Scannez avec le téléphone, ou ouvrez l'adresse sur un ordinateur : la copie se télécharge en .zip, lue directement là où elle est rangée.");
  if (bt && s.state === "missing") s = { ...s, qr: null, url: "" };  // un lien sans ses fichiers : ni code QR ni adresse à donner
  // Deux liens quand la console connaît son adresse publique (comme la télé, touche Carré) : « Lien de la maison » et
  // « Lien Internet » ; le bouton passe de l'un à l'autre et le code QR, l'adresse et « Copier » suivent.
  const two = !!s.url_public && !(bt && s.state !== "ready");
  const hintFor = (pub) => local ? T("Le lien s'arrête seul le %s.", mgDate(s.expires)) + " " + (pub
        ? T("Lien Internet, pour quelqu'un de loin : la console doit rester allumée et son port 52370 ouvert sur la box (UPnP le fait seul s'il est activé dans Paramètres › Réseau).")
        : T("Il marche sur le réseau de la maison ; par Internet seulement si le port 52370 de la console est ouvert sur la box. La console doit rester allumée."))
    : bt && s.state === "ready" ? (pub ? T("Lien Internet, pour quelqu'un de loin (%s) : la console doit rester allumée et son port BitTorrent ouvert sur la box (UPnP le fait seul s'il est activé dans Paramètres › Réseau).", s.public || "")
        : two ? T("Lien de la maison : pour une autre PS5 ou un PC sur le même réseau.") : T("Marche sur le réseau de la maison ; depuis Internet, dès que l'adresse publique est connue"))
    : "";
  const html = `<p class="hint">${esc(s.where || "")}</p>
    ${two ? `<div class="seg" id="sh-seg"><button class="secondary small chosen" data-pub="0">${T("Lien de la maison")}</button><button class="secondary small" data-pub="1">${T("Lien Internet")}</button></div>` : ""}
    <div class="row-inline" style="gap:20px;align-items:flex-start;flex-wrap:wrap"><div class="qr" id="sh-qr" ${s.qr && s.qr.length ? "" : "hidden"}>${s.qr && s.qr.length ? qrSvg(s.qr) : ""}</div><div style="flex:1;min-width:240px">
    <p>${esc(text)}</p>${s.url ? `<p><a id="sh-url" href="${esc(s.url)}" target="_blank" rel="noopener" style="word-break:break-all">${esc(s.url)}</a></p>
    <p><button class="secondary small" id="sh-copy">${T("Copier le lien")}</button>` : "<p>"}${local ? ` <button class="secondary small" id="sh-refresh">${T("Actualiser l'adresse")}</button>` : ""}</p>
    <p class="hint" id="sh-hint">${esc(hintFor(false))}</p></div></div>`;
  const p = ask(T("Partager « %s »", s.title || ""), html, stoppable ? (bt ? T("Arrêter le partage…") : T("Arrêter le lien…")) : T("Fermer"), stoppable ? T("Fermer") : "");
  let shown = s.url;
  const cp = document.getElementById("sh-copy");
  const copy = async () => { try { await navigator.clipboard.writeText(shown || ""); toast(T("Lien copié")); } catch (e) { toast(T("Copie impossible : sélectionnez le lien")); } };
  if (cp) cp.onclick = copy;
  document.querySelectorAll("#sh-seg [data-pub]").forEach((b) => b.onclick = () => {
    const pub = b.dataset.pub === "1", qr = pub ? s.qr_public : s.qr;
    shown = pub ? s.url_public : s.url;
    document.querySelectorAll("#sh-seg [data-pub]").forEach((x) => x.classList.toggle("chosen", x === b));
    const q = document.getElementById("sh-qr"); q.innerHTML = qr && qr.length ? qrSvg(qr) : ""; q.hidden = !(qr && qr.length);
    const a = document.getElementById("sh-url"); if (a) { a.href = shown; a.textContent = shown; }
    document.getElementById("sh-hint").textContent = hintFor(pub);
  });
  const rf = document.getElementById("sh-refresh");
  if (rf) rf.onclick = async () => {  // redemander l'adresse publique, puis rouvrir ce lien à jour
    try {
      await post("/public-address", { refresh: true });
      const all = (await api("/shares/out")).shares || [];
      const x = all.find((y) => y.token === s.token);
      const box = rf.closest(".modal"); if (box && box.cancel) box.cancel();
      toast(x && x.url_public ? T("Adresse publique à jour") : T("Adresse publique inconnue : la console n'a pas pu la demander"));
      if (x) setTimeout(() => shareShow({ ...x, kind: "local", where: s.where }), 50);
    } catch (e) { toast(e.message); }
  };
  if (!(await p) || !stoppable) return;
  if (bt) {
    if (!(await ask(T("Arrêter ce partage ?"), `<p>${esc(T("Le lien ne marchera plus. Rien n'est effacé : le jeu et ses fichiers restent où ils sont."))}</p>`, T("Arrêter ce partage")))) return;
    try { await post("/shares/out/stop", { kind: "bt", id: s.id }); toast(T("Partage arrêté : le jeu reste sur la console")); } catch (e) { toast(e.message); }
    state.linksAt = 0;
    return;
  }
  if (!(await ask(T("Arrêter ce lien ?"), `<p>${esc(T("Il ne s'ouvrira plus, pour personne. La copie n'est pas touchée."))}</p>`, T("Arrêter ce lien")))) return;
  try {
    if (local) await post("/shares/out/stop", { token: s.token });
    else await post("/shares/out", { kind: s.kind, off: true, place: s.place, bundle: s.bundle });  // MEGA, Drive, OneDrive, Dropbox : retiré chez le service
    toast(T("Lien arrêté : il ne s'ouvre plus"));
  } catch (e) { toast(e.message); }
}

// Lien BitTorrent en préparation : la barre avance (relue toutes les 2 s) ; à la fin, la fenêtre montre le code QR.
async function btPreparing(s) {
  const bar = (l) => { const t = l.total || 0, d = l.done || 0;
    return `<div class="bar"><span style="width:${t ? Math.max(1, Math.floor(d * 100 / t)) : 1}%"></span></div><p class="hint">${esc(t ? T("Préparation : %d %%  ·  %s sur %s", Math.floor(d * 100 / t), size(d), size(t)) : T("Préparation…"))}</p>`; };
  const html = `<p class="hint">${esc(s.where || "")}</p><p>${esc(T("La console calcule les empreintes du jeu, là où il est rangé : rien n'est copié. Le code QR apparaît ici à la fin ; vous pouvez fermer, le lien reste dans Téléchargements › Mes liens."))}</p><div id="bt-prep">${bar(s)}</div>`;
  const p = ask(T("Partager « %s »", s.title || ""), html, T("Arrêter…"), T("Fermer"));
  let match = s.match || "", done = false;
  const tick = setInterval(async () => {
    const box = document.getElementById("bt-prep");
    if (!box) { clearInterval(tick); return; }
    let r; try { r = await api("/links"); } catch (e) { return; }
    const l = (r.links || []).find((x) => x.kind === "bt" && (x.id === s.id || (match && (x.file || x.title) === match && x.state !== "preparing")));
    if (!l) return;
    match = l.file || l.title;
    if (l.state === "preparing") { box.innerHTML = bar(l); return; }
    clearInterval(tick); done = true;
    const m = box.closest(".modal"); if (m && m.cancel) m.cancel();
    setTimeout(() => linkShow({ ...l, title: s.title || l.title }), 50);
  }, 2000);
  const stop = await p;
  clearInterval(tick);
  if (!stop || done) return;
  if (!(await ask(T("Arrêter ce partage ?"), `<p>${esc(T("Le lien ne marchera plus. Rien n'est effacé : le jeu et ses fichiers restent où ils sont."))}</p>`, T("Arrêter ce partage")))) return;
  try { await post("/shares/out/stop", { kind: "bt", id: s.id }); toast(T("Partage arrêté : le jeu reste sur la console")); } catch (e) { toast(e.message); }
}

// Téléchargements › Partager… (comme la télé) : quoi (jeux PS5, PS4, ROM par console, applis, sauvegardes, un fichier),
// où (chaque disque, avec le nombre), puis la liste de A à Z ; un élément ouvre « Que partager ? ».
async function shareHub(anchor) {
  const imgs = await loadImages().catch(() => new Map());
  const disk = (it) => { const g = imgs.get(it.key); return (g && g.disk && g.disk.label ? T(g.disk.label) : "") || placeLabel(it.place); };
  const items = state.items || [];
  const of = (k) => items.filter((it) => it.kind === k);
  const saves = items.filter((it) => (it.components || []).some((c) => c.part === "save"));
  const list = (l, what) => {
    const sorted = [...l].sort((a, b) => (a.title || "").localeCompare(b.title || ""));
    openMenu(anchor, sorted.map((it) => {
      const b = (it.components || []).reduce((n, c) => n + (c.bytes || 0), 0);
      return { label: `${it.title} · ${disk(it)}${b ? " · " + size(b) : ""}`, run: () => shareFromSheet(anchor, it) };
    }));
  };
  const where = (l, what) => {
    const by = new Map();
    for (const it of l) { const d = disk(it); if (!by.has(d)) by.set(d, []); by.get(d).push(it); }
    if (by.size <= 1) { list(l, what); return; }
    openMenu(anchor, [{ label: `${T("Partout")} · ${l.length}`, run: () => list(l, what) },
      ...[...by].map(([d, x]) => ({ label: `${d} · ${x.length}`, run: () => list(x, what) }))]);
  };
  const acts = [];
  const add = (name, l) => { if (l.length) acts.push({ label: `${name} · ${l.length}`, run: () => where(l, name) }); };
  add(T("Jeux PS5"), of("game_ps5"));
  add(T("Jeux PS4"), of("game_ps4"));
  const roms = of("rom");
  if (roms.length) acts.push({ label: `${T("ROM (par console)")} · ${roms.length}`, run: () => {
    const by = new Map();
    for (const r of roms) { const p = r.platform || T("Autre"); if (!by.has(p)) by.set(p, []); by.get(p).push(r); }
    openMenu(anchor, [...by].sort((a, b) => a[0].localeCompare(b[0])).map(([p, l]) => ({ label: `${p} · ${l.length}`, run: () => where(l, "ROM " + p) })));
  } });
  add(T("Applis"), of("app"));
  add(T("Sauvegardes de partie"), saves);
  acts.push({ label: T("Un fichier ou un dossier…"), run: () => createLink() });
  acts.push({ label: T("Mes liens…"), run: () => myShares(anchor) });
  openMenu(anchor, acts);
}

