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
const mgDate = (t, hour = true) => (t > 0 ? new Date(t * 1000).toLocaleString(I18N.code, hour ? { dateStyle: "short", timeStyle: "short" } : { dateStyle: "short" }) : "?");
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
  for (const c of g.copies) if (!seen.includes(c.place_label)) seen.push(c.place_label);
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

// Endroits : Partout, chaque disque USB, chaque NAS (et l'interne s'il garde des copies), Corbeille.
const TRASH_ICON = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" class="trash-ico" aria-hidden="true"><path d="M4 7h16M9 7V4.5h6V7M6.5 7l1 13h9l1-13M10 11v6M14 11v6"/></svg>`;
function mgPlaceList() {
  const count = new Map();
  let total = 0;
  const peers = new Map();  // autre PS5 : une carte par console (toutes ses copies, disques et NAS)
  for (const g of (mg.sum && mg.sum.games) || []) for (const c of g.copies) {
    total++;
    if (c.on_peer) { const k = mgPeerKey(c); const x = peers.get(k) || { name: c.peer_name, n: 0 }; x.n++; peers.set(k, x); continue; }
    count.set(c.place, (count.get(c.place) || 0) + 1);
  }
  const out = [{ id: "", name: T("Partout"), sub: T("%d copie(s)", total), icon: ICONS.consoles }];
  for (const p of state.places) {
    const n = count.get(p.id) || 0;
    if (!(p.type === "usb" || p.type === "ftp" || n)) continue;
    if (p.type === "switch" && !n) continue;
    const free = p.type === "usb" || p.type === "internal" ? " · " + T("%s libres", size(p.free)) : "";
    out.push({ id: p.id, name: p.label, sub: (n ? T("%d copie(s)", n) : T("vide")) + free, icon: ICONS[p.type === "usb" ? "usb" : p.type === "internal" ? "ps5" : p.type === "switch" ? "switch" : "nas"] });
  }
  for (const [id, n] of count) if (!out.some((p) => p.id === id)) {  // disque absent : ses copies restent visibles
    const g = mg.sum.games.find((x) => x.copies.some((c) => c.place === id)), c = g && g.copies.find((x) => x.place === id);
    out.push({ id, name: c ? c.place_label : id, sub: T("%d copie(s)", n) + " · " + T("Disque absent"), icon: ICONS.usb });
  }
  for (const [id, x] of peers) out.push({ id, name: x.name, sub: T("autre PS5 · %d copie(s)", x.n), icon: ICONS.ps5 });
  out.push({ id: "trash", name: T("Corbeille"), sub: T("30 jours"), icon: TRASH_ICON });
  return out;
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
    view.innerHTML = `<h1>${T("Sauvegardes")}</h1>${loading(T("Lecture des sauvegardes…"))}`;
    selBar(0);
    loadOverview().then(() => { if (state.tab === "backups") backupsTab(); });
    return;
  }
  const acc = mgAccount();
  if (mg.account !== acc) { mg.account = acc; mg.sum = null; mg.error = ""; }
  if (!mg.sum && !mg.loadP && !mg.error) mgLoad();
  if (mg.game && mgGame(mg.game)) return mgGameView(view);
  if (mg.game && !mg.sum) { view.innerHTML = `<h1>${T("Toutes les versions")}</h1>${loading(T("Lecture des sauvegardes…"))}`; selBar(0); return; }
  mg.game = "";
  const places = mgPlaceList();
  if (!places.some((p) => p.id === mg.place)) mg.place = "";
  view.innerHTML = `<h1>${T("Sauvegardes")}<small>${mg.sum ? esc(T("%d jeu(x) · %d copie(s)", mg.sum.games.length, mg.sum.copies || 0)) : ""}${mg.loading ? " " + SPIN : ""}</small></h1><p class="hint tab-help">${esc(T("Des COPIES de vos parties : sur les disques de cette console, le NAS et les autres PS5 — pas la partie active."))}</p>
    <div class="tr-label">${T("ENDROITS")}</div>
    <div class="places" data-scroll="bk-places">${places.map((p) => `<button class="tr-dev place-card ${mg.place === p.id ? "chosen" : ""}" data-place="${esc(p.id)}">
      <span class="dev-ico">${p.icon}</span><span class="grow"><b>${esc(p.name)}</b><span class="hint">${esc(p.sub)}</span></span></button>`).join("")}</div>
    <div id="bk-main"></div>`;
  $$("[data-place]", view).forEach((b) => b.onclick = () => { mg.place = b.dataset.place; mg.gsel.clear(); mg.csel.clear(); if (mg.place === "trash") mg.trash = {}; backupsTab(); });
  const main = $("#bk-main");
  if (!mg.sum) {
    main.innerHTML = mg.error
      ? `<div class="empty"><p class="bad">${esc(T("Lecture impossible : %s", mg.error))}</p><p class="hint">${T("Il faut le moteur 2.33 ou plus récent pour cet écran.")}</p><button class="secondary" id="mg-retry">${T("Actualiser")}</button></div>`
      : loading(T("Lecture des sauvegardes…"));
    const r = $("#mg-retry");
    if (r) r.onclick = () => { mg.error = ""; backupsTab(); };
    selBar(0);
    return;
  }
  if (mg.place === "trash") { selBar(0); main.innerHTML = `<div id="mg-trash"></div>`; return mgTrashView($("#mg-trash")); }
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
  try { plan = await api(`/sync/copies/plan?peer=${encodeURIComponent(peer)}${full ? "&full=1" : ""}`); } catch (e) { toast(e.message); return; }
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
  const nasPlaces = (state.places || []).filter((p) => p.type === "ftp");
  if (Date.now() - syncState.plansAt > 60000 && ((typeof peerGames !== "undefined" && peerGames.length) || nasPlaces.length)) {  // PS5 jumelées et NAS
    syncState.plansAt = Date.now();
    const out = [];
    for (const p of (typeof peerGames !== "undefined" ? peerGames : [])) { try { const pl = await api(`/sync/copies/plan?peer=${encodeURIComponent(p.instance)}&full=1`); if (!pl.error) out.push({ name: p.name, plan: pl }); } catch (e) { /* hors ligne */ } }
    for (const n of nasPlaces) { try { const pl = await api(`/sync/copies/plan?peer=${encodeURIComponent(n.id)}&full=1`); if (!pl.error) out.push({ name: n.label, plan: pl }); } catch (e) { /* injoignable */ } }
    syncState.plans = out;
  }
}

function mgOverview(main) {
  const s = mg.sum, games = s.games, place = mg.place;
  const st = mgStats(place);
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
  main.innerHTML = `<div class="mg-stats three">
      <div class="mg-stat"><span class="hint">${T("Doublons sûrs")}</span><b class="${st.dup ? "ok" : ""}">${esc(T("%s à libérer", size(st.freed)))}</b><span class="hint">${esc(T("%d copie(s)", st.dup))}</span>
        <button class="secondary small" id="mg-clean" ${st.dup ? "" : "disabled"}>${T("Nettoyer les doublons…")}</button></div>
      <div class="mg-stat"><span class="hint">${T("Pas encore comparés")}</span><b class="${st.cmp ? "warn" : "ok"}">${esc(T("%d jeu(x)", st.cmp))}</b>
        <span class="hint">${mg.comparing ? SPIN + esc(T("comparaison en cours : %d jeu(x)", mg.comparing)) : ""}</span>
        <button class="secondary small" id="mg-compare" ${s.to_compare ? "" : "disabled"}>${T("Tout comparer")}</button></div>
      <button class="mg-stat" data-goview="3"><span class="hint">${T("À vérifier")}</span><b class="${st.chk ? "warn" : "ok"}">${esc(T("%d copie(s)", st.chk))}</b><span class="hint">${T("vides ou incomplètes : mises à part, rien n'est supprimé")}</span></button>
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
      return `<div class="row mg-game ${on ? "on" : ""}" data-g="${esc(k)}">${ckHtml(`data-c="${esc(k)}"`, on)}${mgCover(g)}
        <div class="grow"><div class="t">${esc(g.title)} <span class="hint">${esc(mgPlaces(g))}</span></div><div>${esc(mgSummary(g))}</div>${pl ? `<div class="d progress-text">${esc(pl)}</div>` : ""}</div>
        <span class="tag ${tone}">${esc(badge)}</span>${moreBtn(`data-m="${esc(k)}"`)}</div>`;
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
    { label: T("Toutes les versions"), run: () => { mg.back = ""; mg.game = k; window.scrollTo(0, 0); backupsTab(); } },
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
  const selectable = mine.filter((c) => !mg.prot.has(mgCk(c)));
  const canRestore = (c) => !c.on_peer && c.complete !== false && (g.full || (g.account || "").startsWith("ps5:"));
  const copyRow = ([c, i]) => {
    const ck = mgCk(c), prot = mg.prot.has(ck), other = !!c.other_account || !!c.on_peer;
    const [tag, t] = other ? [T("Autre compte"), "muted"] : c.needs_resign ? [T("Autre identifiant"), "info"] : c.complete === false ? [T("Incomplète"), "bad"] : c.content_bytes === 0 ? [T("Vide"), "bad"] : g.full ? ["", ""] : !c.group ? [T("À comparer"), "warn"]
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
    if (!l2) l2 = T("contenu non comparé");
    const letter = c.group ? (c.group === cg ? T("contenu %s (= console)", cg) : T("contenu %s", c.group)) : "";
    return `<div class="row mg-copy ${ch.has(ck) ? "on" : ""}">${ckHtml(`data-k="${esc(ck)}" ${other || prot ? "disabled" : ""}`, ch.has(ck))}
      <div class="grow"><div class="t">${esc(c.place_label)} · ${esc(mgDate(c.created_unix))}</div><div class="d ${prot ? "warn" : ""}">${esc(l2)}</div></div>
      <span class="hint">${esc(letter)}</span>${tag ? `<span class="tag ${t}">${esc(tag)}</span>` : "<span></span>"}
      <span class="vbtns"><button class="secondary small" data-r="${esc(ck)}" ${canRestore(c) ? "" : "disabled"}>${c.needs_resign ? T("Reprendre sur mon compte…") : T("Restaurer…")}</button>${moreBtn(`data-vm="${esc(ck)}"`)}</span></div>`;
  };
  const con = g.console;
  main.innerHTML = `<div class="eyebrow tr-crumb"><button class="link" id="mg-back">‹ ${T("Retour")}</button> ${T("SAUVEGARDES")} · ${T("TOUTES LES VERSIONS")}</div>
    <div class="acc-head mg-title">${mgCover(g)}<div><h2>${esc(g.title)}</h2><span class="hint">${esc(mgPlaces(g))}</span></div></div>
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
      }) })) : []),
      { label: prot ? T("Retirer la protection") : T("Protéger"), run: () => mgProtect([c], !prot) },
      ...(!prot && !c.other_account ? [{ label: T("Supprimer…"), cls: "bad", run: () => { mg.csel.set(k, new Set([ck])); mg.gsel.add(k); mgPreview([g]); } }] : []),
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
  const sources = (g.console ? [{ kind: "console" }] : []).concat(g.copies.filter((c) => c.complete !== false).map((c) => ({ place: c.place, bundle: c.bundle })));
  try {
    const r = await post("/tasks", { type: "inspect_save", title_id: g.title_id, user_id: g.account.split(":")[1], sources });
    mg.task = r.id; mg.taskGame = mgGk(g);
    toast(T("Comparaison du contenu… (lecture seule, sur des copies)"));
    refresh(true);
  } catch (e) { toast(e.message || T("Comparaison refusée")); }
  backupsTab();
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
  $("#mg-cancel", box).onclick = () => { close(); toast(T("Rien n'a été supprimé")); };
  box.onclick = (e) => { if (e.target === box && !busy) { close(); toast(T("Rien n'a été supprimé")); } };
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
          <div class="d">${esc(new Date(b.created).toLocaleString(I18N.code))} · ${b.bytes ? size(b.bytes) : T("taille ?")} · ${(b.parts || []).map((p) => PARTS[p] || p).join(", ")}</div></div>
        <div class="btns"><button data-bundle="${esc(b.bundle)}" ${b.complete !== false && place.type === "usb" ? "" : "disabled"}>${T("Restaurer dans un dossier neuf")}</button><button class="bad" data-trash="${esc(b.bundle)}" data-name="${esc(b.title || b.bundle)}">${T("Corbeille…")}</button></div></div>`).join("")}</div>`;
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
  try { idx = await api("/backups?place=" + encodeURIComponent(placeId)); } catch (e) { idx = { error: e.message, backups: [] }; }
  idx.at = Date.now();
  mg.disk[placeId] = idx;
  const r = $("#bk-extra");
  if (r && mg.place === placeId) { root = r; draw(idx); }
}

// Corbeille de chaque disque USB et du NAS, par jour ; « Remettre » remet la copie à sa place.
async function mgTrashView(root) {
  const places = (state.places || []).filter((p) => p.type === "usb" || p.type === "ftp");
  const draw = () => {
    if (!root.isConnected) return;
    root.innerHTML = places.length ? places.map((p) => {
      const list = mg.trash[p.id];
      if (list === undefined) return `<h3>${esc(p.label)}</h3>${loading(T("lecture…"))}`;
      if (list.error) return `<h3>${esc(p.label)}</h3><p class="bad">${esc(list.error)}</p>`;
      if (!list.length) return `<h3>${esc(p.label)} <span class="hint">· ${T("corbeille vide")}</span></h3>`;
      const days = [...new Set(list.map((t) => t.day))].sort().reverse();
      return `<h3>${esc(p.label)} <span class="hint">· ${esc(T("%d copie(s)", list.length))}</span></h3>` + days.map((d) => {
        const [y, m, dd] = d.split("-");
        return `<p class="hint">${esc(T("Mise à la corbeille le %s", dd && m ? `${dd}/${m}/${y}` : d))}</p><div class="list">${list.filter((t) => t.day === d).map((t) => {
          const g = (mg.sum.games || []).find((x) => (x.title_id && t.bundle.includes(x.title_id)) || t.bundle.includes(x.title));
          return `<div class="row"><div><div class="t">${esc(g ? g.title : t.bundle.split("/").pop())}</div><div class="d">${esc(t.bundle)}</div></div>
            <div class="btns"><button data-place="${esc(p.id)}" data-path="${esc(t.path)}">${T("Remettre")}</button></div></div>`;
        }).join("")}</div>`;
      }).join("");
    }).join("") + `<p class="hint">${T("Les copies restent 30 jours dans « PBC Backup/Corbeille » de leur disque ou NAS.")}</p>` : `<div class="empty">${T("Aucun disque USB ni NAS.")}</div>`;
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
    catch (e) { mg.trash[p.id] = { error: e.message }; }
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
