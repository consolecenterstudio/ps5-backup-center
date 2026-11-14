// PS5 Backup Center — interface web servie par le moteur. Tout vient de l'API /api/v2 :
// affichage immédiat de l'état connu, mise à jour quand la révision du moteur change.
// Mêmes onglets que l'application télé (docs/SPEC-decoupage.md) : Accueil, Bibliothèque, Parties, Backups, Transferts, Appareils,
// Tâches (+ Paramètres), et la même règle de sélection partout (case, case « Tout », barre d'actions, menu « ⋯ »).
"use strict";
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const KINDS = { game_ps5: "PS5", game_ps4: "PS4", app: T("Application"), rom: "ROM" };
const PARTS = { game: T("Jeu"), update: T("Mise à jour"), dlc: T("DLC"), save: T("Sauvegarde"), state: T("États"), config: T("Configuration"), app: T("Application") };
const STATES = { queued: T("En attente"), running: T("En cours"), paused: T("En pause"), done: T("Terminée"), failed: T("Échec"), cancelled: T("Annulée") };
const TASK_KINDS = { transfer: T("Transfert"), link_send: T("Envoi vers une autre PS5"), compress: T("Compression"), uncompress: T("Décompression"), full_backup: T("Sauvegarde complète"), make_image: T("Création d'image"),
  extract_image: T("Extraction d'image"), validate_image: T("Vérification d'image"), read_speed_test: T("Test de vitesse"), build_ampr_index: T("Index AMPR"),
  set_read_only: T("Lecture seule"), backup: T("Sauvegarde"), restore: T("Restauration"), restore_save: T("Restauration"), import_switch: T("Synchro Switch"), export_switch: T("Synchro Switch"),
  files_copy: T("Fichiers · copie"), files_move: T("Fichiers · déplacement"), files_delete: T("Fichiers · suppression"), game_uninstall: T("Désinstallation") };
// Thèmes : mêmes palettes que la télé (app/src/gfx.cpp) ; les couleurs vivent dans app.css ([data-theme=…]).
// Aperçu : fond haut / bas, carte, texte, accent, accent 2.
const THEMES = [
  ["nuit", T("Nuit"), "#0a1e3a", "#06101d", "#0f1f35", "#e8f1fb", "#7cc4ff", "#3d8bff"],
  ["oled", T("Minuit OLED"), "#000000", "#000000", "#0d0f15", "#eef1f6", "#7cc4ff", "#3d8bff"],
  ["playstation", "PlayStation", "#062a6e", "#020816", "#0c1f45", "#ffffff", "#ffffff", "#0070d1"],
  ["aurore", T("Aurore"), "#1e1240", "#0a0818", "#1c1638", "#f0ecfc", "#5eead4", "#8b5cf6"],
  ["foret", T("Forêt"), "#0e2a1e", "#06100c", "#12261e", "#e6f2e8", "#86e0a0", "#2ea064"],
  ["sakura", T("Sakura"), "#34182e", "#140a12", "#301c2e", "#fcecf4", "#ffaacd", "#ec6096"],
  ["clair", T("Clair"), "#f6f9fd", "#dce6f2", "#ffffff", "#101c2e", "#0062cc", "#0070e0"],
  ["contraste", T("Contraste élevé"), "#000000", "#000000", "#0e0e0e", "#ffffff", "#ffdd00", "#00c8ff"],
];
function applyTheme(id) {
  if (!THEMES.some((t) => t[0] === id)) id = "nuit";
  document.documentElement.dataset.theme = id;
  try { localStorage.setItem("pbc-theme", id); } catch (e) {}
}
try { applyTheme(localStorage.getItem("pbc-theme") || "nuit"); } catch (e) { applyTheme("nuit"); }
function themeSwatch([id, name, top, bottom, panel, text, accent, accent2]) {
  return `<button class="theme-pick ${document.documentElement.dataset.theme === id ? "active" : ""}" data-theme-pick="${id}">
    <span class="sw" style="background:linear-gradient(${top}, ${bottom})">
      <i style="left:10px;right:10px;top:10px;height:26px;background:${panel}"></i><i style="left:18px;top:19px;width:34%;height:8px;background:${text}"></i>
      <i style="left:10px;bottom:10px;width:46%;height:24px;border-radius:12px;background:${accent}"></i><i style="left:58%;bottom:13px;width:18px;height:18px;border-radius:9px;background:${accent2}"></i>
    </span>${esc(name)}</button>`;
}

const SPIN = '<span class="spinner"></span>';
const loading = (text) => `<div class="loading">${SPIN}${esc(text)}</div>`;

const state = { tab: "home", token: localStorage.getItem("pbc-token") || "", status: null, items: [], places: [], tasks: [], remotes: [], loaded: false,
  emulators: [], settings: {}, devices: [], filter: { kind: "", app: "", place: "", q: "" }, taskFilter: "active", settingsSection: "general",
  revision: -1, party: { player: "", view: "all", sel: new Set() }, edenProfiles: null, devicesAt: 0, hero: 0, overview: null, overviewAt: 0, images: null, imagesAt: 0,
  tr: { screen: "main", from: "ps5", to: "", action: 0, cat: 0, sel: new Set(), compress: false, saves: true, verify: true,
    fullOff: new Set(["images", "system"]), fullMode: 0, fullTo: "", full: null, fullAt: 0, backups: null, syncA: "", syncB: "" } };

function size(b) {
  const u = [T("o"), T("Ko"), T("Mo"), T("Go"), T("To")]; let i = 0;
  if (!b) return `0 ${u[0]}`;
  while (b >= 1000 && i < u.length - 1) { b /= 1000; i++; }
  return `${b < 10 && i ? b.toFixed(1) : Math.round(b)} ${u[i]}`.replace(".", I18N.decimal);
}
function duration(s) {
  if (!isFinite(s) || s <= 0) return "";
  if (s < 60) return `${Math.ceil(s)} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${Math.round(s % 60)} s`;
  return `${Math.floor(s / 3600)} h ${Math.round((s % 3600) / 60)} min`;
}
function toast(msg) {
  const t = $("#toast"); t.textContent = msg; t.classList.remove("hidden");
  clearTimeout(toast.timer); toast.timer = setTimeout(() => t.classList.add("hidden"), 3500);
}
// Titre sans les parenthèses de région ni les crochets : « Aladdin (Europe) » → « Aladdin ».
const cleanTitle = (t) => (t || "").replace(/\s*[([][^)\]]*[)\]]/g, "").trim() || t || "";

async function api(path, opts = {}) {
  const r = await fetch("/api/v2" + path, { ...opts, headers: { "X-PBC-Token": state.token, ...(opts.body ? { "Content-Type": "application/json" } : {}) } });
  if (r.status === 401) {  // jeton refusé (appareil oublié) : on repart sur l'appairage
    state.token = ""; try { localStorage.removeItem("pbc-token"); } catch (e) {}
    showPair(); throw new Error(T("non associé"));
  }
  const ct = r.headers.get("Content-Type") || "";
  const data = ct.includes("json") ? await r.json() : null;
  if (!r.ok) throw new Error((data && data.error) || T("erreur %d", r.status));
  return data;
}
const post = (path, body = {}) => api(path, { method: "POST", body: JSON.stringify(body) });

// Petite fenêtre de confirmation, dans le style de l'app (pas de confirm() du navigateur).
function ask(title, html, ok = T("Continuer"), cancel = T("Annuler")) {
  return new Promise((resolve) => {
    const box = document.createElement("div");
    box.className = "modal";
    box.innerHTML = `<div class="dialog"><h2>${esc(title)}</h2>${html}<div class="actions"><button class="primary" data-r="1">${esc(ok)}</button>${cancel ? `<button class="secondary" data-r="0">${esc(cancel)}</button>` : ""}</div></div>`;
    document.body.appendChild(box);
    box.querySelectorAll("[data-r]").forEach((b) => b.onclick = () => { box.remove(); resolve(b.dataset.r === "1"); });
    box.onclick = (e) => { if (e.target === box) { box.remove(); resolve(false); } };
  });
}

// Supprimer la sauvegarde d'un jeu sur la console : choix du compte s'il y en a plusieurs, puis confirmation. Le moteur
// refuse sans copie à jour dans Backups et fait une copie de sécurité juste avant.
const hasConsoleSave = (tid) => !!tid && entries().some((e) => e.title_id === tid && e.console);
async function deleteConsoleSave(anchor, tid, title, done) {
  const accs = entries().filter((e) => e.title_id === tid && e.console).map((e) => {
    const a = ((state.overview && state.overview.accounts) || []).find((x) => x.key === e.account);
    return { uid: e.account.split(":")[1], name: (a && a.name) || e.account.split(":")[1], status: e.status };
  });
  if (!accs.length) { toast(T("Aucune sauvegarde de ce jeu sur la console")); return; }
  const go = async (a) => {
    let force = false;
    if (a.status !== "up_to_date" && a.status !== "backup_newer") {
      // Sans copie à jour : possible quand même, en deux confirmations (comme la télé).
      if (!(await ask(T("Supprimer la sauvegarde de « %s » ?", title), `<p>${esc(a.status === "console_newer"
        ? T("La partie active est plus récente que vos sauvegardes : sauvegardez-la d'abord (fiche › Sauvegarder), puis supprimez.")
        : T("Aucune copie dans Sauvegardes : sauvegardez-la d'abord (fiche › Sauvegarder), puis supprimez."))}</p>`, T("Supprimer quand même…")))) return;
      if (!(await holdAsk(T("Supprimer DÉFINITIVEMENT la sauvegarde de « %s » (%s) ?", title, a.name),
        `<p>${esc(T("C'est la partie active sur la console, pas une copie de l'onglet Sauvegardes."))} ${esc(T("Aucune sauvegarde à jour ne pourra la remettre. Seule une copie de sécurité brute est gardée sur la console (dossier de l'application)."))}</p>`,
        T("Maintenir 5 s pour supprimer")))) return;
      force = true;
    } else {
      const note = `<p>${esc(T("C'est la partie active sur la console, pas une copie de l'onglet Sauvegardes."))} ${esc(T("Une copie à jour reste dans Sauvegardes : vous pourrez la restaurer. Une copie de sécurité est aussi faite juste avant."))}</p>`;
      if (!(await ask(T("Supprimer la sauvegarde de « %s » (%s) ?", title, a.name), note, T("Supprimer de la console…")))) return;
      if (!(await holdAsk(T("Supprimer la sauvegarde de « %s » (%s) ?", title, a.name), note, T("Maintenir 5 s pour supprimer")))) return;
    }
    try { await post("/saves/delete", { title_id: tid, user_id: a.uid, force }); toast(T("Sauvegarde de « %s » supprimée de la console", title)); state.overview = null; if (done) done(); }
    catch (e) { toast(e.message); }
  };
  if (accs.length === 1) return go(accs[0]);
  setTimeout(() => openMenu(anchor, accs.map((a) => ({ label: a.name, hint: T("Supprimer la sauvegarde de « %s » : quel compte ?", title), run: () => go(a) }))), 0);
}

// Dernière étape d'une action sans retour : maintenir le bouton 5 s (souris ou doigt) ; relâcher avant = rien.
function holdAsk(title, html, label) {
  return new Promise((resolve) => {
    const box = document.createElement("div");
    box.className = "modal";
    box.innerHTML = `<div class="dialog hold"><h2 class="bad">${esc(title)}</h2>${html}
      <button class="danger hold-btn" type="button"><span class="hold-fill"></span><span class="hold-txt">${esc(label)}</span></button>
      <div class="actions"><button class="secondary" data-r="0">${esc(T("Annuler"))}</button></div></div>`;
    document.body.appendChild(box);
    const btn = box.querySelector(".hold-btn"), fill = box.querySelector(".hold-fill"), txt = box.querySelector(".hold-txt");
    let t0 = 0, raf = 0;
    const stop = () => { t0 = 0; cancelAnimationFrame(raf); fill.style.width = "0%"; txt.textContent = label; };
    const tick = () => {
      if (!t0) return;
      const ms = performance.now() - t0;
      fill.style.width = Math.min(100, ms / 50) + "%";
      txt.textContent = T("Encore %d s…", Math.max(1, Math.ceil((5000 - ms) / 1000)));
      if (ms >= 5000) { box.remove(); resolve(true); return; }
      raf = requestAnimationFrame(tick);
    };
    btn.onpointerdown = (e) => { e.preventDefault(); t0 = performance.now(); raf = requestAnimationFrame(tick); };
    btn.onpointerup = btn.onpointerleave = btn.onpointercancel = stop;
    box.querySelector("[data-r]").onclick = () => { stop(); box.remove(); resolve(false); };
    box.onclick = (e) => { if (e.target === box) { stop(); box.remove(); resolve(false); } };
  });
}

// ---- Règle unique de sélection (comme la télé, docs/SPEC-decoupage.md) ----------------------
// Case à cocher sur chaque ligne / jaquette, case « Tout » en tête de liste ; dès qu'un élément est coché, barre d'actions
// collante « N sélectionnés · Supprimer… · Ignorer… · Protéger · Copier vers… · Annuler » (seulement les actions qui ont
// un sens sur l'écran) ; menu « ⋯ » par ligne pour agir sur un seul élément.
const ckHtml = (attrs, on) => `<input type="checkbox" class="ck" ${attrs} ${on ? "checked" : ""} aria-label="${esc(T("Cocher"))}">`;
const allBoxHtml = (id, n, total) => `<label class="sel-all"><input type="checkbox" class="ck" id="${id}" ${total && n >= total ? "checked" : ""} ${total ? "" : "disabled"}>${T("Tout")}</label>`;
function setAllBox(el, n, total) { if (el) { el.checked = !!total && n >= total; el.indeterminate = n > 0 && n < total; } }
const moreBtn = (attrs) => `<button class="more" type="button" ${attrs} title="${esc(T("Actions"))}" aria-label="${esc(T("Actions"))}">⋯</button>`;
// Barre d'actions : actions = [{ label, run, cls }] ; n = 0 la retire. Une seule à la fois, sous la page.
function selBar(n, actions = [], cancel = null, info = "") {
  let bar = $("#selbar");
  if (!n) { if (bar) bar.remove(); document.body.classList.remove("has-selbar"); return; }
  if (!bar) { bar = document.createElement("div"); bar.id = "selbar"; bar.className = "selbar"; document.body.appendChild(bar); }
  bar.innerHTML = `<span class="selbar-n"><b>${esc(n > 1 ? T("%d sélectionnés", n) : T("%d sélectionné", n))}</b>${info ? `<span class="hint">${esc(info)}</span>` : ""}</span>
    ${actions.map((a, i) => `<button class="${a.cls || "secondary"}" data-sa="${i}">${esc(a.label)}</button>`).join("")}<button class="link" data-sa="x">${T("Annuler")}</button>`;
  $$("[data-sa]", bar).forEach((b) => b.onclick = () => (b.dataset.sa === "x" ? cancel && cancel() : actions[+b.dataset.sa].run()));
  document.body.classList.add("has-selbar");
}
// Menu « ⋯ » : petite liste d'actions sous le bouton (au-dessus s'il manque de place).
// Compatibilité (constat, pas un verdict — le minimum déclaré n'est qu'un indice), comme sur la télé.
function compatStateText(c) {
  const who = c.console || T("cette console");
  const label = { system_ok: T("Compatible : système suffisant"), verified: T("Compatibilité vérifiée"), backport_present: T("Backport nécessaire : présent"),
    no_backport: T("Risque de ne pas fonctionner : aucun backport"), incompatible_known: T("Incompatibilité connue") }[c.state] || T("Informations insuffisantes");
  return c.firmware ? T("%s (%s, système %s)", label, who, c.firmware) : T("%s (%s)", label, who);
}
function compatReasons(c) {
  return (c.reasons || []).map((r) => ({
    declared: () => T("le jeu déclare le système %s", r.declared),
    exe_sdk: () => T("exécutables : SDK %s", r.sdk),
    exe_signed: () => T("%d exécutable(s) signé(s), SDK non lisible", r.count),
    own_libs: () => r.sdk ? T("backport du jeu (fakelib) : %d bibliothèque(s), SDK %s", r.count, r.sdk) : T("backport du jeu (fakelib) : %d bibliothèque(s)", r.count),
    backport: () => T("backport ShadowMountPlus sur la console : %d fichier(s)", r.count),
    smp_fakelib: () => r.on ? T("backport fakelib activé dans ShadowMountPlus") : T("backport fakelib désactivé dans ShadowMountPlus"),
    format_failed: () => T(".%s ne s'est pas monté sur cette console : image non compressée conseillée", r.ext),
    unreadable: () => T("image non lisible ici"),
    no_firmware: () => T("système de la console inconnu"),
  }[r.code] || (() => ""))()).filter(Boolean);
}
function compatClass(c) { return { system_ok: "ok", verified: "ok", backport_present: "warn", no_backport: "bad", incompatible_known: "bad" }[c.state] || "muted"; }
function compatBackportText(c) {
  const bp = c.backport || {}, n = (bp.game_libs || []).length;
  const parts = n ? [bp.game_libs_sdk ? T("fakelib du jeu, %d bibliothèque(s), SDK %s", n, bp.game_libs_sdk) : T("fakelib du jeu, %d bibliothèque(s)", n)] : [];
  (bp.console || []).forEach((b) => parts.push(T("backport ShadowMountPlus sur la console : %d fichier(s)", (b.files || []).length)));
  if (bp.apr) parts.push(bp.apr.index ? T("APR-EMU (index présent)") : T("APR-EMU (sans index)"));
  return parts.join(" · ");
}
// Avant d'envoyer / d'installer : si le système déclaré n'est pas atteint, on montre le constat et ses preuves.
async function systemOk(path, body, title) {
  let j;
  try { j = await post(path, body); } catch (e) { return true; }  // inconnu : on n'empêche rien
  if (!j || j.ok !== false) return true;
  const who = j.console || T("cette console");
  let note = T("« %s » demande le système %s ; %s est en %s.", title, j.required, who, j.firmware);
  if (j.state === "backport_present") note += " " + T("Il faut un backport, et il existe : %s.", compatBackportText(j));
  else if (j.state === "no_backport") note += " " + T("Aucun backport trouvé : le jeu risque de ne pas fonctionner. PS5 Center pourra aider (fonctions expérimentales de backport).");
  return ask(compatStateText(j), `<p class="hint">${esc(note)}</p>`, T("Continuer quand même"));
}

// Format d'un jeu installé (envoi à une PS5, installation depuis une copie), comme sur la télé.
function chooseFormat(anchor, then) {
  setTimeout(() => openMenu(anchor, [
    { label: T("Automatique (selon le système de la console)"), run: () => then("auto") },
    { label: T("Compressé (.ffpfsc) · prend moins de place"), run: () => then("ffpfsc") },
    { label: T("Image non compressée (.exfat) · pour les anciens systèmes"), run: () => then("exfat") },
    { label: T("Tel quel (comme la copie)"), run: () => then("keep") },
  ]), 0);
}

function openMenu(anchor, actions) {
  closeMenu();
  if (!actions.length) return;
  const m = document.createElement("div");
  m.className = "popmenu"; m.id = "popmenu"; m.setAttribute("role", "menu");
  m.innerHTML = actions.map((a, i) => `<button role="menuitem" data-mi="${i}" class="${a.cls || ""}" ${a.disabled ? "disabled" : ""}>${esc(a.label)}${a.hint ? `<small class="mi-hint">${esc(a.hint)}</small>` : ""}</button>`).join("");
  document.body.appendChild(m);
  const r = anchor.getBoundingClientRect(), w = m.offsetWidth, h = m.offsetHeight;
  m.style.left = Math.max(8, Math.min(r.right - w, innerWidth - w - 8)) + "px";
  m.style.top = (r.bottom + 6 + h > innerHeight - 8 ? Math.max(8, r.top - h - 6) : r.bottom + 6) + "px";
  $$("[data-mi]", m).forEach((b) => b.onclick = (e) => { e.stopPropagation(); closeMenu(); actions[+b.dataset.mi].run(); });
  const first = $("button:not(:disabled)", m);
  if (first) first.focus({ preventScroll: true });
}
function closeMenu() { const m = $("#popmenu"); if (m) m.remove(); }
document.addEventListener("pointerdown", (e) => { const m = $("#popmenu"); if (m && !m.contains(e.target)) m.remove(); }, true);
window.addEventListener("scroll", closeMenu, true);
window.addEventListener("resize", closeMenu);

// ---- Appairage --------------------------------------------------------------------
// Le panneau n'est préparé (focus sur le code) qu'à sa première apparition : tant qu'il est affiché,
// plus aucun appel au moteur, et rien ne vient voler le focus du champ en cours de saisie.
function showPair() {
  const p = $("#pair");
  if (!p.classList.contains("hidden")) return;
  p.classList.remove("hidden");
  $("#pair-error").textContent = "";
  $("#pair-code").focus();
}
$("#pair-go").onclick = async () => {
  const code = $("#pair-code").value.replace(/\D/g, ""), name = $("#pair-name").value.trim() || navigator.platform || T("Navigateur");
  if (code.length !== 6) { $("#pair-error").textContent = T("Le code a 6 chiffres."); $("#pair-code").focus(); return; }
  $("#pair-go").disabled = true;
  try {
    const r = await fetch("/api/v2/pair", { method: "POST", body: JSON.stringify({ code, name }) });
    const d = await r.json();
    if (!r.ok) throw new Error(d.error || T("erreur %d", r.status));
    state.token = d.token; localStorage.setItem("pbc-token", d.token);
    $("#pair").classList.add("hidden"); refresh(true);
  } catch (e) { $("#pair-error").textContent = e.message; }
  $("#pair-go").disabled = false;
};
$("#pair-code").addEventListener("keydown", (e) => { if (e.key === "Enter") $("#pair-name").focus(); });
$("#pair-name").addEventListener("keydown", (e) => { if (e.key === "Enter") $("#pair-go").click(); });

// ---- Données ------------------------------------------------------------------------
async function refresh(force = false) {
  if (!$("#pair").classList.contains("hidden")) return;  // en attente d'appairage : pas d'appels
  if (force) state.revision = -1;
  if (refresh.busy) { if (force) refresh.again = true; return; }
  refresh.busy = true;
  try {
    const st = await (await fetch("/api/v2/status")).json();
    state.status = st;
    const eng = $("#engine-state");
    eng.className = "engine ok";
    // Mise à jour installée pendant que la page est ouverte : on recharge (sinon on garde l'ancienne page).
    // Si une fenêtre est ouverte (saisie en cours), on attend qu'elle soit fermée.
    if (st.version) {
      if (!window.pbcVersion) window.pbcVersion = st.version;
      else if (window.pbcVersion !== st.version && !document.querySelector(".modal")) {
        try { sessionStorage.setItem("pbcUpdatedAt", String(Date.now())); } catch (e) {}  // pastille verte une minute après le rechargement
        location.reload(); return;
      }
    }
    eng.querySelector(".label").textContent = st.scanning ? T("Actualisation…") : `v${st.version}`;
    eng.title = T("Moteur %s", st.version);
    if (!state.token) { showPair(); return; }
    if (st.revision === state.revision) return;
    const rev = st.revision;
    const [items, places, tasks, emulators, settings, remotes] = await Promise.all([api("/items"), api("/places"), api("/tasks"), api("/emulators"), api("/settings"),
      api("/remotes").catch(() => state.remotes)]);
    for (const p of places) p.label = engineLabel(p.label);
    for (const i of items) for (const c of i.components || []) c.label = engineLabel(c.label);
    Object.assign(state, { items, places, tasks: tasks.tasks, emulators, settings, remotes, loaded: true });
    if (settings.theme) applyTheme(settings.theme);
    // La langue choisie sur la télé (ou une autre page) suit ici aussi, sans attendre un rechargement manuel.
    const lang = settings.language || "auto";
    if (I18N.setting && lang !== I18N.setting && !document.querySelector(".modal")) {
      try { localStorage.setItem("pbc-lang", lang); } catch (e) {}
      location.reload();
      return;
    }
    state.revision = rev;
    const active = state.tasks.filter((t) => ["queued", "running", "paused"].includes(t.state)).length;
    $("#tasks-count").textContent = active || "";
    render();
  } catch (e) {
    if (e.message === T("non associé")) return;
    const eng = $("#engine-state"); eng.className = "engine down"; eng.querySelector(".label").textContent = T("Moteur injoignable");
  } finally { refresh.busy = false; if (refresh.again) { refresh.again = false; refresh(true); } }
}
setInterval(() => refresh(), 1000);

// ---- Navigation -----------------------------------------------------------------------
// Recherche sans majuscules ni accents (« pokemon » trouve « Pokémon »).
const fold = (s) => (s || "").normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase();
// Loupe de l'en-tête : ouvre la Bibliothèque avec le champ de recherche (aussi la touche « / »).
function openSearch() { setTab("library"); setTimeout(() => { const q = $("#f-q"); if (q) { q.focus(); q.select(); } }, 50); }
document.addEventListener("keydown", (e) => { if (e.key === "/" && !/INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)) { e.preventDefault(); openSearch(); } });
// « 12,3 Mo sur 27 Mo · fichier 5 sur 23 · 1,2 Mo/s · reste 10 s » (comme la télé).
function updateProgress(u) {
  let t = T("%s sur %s", size(u.bytes_done), size(u.bytes_total));
  if (u.file_index && u.files) t += " · " + T("fichier %d sur %d", u.file_index, u.files);
  if (u.bytes_per_second) t += " · " + size(u.bytes_per_second) + "/s";
  if (u.eta_seconds) t += " · " + T("reste") + " " + duration(u.eta_seconds);
  return t;
}
// Mises à jour : recherche à l'ouverture de la page et à chaque entrée dans Paramètres (le moteur ignore si la
// dernière date de moins d'une minute, ou si « Rechercher automatiquement » est à Non).
function autoCheckUpdates() { if (state.token) post("/update/check", { auto: true }).catch(() => {}); }
function setTab(tab) {
  if (tab === "settings" && state.tab !== "settings") autoCheckUpdates();
  state.tab = tab;
  $$("[data-tab]").forEach((x) => x.classList.toggle("active", x.dataset.tab === tab));
  const b = $(`#tabs [data-tab=${tab}]`);
  if (b) b.scrollIntoView({ block: "nearest", inline: "nearest" });
  window.scrollTo(0, 0);
  render(true);
}
$$("[data-tab]").forEach((b) => b.onclick = () => { mg.back = ""; if (b.dataset.tab === state.tab && state.tab === "backups") mg.game = ""; setTab(b.dataset.tab); });
$("#search-btn").onclick = openSearch;
function render(force = false) {
  // Ne pas effacer un champ en cours de saisie quand le moteur change d'état.
  const a = document.activeElement;
  if (!force && $("#popmenu")) return;  // un menu « ⋯ » ouvert n'est pas refermé par une mise à jour du moteur
  if (!force && a && ((a.tagName === "INPUT" && !["checkbox", "radio", "button"].includes(a.type)) || a.tagName === "SELECT" || a.tagName === "TEXTAREA") && $("#view").contains(a)) return;
  if (!state.loaded && state.tab !== "settings") { $("#view").innerHTML = loading(T("Chargement de votre collection…")); return; }
  // Les rangées défilantes gardent leur position quand la page est redessinée.
  const scrolls = {};
  $$("[data-scroll]").forEach((el) => { scrolls[el.dataset.scroll] = el.scrollLeft; });
  closeMenu();
  selBar(0);  // chaque écran remet sa barre d'actions s'il a une sélection
  ({ home, library, saves: partiesView, backups: backupsTab, synchro: synchroView, transfers, devices: devicesView, tasks, settings })[state.tab]();
  $$("[data-scroll]").forEach((el) => { if (scrolls[el.dataset.scroll]) el.scrollLeft = scrolls[el.dataset.scroll]; });
}

// ---- Jaquettes ----------------------------------------------------------------------
// Libellés écrits en français par le moteur (composants, stockage interne) : traduits à l'affichage.
function engineLabel(l) {
  if (typeof l !== "string") return l;
  if (l.startsWith("Sauvegarde · ")) return T("Sauvegarde") + " · " + l.slice(13);
  const m = /^Jeu \(image (.*)\)$/.exec(l);
  if (m) return T("Jeu (image %s)", m[1]);
  return { Jeu: T("Jeu"), Configuration: T("Configuration"), Interne: T("Interne"), "Sauvegarde (comptes Switch)": T("Sauvegarde (comptes Switch)") }[l] || l;
}
function placeLabel(id) { const p = state.places.find((x) => x.id === id); return p ? p.label : T("Disque absent"); }
function placeholderHtml(it) {
  // Pas de jaquette : une carte bleu-violet, la même pour toutes les consoles, avec le titre.
  return `<div class="placeholder" style="background:linear-gradient(160deg, #5b4fd0, #2f5fc4 55%, #142a5e)"><span>${esc(cleanTitle(it.title || "?"))}</span></div>`;
}
const coverUrl = (it, px) => `/api/v2/cover?key=${encodeURIComponent(it.key)}&size=${px}&token=${encodeURIComponent(state.token)}`;
function kindLabel(it) { return it.kind === "rom" ? it.platform : KINDS[it.kind] || it.kind; }
const SHORTS = { "Nintendo 64": "N64", "Mega Drive": "MD", Genesis: "MD", PlayStation: "PS1", "PlayStation 2": "PS2", "PlayStation 3": "PS3", "Super Nintendo": "SNES", SNES: "SNES",
  NES: "NES", "Game Boy": "GB", "Game Boy Color": "GBC", "Game Boy Advance": "GBA", "Nintendo DS": "DS", "Nintendo 3DS": "3DS", GameCube: "GC", Wii: "Wii", "Wii U": "Wii U",
  Dreamcast: "DC", Saturn: "Saturn", "Master System": "SMS", "PC Engine": "PCE", "PC Engine CD": "PCE CD", "Atari 2600": "2600", Arcade: "Arcade", "Neo Geo": "Neo Geo",
  Switch: "Switch", "Switch 2": "Switch 2", "Xbox 360": "X360", WonderSwan: "WS", "WonderSwan Color": "WSC" };
// Couleur de la famille : bleu PlayStation, rouge Nintendo, turquoise Sega, violet appli, ambre le reste.
function familyBadge(it) {
  if (it.kind === "app") return ["App", "#7c5ce6"];
  if (it.kind === "game_ps4") return ["PS4", "#00439c"];
  if (it.kind !== "rom") return ["PS5", "#0070d1"];
  const p = it.platform || "";
  const nintendo = p.includes("Nintendo") || ["SNES", "NES", "GameCube"].includes(p) || p.startsWith("Switch") || p.startsWith("Game Boy") || p.startsWith("Wii");
  const sega = ["Mega Drive", "Genesis", "Dreamcast", "Saturn", "Master System", "Game Gear"].includes(p);
  const sony = p.startsWith("PlayStation") || p === "PSP";
  return [SHORTS[p] || p || "ROM", nintendo ? "#e4000f" : sega ? "#0096aa" : sony ? "#005ab4" : "#d68414"];
}
function coverHtml(it, big = false) {
  const ph = placeholderHtml(it).replace(/'/g, "&#39;");
  const img = it.has_cover ? `<img loading="lazy" src="${coverUrl(it, big ? 512 : 384)}" onerror="this.outerHTML='${ph.replace(/"/g, "&quot;")}'">` : placeholderHtml(it);
  const [short, tone] = familyBadge(it);
  const badges = big ? "" : `<div class="badges"><span class="badge" style="background:${tone}">${esc(short)}</span></div>`;
  return `<div class="cover">${img}${badges}</div>`;
}
// Jaquette : un clic ouvre la fiche ; avec « sel » (Bibliothèque), une case à cocher et un menu « ⋯ » sur la jaquette.
// « menu » (Accueil) : seulement le menu « ⋯ » (Ouvrir la fiche, Toutes les versions, Ignorer…), sans case à cocher.
const tileHtml = (it, sel = null, menu = false) => { const on = !!sel && sel.has(it.key);
  return `<div class="tile${it.ignored ? " ignored" : ""}${on ? " marked" : ""}${sel ? " selectable" : ""}" data-key="${esc(it.key)}" tabindex="0" role="button">${coverHtml(it)}${it.ignored ? `<span class="ign-badge">${T("Ignoré")}</span>` : ""}
  ${sel ? `<span class="tile-ck">${ckHtml(`data-tk="${esc(it.key)}"`, on)}</span>${moreBtn(`data-tm="${esc(it.key)}"`)}` : menu ? moreBtn(`data-tm="${esc(it.key)}"`) : ""}
  <div class="name">${esc(cleanTitle(it.title))}</div><div class="meta">${esc(placeLabel(it.place))}${it.apps.length ? " · " + esc(it.apps.join(", ")) : ""}</div></div>`; };
function bindTiles(root = document) {
  $$(".tile[data-key]", root).forEach((t) => {
    const open = (e) => {
      if (e.target.closest(".ck, .more, .tile-ck")) return;
      const it = state.items.find((i) => i.key === t.dataset.key);
      if (it) openItem(it);
    };
    t.onclick = open;
    t.onkeydown = (e) => { if (e.key === "Enter" && e.target === t) open(e); };
  });
}

// ---- Accueil ----------------------------------------------------------------------------
// Comme la télé : un carrousel des jeux à sauvegarder (jamais sauvegardés, ou joués depuis), puis des rangées.
async function loadOverview(force = false) {
  if (!force && state.overview && Date.now() - state.overviewAt < 15000) return state.overview;
  if (loadOverview.p) return loadOverview.p;
  loadOverview.p = api("/saves/overview").then((o) => { state.overview = o; }).catch(() => { state.overview = state.overview || { accounts: [], entries: [] }; })
    .finally(() => { state.overviewAt = Date.now(); loadOverview.p = null; });
  await loadOverview.p;
  return state.overview;
}
const entries = () => (state.overview && state.overview.entries) || [];
function heroItems() {
  const out = [], add = (it) => { if (it && it.has_cover && !it.ignored && out.length < 5 && !out.includes(it)) out.push(it); };
  const find = (k) => state.items.find((i) => i.key === k);
  for (const want of ["console_newer", "not_backed_up"])
    for (const e of entries()) if (e.status === want) { const it = find(e.item); if (it && it.kind.startsWith("game")) add(it); }
  for (const i of state.items) if (i.kind.startsWith("game")) add(i);
  for (const i of state.items) add(i);
  return out;
}
// « À faire » : cartes illustrées (doublons à libérer → Backups ; copies à vérifier → Backups, vue À vérifier ;
// tâche en cours → Tâches). Jamais d'élément ignoré.
function homeTodo() {
  const cards = [], s = mg.sum;
  const ignored = (key) => { const it = state.items.find((i) => i.key === key); return !!(it && it.ignored); };
  const goBackups = (view) => () => { mg.place = ""; mg.view = view; mg.game = ""; mg.back = ""; setTab("backups"); };
  // À sauvegarder (comme la télé) : les jeux de la console joués depuis le dernier backup, ou jamais sauvegardés.
  const ov = state.overview, me = ov && (ov.accounts || []).find((a) => a.on_console && a.platform === "PS5");
  // Centre de notifications : une bulle par sorte de chose à faire, avec le nombre ; la jaquette et le nom des jeux
  // concernés défilent dans la bulle (toutes les 3 s). Mêmes bulles que la télé.
  const goSave = () => { state.party.player = ""; state.party.view = "todo"; state.party.sel.clear(); setTab("saves"); };
  const save = me ? ov.entries.filter((e) => e.account === me.key && (e.status === "console_newer" || e.status === "not_backed_up") && !ignored(e.item)).map((e) => ({ item: e.item, title: e.title })) : [];
  if (save.length) cards.push({ eyebrow: T("À SAUVEGARDER"), title: T("%d jeu(x)", save.length), tone: "warn", games: save, go: goSave });
  // Copie plus récente que la partie sur la console (jouée ailleurs) : à restaurer ? (comme la télé)
  const onAcc = ov ? (ov.accounts || []).filter((a) => a.on_console).map((a) => a.key) : [];
  const newer = ov ? ov.entries.filter((e) => onAcc.includes(e.account) && e.status === "backup_newer" && !ignored(e.item)).map((e) => ({ item: e.item, title: e.title })) : [];
  if (newer.length) cards.push({ eyebrow: T("COPIE PLUS RÉCENTE"), title: T("%d jeu(x) à restaurer ?", newer.length), tone: "info", games: newer,
    go: () => { state.party.player = ""; state.party.view = "backup_newer"; state.party.sel.clear(); setTab("saves"); } });
  if (s) {
    const st = mgStats(""), live = s.games.filter((x) => !ignored(x.item));
    if (st.dup) cards.push({ eyebrow: T("SAUVEGARDES · DOUBLONS"), title: T("%s à libérer", size(st.freed)), tone: "ok",
      games: live.filter((x) => (x.remove || []).length).sort((a, b) => (b.freed_bytes || 0) - (a.freed_bytes || 0)), go: goBackups(1) });
    if (st.cmp) cards.push({ eyebrow: T("SAUVEGARDES · PAS ENCORE COMPARÉES"), title: T("%d jeu(x)", st.cmp), tone: "info", games: live.filter((x) => !x.compared && !x.full), go: goBackups(2) });
    if (st.chk) cards.push({ eyebrow: T("SAUVEGARDES · À VÉRIFIER"), title: T("%d copie(s) à vérifier", st.chk), tone: "bad", games: live.filter((x) => x.copies.some(mgSuspect)), go: goBackups(3) });
  }
  {  // Mise à jour de Backup Center (comme la télé) : en cours, ou installée mais pas encore en marche
    const u = (state.status || {}).update || {}, from = u.received_from ? T("reçue de %s", u.received_from) : "";
    if (u.state === "downloading" || u.state === "installing")
      cards.push({ eyebrow: T("MISE À JOUR"), title: u.state === "downloading" && u.bytes_total ? T("Téléchargement… %d %%", Math.min(100, Math.floor((u.bytes_done * 100) / u.bytes_total))) : u.state === "downloading" ? T("Téléchargement…") : T("Installation…"), tone: "info",
        games: [{ item: "", logo: true, title: u.state === "downloading" && u.bytes_total ? updateProgress(u) : from || T("Backup Center se met à jour") }], go: () => setTab("settings") });
    else if (u.state === "available" && u.available)  // trouvée par la recherche automatique ou manuelle
      cards.push({ eyebrow: T("MISE À JOUR"), title: T("%s disponible", u.available), tone: "info", games: [{ item: "", logo: true, title: T("Cliquer pour installer") }],
        go: async () => { try { await post("/update/install", {}); toast(T("Téléchargement de la mise à jour…")); refresh(true); } catch (e) { toast(e.message); } } });
    else if (u.engine_restart_needed && u.installed)
      cards.push({ eyebrow: T("MISE À JOUR"), title: T("%s installée", u.installed), tone: "ok", games: [{ item: "", logo: true, title: (from ? from + " · " : "") + T("Relancer le moteur") }],
        go: async () => { try { await post("/update/restart-engine", {}); toast(T("Relance du moteur…")); } catch (e) { toast(T("Relance refusée : %s — la nouvelle version sera active au prochain démarrage de la console", e.message)); } } });
  }
  // Synchro des copies (comme la télé) : en cours, sinon l'état avec chaque autre PS5.
  if (typeof syncState !== "undefined") {
    const ss = syncState.status;
    if (ss.state === "running") cards.push({ eyebrow: T("SYNCHRO DES SAUVEGARDES"), title: T("%d / %d copie(s)", ss.done || 0, ss.total || 0), tone: "task-move",
      games: [{ item: "", title: ss.current || ss.name || "", progress: ss.total ? Math.floor(((ss.done || 0) * 1000) / ss.total) : -1, eta: "" }], go: () => setTab("backups") });
    else if (syncState.plans.length) {  // une seule carte Synchro : à mettre à niveau d'abord, puis au même niveau, puis une ligne pour les autres
      let need = 0, first = null, sameFirst = null;
      const games = [], same = [], idle = [];
      for (const p of syncState.plans) {
        const titles = p.plan.titles || [], chosen = titles.filter((t) => t.followed).length, n = titles.filter(syNeeds);
        if (!chosen) { idle.push({ item: "", dev: String(p.plan.peer || "").startsWith("remote:") ? "nas" : "ps5", title: T("%s · aucun jeu choisi", p.name) }); continue; }
        if (n.length) { need += n.length; first = first || p; for (const t of n) if (games.length < 12) games.push({ item: "title:" + t.title_id, title_id: t.title_id, title: (t.state === "pull" ? "↓ " : "↑ ") + t.title + " · " + p.name }); continue; }
        same.push({ item: "", dev: String(p.plan.peer || "").startsWith("remote:") ? "nas" : "ps5", title: T("%s : les mêmes sauvegardes des deux côtés", p.name) });
        sameFirst = sameFirst || p;
      }
      const idleLine = idle;  // chaque appareil défile avec son image
      const head = syncState.plans.length > 1 ? T("SYNCHRO · %d appareils", syncState.plans.length) : T("SYNCHRO");
      const open = (p) => () => { if (p) SY.dev = p.plan.peer; SY.pick = null; setTab("synchro"); };
      if (need) cards.push({ eyebrow: head, title: T("%d jeu(x) à mettre à niveau", need), tone: "warn", games: [...games, ...idleLine], go: open(first) });
      else if (same.length) cards.push({ eyebrow: head, title: T("Tout est au même niveau"), tone: "ok", games: [...same, ...idleLine], go: open(sameFirst) });
      else cards.push({ eyebrow: head, title: T("Aucun jeu choisi"), tone: "info", games: idleLine, go: open(syncState.plans[0]) });
    }
  }
  // Tâches : une bulle par sorte (compression, transfert, décompression…), en cours d'abord, avec l'avancement.
  const live = state.tasks.filter((t) => t.state === "running" || t.state === "queued" || t.state === "paused");
  for (const kind of [...new Set(live.map((t) => TASK_KINDS[t.type] || T("Tâche")))]) {
    const mine = live.filter((t) => (TASK_KINDS[t.type] || T("Tâche")) === kind), run = mine.filter((t) => t.state === "running"), wait = mine.filter((t) => t.state !== "running");
    const ty = mine[0].type, fam = ["transfer", "import_switch", "export_switch", "link_send"].includes(ty) ? "task-move" : ["compress", "uncompress", "make_image", "extract_image", "validate_image"].includes(ty) ? "task-pack" : "task";
    cards.push({ eyebrow: kind, title: !run.length ? T("%d en attente", wait.length) : wait.length ? T("%d en cours", run.length) + " · " + T("%d en attente", wait.length) : T("%d en cours", run.length), tone: fam,
      games: [...run, ...wait].map((t) => ({ item: t.item || "", title: cleanTitle((t.title || "").split(" · ").pop()) || kind,
        progress: taskPhase(t) ? Math.floor(taskPhase(t).f * 1000) : t.state === "running" && t.progress && t.progress.bytes_total ? Math.floor((Math.min(t.progress.bytes_done, t.progress.bytes_total) * 1000) / t.progress.bytes_total) : -1,
        phase: taskPhase(t) ? taskPhase(t).text : "",
        eta: taskPhase(t) ? "" : t.state === "running" && t.bytes_per_second > 0 && t.progress && t.progress.bytes_total ? duration((t.progress.bytes_total - t.progress.bytes_done) / t.bytes_per_second) : "" })),
      go: () => setTab("tasks") });
  }
  return cards;
}
const todoName = (g) => ("progress" in g ? g.phase ? g.phase + " · " : (g.progress >= 0 ? `${Math.floor(g.progress / 10)} % · ` + (g.eta ? T("reste") + " " + g.eta + " · " : "") : T("En attente") + " · ") : "") + cleanTitle(g.title);
const todoBar = (g) => ("progress" in g ? `<span class="todo-bar"><i style="width:${Math.max(2, g.progress / 10)}%"></i></span>` : "");
const todoImg = (g) => (g.logo ? `<span class="logo big" aria-hidden="true"></span>` : g.item ? mgCover(g) : `<span class="dev-ico">${g.dev === "nas" ? ICONS.nas : ICONS.ps5}</span>`);  // mise à jour : notre logo
const todoDots = (n, at) => (n > 1 ? `<span class="todo-dots">${Array.from({ length: Math.min(n, 8) }, (_, d) => `<i class="${d === at % 8 ? "on" : ""}"></i>`).join("")}</span>` : "");
// Toutes les 3 s : chaque bulle « À faire » passe au jeu suivant (jaquette, nom, points), sans redessiner la page.
setInterval(() => {
  if (state.tab !== "home" || !state.todo) return;
  // Synchro des copies en cours : l'avancement de la bulle suit (relu toutes les 2 s par loadSyncState).
  if (typeof syncState !== "undefined" && syncState.status.state === "running")
    loadSyncState().then(() => { if (state.tab === "home") home(); });
  const fresh = homeTodo();  // avancement des tâches à jour
  if (fresh.length === state.todo.length) state.todo = fresh;
  const tick = Math.floor(Date.now() / 3000);
  $$("[data-todo]").forEach((b) => {
    const c = state.todo[+b.dataset.todo];
    if (!c || !c.games || (c.games.length < 2 && !("progress" in c.games[0]))) return;
    const at = tick % c.games.length, g = c.games[at];
    if (b.dataset.shown !== g.item + "|" + g.title) { b.dataset.shown = g.item + "|" + g.title; b.querySelector(".todo-img").innerHTML = todoImg(g); }  // jaquette : seulement si le jeu change
    b.querySelector(".todo-name").textContent = todoName(g);
    const bar = b.querySelector(".todo-bar");
    if (bar) bar.outerHTML = todoBar(g);
    const dots = b.querySelector(".todo-dots");
    if (dots) dots.outerHTML = todoDots(c.games.length, at);
  });
}, 3000);
// Rangées par catégorie de l'Accueil (comme avant le nouveau découpage, mêmes rangées que la télé) : jeux, une rangée
// par console rétro, applications, jeux dont il ne reste que la sauvegarde.
function categoryShelves() {
  const games = { title: T("Jeux PS5"), items: [] }, games4 = { title: T("Jeux PS4"), items: [] }, apps = { title: T("Applications"), items: [] }, orphans = { title: T("Sauvegardes sans le jeu"), items: [] };
  const retro = new Map();
  for (const i of state.items) {
    if (i.ignored) continue;
    if (i.key.startsWith("save:")) { if (i.kind !== "app") orphans.items.push(i); continue; }
    if (i.kind === "game_ps4") games4.items.push(i);
    else if (i.kind.startsWith("game")) games.items.push(i);
    else if (i.kind === "app") apps.items.push(i);
    else if (i.kind === "rom") { if (!retro.has(i.platform)) retro.set(i.platform, { title: i.platform, items: [] }); retro.get(i.platform).items.push(i); }
  }
  const later = [...[...retro.values()].sort((a, b) => b.items.length - a.items.length), apps].map((x) => ({ ...x, later: true }));
  return [games, games4, orphans, ...later].filter((s) => s.items.length);
}
// Jeux installés sur les PS5 jumelées et absents d'ici (comme la télé) : relus toutes les 30 s.
let peerGames = [], peerGamesAt = 0;
async function loadPeerGames() {
  if (Date.now() - peerGamesAt < 30000) return;
  peerGamesAt = Date.now();
  try {
    peerGames = await api("/link/peer-games");
    if (state.tab === "home") home();
    if (state.tab === "synchro") { syncState.plansAt = 0; loadSyncState().then(() => { if (state.tab === "synchro" && !SY.pick) synchroDraw(); }); }
  } catch (e) { /* liaison absente : rien */ }
}
// Recevoir ici un jeu de l'autre PS5 : disque d'ici, format, compte (comme la télé).
function receivePeerGame(anchor, peer, g) {
  const disks = state.places.filter((p) => p.type === "internal" || p.type === "usb");
  if (!disks.length) { toast(T("Aucun disque pour recevoir ce jeu")); return; }
  openMenu(anchor, disks.map((p) => ({ label: T("Recevoir sur %s · %s libres", p.type === "internal" ? T("Interne") : p.label, size(p.free)),
    hint: T("Installé sur %s, %s. Il sera copié ici et installé ; sa sauvegarde suit, sur le compte choisi.", peer.name, size(g.bytes || 0)),
    run: () => chooseFormat(anchor, (format) => pickAccount(anchor, "", T("cette console"), async (uid) => {
      try { await post("/link/pull-game", { peer: peer.instance, item: g.key, place: p.id, format, user_id: uid }); toast(T("« %s » arrive de %s (suivi dans Tâches) ; le jeu apparaît sur l'accueil une fois l'application fermée", g.title, peer.name)); }
      catch (e) { toast(e.message); }
    })) })));
}
// ---- Onglet Synchro (comme la télé) ----
// À gauche les appareils ; à droite où on en est et UN bouton « Mettre à niveau » ; dessous des sections séparées
// (Sauvegardes active ; Parties, Applis, DLC, Réglages bientôt). « Choisir les jeux » : un interrupteur par jeu.
const SY = { dev: "", pick: null, filter: 0, sort: 0, versions: false, vsel: new Set() };
// Un jeu choisi a-t-il quelque chose à échanger ? (sauvegardes, ou le jeu entier une seule fois s'il est demandé)
const syNeeds = (t) => t.followed && (t.pull || t.push || (t.send_full && (t.full_push_bytes || t.full_pull_bytes)));
function syDay(ts) {
  if (!ts) return "";
  const d = new Date(ts * 1000), now = new Date(), hm = d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  const day = (x) => Math.floor((x - new Date(x).getTimezoneOffset() * 60000) / 86400000);
  if (day(d) === day(now)) return T("aujourd'hui %s", hm);
  if (day(d) + 1 === day(now)) return T("hier %s", hm);
  return d.toLocaleDateString([], { day: "2-digit", month: "2-digit" }) + " " + hm;
}
const syPlay = (s) => (s >= 3600 ? T("%d h de jeu", Math.floor(s / 3600)) : s >= 60 ? T("%d min de jeu", Math.floor(s / 60)) : "");
function synchroView() {
  loadPeerGames();
  loadSyncState().then(() => { if (state.tab === "synchro" && !SY.pick) synchroDraw(); });
  synchroDraw();
}
// Deux synchros sur le même NAS (deux entrées du même nom) : « NAS (WebDAV) », et le dossier si le protocole est le même.
function nasSyncName(p) {
  const proto = (x) => (x.protocol === "webdav" ? "WebDAV" : x.protocol === "smb" ? "SMB" : "FTP");
  const twins = state.places.filter((o) => o.id !== p.id && o.type === "ftp" && o.use !== "backups" && !o.same_as && o.label === p.label);
  if (!twins.length) return p.label;
  const folder = (p.mount.match(/^\w+:\/\/[^/]*(\/.*)$/) || [, "/"])[1];
  return `${p.label} (${proto(p)})` + (twins.some((o) => proto(o) === proto(p)) ? " · " + folder : "");
}

function synchroDevices() {
  // PS5 jumelées : celles déjà lues, et celles dont on connaît les disques (présentes même avant la première lecture).
  const ps5 = peerGames.map((p) => ({ kind: "ps5", id: p.instance, name: p.name }));
  for (const pl of state.places.filter((x) => x.type === "peer")) {
    const inst = pl.id.slice(5).split(":")[0];
    if (!ps5.some((d) => d.id === inst)) ps5.push({ kind: "ps5", id: inst, name: pl.label.split(" (")[0].split(" · ")[0] });
  }
  return [...ps5,
    ...state.remotes.filter((r) => r.type === "switch").map((r) => ({ kind: "switch", id: r.id, name: r.name })),
    ...state.places.filter((p) => p.type === "ftp" && p.use !== "backups" && !p.same_as).map((p) => ({ kind: "nas", id: p.id, name: nasSyncName(p) }))];  // NAS qui sert à la synchro (un dossier une fois)
}
const syPlanOf = (id) => (syncState.plans.find((p) => p.plan.peer === id) || {}).plan;
async function syPrefs(body, ok) {
  try { await post("/sync/copies/prefs", body); if (ok) toast(ok); } catch (e) { toast(e.message); }
  syncState.plansAt = 0;
  if (SY.pick) await synchroOpenPick(body.peer); else { await loadSyncState(); if (state.tab === "synchro") synchroDraw(); }
}
async function syStart(peer, name, extra) {
  try { await post("/sync/copies/start", { peer, include_full: true, ...extra }); toast(T("Mise à niveau avec %s lancée (suivie sur l'accueil)", name)); syncState.at = 0; loadSyncState(); }
  catch (e) { toast(e.message); }
}
function synchroDraw() {
  if (SY.pp) { partiesDraw(); return; }
  if (SY.pick) { synchroPickDraw(); return; }
  const devs = synchroDevices();
  const head = `<h1>${T("Synchro")}</h1><p class="hint tab-help">${esc(T("Vos appareils au même niveau, sans jamais rien recopier deux fois."))}</p>`;
  if (!devs.length) { $("#view").innerHTML = head + `<p class="hint">${esc(T("Aucun appareil avec qui se synchroniser : jumelez une autre PS5 (Appareils) ou ajoutez une Switch."))}</p>`; return; }
  if (!devs.some((d) => d.id === SY.dev)) SY.dev = devs[0].id;
  const d = devs.find((x) => x.id === SY.dev);
  const count = (pl) => { const t = (pl && pl.titles) || []; return { chosen: t.filter((x) => x.followed).length, need: t.filter(syNeeds) }; };
  const list = devs.map((x) => {
    let sub = T("Switch · avec Eden"), tone = "";
    if (x.kind === "nas" && !syPlanOf(x.id)) sub = T("NAS · lecture…");
    if (x.kind !== "switch") { const pl = syPlanOf(x.id); if (!pl) sub = x.kind === "nas" ? T("NAS · lecture…") : T("lecture…"); else { const c = count(pl); sub = !c.chosen ? T("aucun jeu choisi") : c.need.length ? T("%d à mettre à jour", c.need.length) : T("au même niveau"); tone = !c.chosen ? "" : c.need.length ? "warn" : "ok"; } }
    return `<button class="sy-dev ${x.id === SY.dev ? "chosen" : ""}" data-sy="${esc(x.id)}"><span class="dev-ico">${x.kind === "switch" ? ICONS.switch : x.kind === "nas" ? ICONS.nas : ICONS.ps5}</span><span class="grow"><b>${esc(x.name)}</b><span class="hint ${tone}">${esc(sub)}</span></span></button>`;
  }).join("") + `<button class="sy-card" id="sy-games"><b>${esc(T("Jeux entiers ›"))}</b><p class="hint">${esc(T("Copier, compresser, envoyer un jeu : c'est l'onglet Transferts."))}</p></button>`;
  // Où on en est, et un bouton
  let eyebrow = T("AVEC %s", d.name), title = "", sub = "", btn = T("Mettre à niveau"), last = "", tone = "";
  const st = syncState.status, running = d.kind !== "switch" && st.peer === d.id && st.state === "running";
  const pl = d.kind !== "switch" ? syPlanOf(d.id) : null, c = count(pl);
  if (d.kind === "switch") { eyebrow = T("AVEC EDEN SUR CETTE PS5"); title = T("Les parties de la Switch et d'Eden"); sub = T("La plus récente gagne, jeu par jeu : la synchro Switch, ouverte d'ici."); btn = T("Ouvrir"); }
  else if (!pl) { title = d.kind === "nas" ? T("Lecture du NAS…") : T("Lecture de l'autre PS5…"); sub = d.kind === "nas" ? T("Il doit être allumé et joignable.") : T("Elle doit être allumée, sur le même réseau."); btn = ""; }
  else if (running) { title = T("Mise à niveau en cours : %lld / %lld", st.done || 0, st.total || 0); sub = st.current || ""; btn = ""; }
  else if (!c.chosen) { title = T("Aucun jeu choisi"); sub = d.kind === "nas" ? T("Choisissez les jeux dont les sauvegardes sont aussi gardées sur le NAS (et inversement).") : T("Choisissez les jeux dont les sauvegardes sont copiées sur les deux PS5."); btn = T("Choisir les jeux"); tone = "muted"; }
  else if (!c.need.length) { title = T("Tout est au même niveau"); sub = T("%d jeu(x) choisi(s) : les mêmes sauvegardes des deux côtés.", c.chosen); tone = "ok"; }
  else {
    const bytes = c.need.reduce((a, t) => a + (t.bytes || 0) + (t.send_full ? (t.full_push_bytes || 0) + (t.full_pull_bytes || 0) : 0), 0);
    title = T("%d jeu(x) à mettre à jour", c.need.length);
    sub = c.need.slice(0, 3).map((t) => t.title).join(", ") + (c.need.length > 3 ? "…" : "") + " · " + size(bytes);
  }
  if (d.kind !== "switch" && st.peer === d.id && !running && st.state)
    last = st.state === "done" ? T("Dernière fois : %s · tout s'est bien passé", syDay(st.finished)) : T("Dernière fois : %s · %lld erreur(s)", syDay(st.finished), (st.errors || []).length);
  const hero = `<div class="sy-hero"><div class="sy-pair"><span><span class="dev-ico big">${ICONS.ps5}</span><small>${esc(T("Cette PS5"))}</small></span><span class="sy-arrow">⇄</span>
      <span><span class="dev-ico big">${d.kind === "switch" ? ICONS.switch : d.kind === "nas" ? ICONS.nas : ICONS.ps5}</span><small>${esc(d.name)}</small></span></div>
    <div class="grow"><div class="sy-eb">${esc(eyebrow)}</div><h2 class="${tone}">${esc(title)}</h2><p class="hint">${esc(sub)}</p>
      ${running && st.total ? `<span class="todo-bar"><i style="width:${Math.floor(((st.done || 0) * 100) / st.total)}%"></i></span>` : ""}</div>
    <div class="sy-go">${btn ? `<button class="primary big" id="sy-main">${esc(btn)}</button>` : ""}${last ? `<small class="hint">${esc(last)}</small>` : ""}</div></div>`;
  let secs = "";
  if (d.kind === "switch") secs = `<div class="sy-grid one"><div class="sy-card soon" data-soon="${esc(T("Bientôt : Switch ↔ Switch (les parties passent par Eden sur cette PS5, la plus récente gagne)."))}"><div class="sy-h"><i style="--c:#aab9cd"></i><b>${esc(T("Switch ↔ Switch"))}</b><span class="sy-chip">${esc(T("Bientôt"))}</span></div><p class="hint">${esc(T("Deux Switch au même niveau, en passant par Eden sur cette PS5."))}</p></div></div>`;
  else {
    const titles = (pl && pl.titles) || [], shown = [...titles.filter(syNeeds), ...titles.filter((t) => t.followed && !syNeeds(t))];
    const badge = (t) => !syNeeds(t) ? `<em class="ok">✓</em>` : `<em>${t.state === "pull" ? "↓" : t.state === "both" ? "↕" : "↑"}</em>`;
    const chip = !pl ? "" : !c.chosen ? `<span class="sy-chip">${esc(T("aucun jeu choisi"))}</span>` : c.need.length ? `<span class="sy-chip warn">${esc(T("%d à mettre à jour", c.need.length))}</span>` : `<span class="sy-chip ok">${esc(T("au même niveau"))}</span>`;
    const soon = [[T("Parties actives"), T("Reprenez votre partie sur l'autre PS5 : la plus récente arrive, l'ancienne est gardée."), "#5fb2ff", T("Bientôt : reprenez votre partie sur l'autre PS5 ; la plus récente arrive, l'ancienne est gardée.")],
      [T("Applis"), T("Une appli mise à jour ici l'est aussi là-bas. Jamais de retour en arrière."), "#9b7cff", T("Bientôt : une appli mise à jour ici l'est aussi là-bas, jamais de retour en arrière.")],
      [T("DLC"), T("Seulement les extensions qui manquent de l'autre côté, rien d'autre."), "#ffb44d", T("Bientôt : seulement les extensions qui manquent de l'autre côté.")],
      [T("Réglages"), T("La configuration de certaines applis, au cas par cas."), "#aab9cd", T("Bientôt : la configuration de certaines applis, au cas par cas.")],
      [T("Émulateurs"), T("La progression la plus récente de ProsperoEden et d'Églantine arrive sur l'autre PS5."), "#4fd1be", ""]];
    const order = [0, 4, 1, 3, 2];  // ligne 1 : Parties, Émulateurs ; ligne 2 : Applis, Réglages ; ligne 3 : DLC (toute la largeur)
    secs = `<div class="sy-grid"><button class="sy-card big" id="sy-saves"><div class="sy-h"><i style="--c:var(--ok)"></i><b>${esc(T("Sauvegardes"))}</b>${chip}</div>
        <p class="hint">${esc((d.kind === "nas" ? T("Des copies de vos parties (onglet Sauvegardes), gardées sur cette PS5 et sur le NAS. Pas jouables telles quelles : on en restaure une pour jouer.") : T("Des copies de vos parties (onglet Sauvegardes), rangées sur les deux PS5. Pas jouables telles quelles : on en restaure une pour jouer. Ne touche pas aux parties actives.")))}</p>
        <div class="sy-covers">${shown.slice(0, 6).map((t) => `<span>${mgCover({ item: "title:" + t.title_id, title_id: t.title_id, title: t.title, kind: t.kind })}${badge(t)}</span>`).join("")}${shown.length > 6 ? `<span class="more">+${shown.length - 6}</span>` : ""}${pl && !shown.length ? `<span class="hint">${esc(T("Aucun jeu choisi pour l'instant."))}</span>` : ""}</div>
        <div class="sy-row"><span class="sy-chip info">${esc(T("%d jeu(x) choisi(s)", c.chosen))}</span><span class="sy-link">${esc(T("Choisir les jeux ›"))}</span></div>
        <p class="hint small">${esc((d.kind === "nas" ? T("↑ part vers le NAS · ↓ arrive ici · ✓ déjà au même niveau") : T("↑ part vers l'autre PS5 · ↓ arrive ici · ✓ déjà au même niveau")))}</p></button>
      ${order.map((i) => [...soon[i], i]).map(([n, txt, col, msg, i]) => d.kind === "nas" && i >= 2 ? `<div class="sy-card soon" data-soon="${esc(T("Cette section se synchronise avec une autre PS5, pas avec un NAS."))}"><div class="sy-h"><i style="--c:${col}"></i><b>${esc(n)}</b><span class="sy-chip">${esc(T("Entre deux PS5"))}</span></div><p class="hint">${esc(T("Cette section se synchronise avec une autre PS5, pas avec un NAS."))}</p></div>`
        : i === 4 ? partiesCard(d, n, col, 3) : i === 3 ? configCard(d, n, col) : i === 2 ? dlcCard(d, n, col) : i <= 1 ? partiesCard(d, n, col, i) : `<div class="sy-card soon" data-soon="${esc(msg)}"><div class="sy-h"><i style="--c:${col}"></i><b>${esc(n)}</b><span class="sy-chip">${esc(T("Bientôt"))}</span></div><p class="hint">${esc(txt)}</p></div>`).join("")}</div>`;
  }
  $("#view").innerHTML = head + `<div class="sy-page"><div class="sy-devs">${list}</div><div class="sy-main">${hero}${secs}</div></div>`;
  $$("[data-sy]").forEach((b) => b.onclick = () => { SY.dev = b.dataset.sy; SY.pick = null; synchroDraw(); });
  $$("[data-soon]").forEach((b) => b.onclick = () => toast(b.dataset.soon));
  if ($("#sy-saves")) $("#sy-saves").onclick = () => synchroOpenPick(d.id);
  if ($("#sy-parties")) $("#sy-parties").onclick = () => partiesOpen(d.id, 0);
  if ($("#sy-apps")) $("#sy-apps").onclick = () => partiesOpen(d.id, 1);
  if ($("#sy-dlc")) $("#sy-dlc").onclick = () => partiesOpen(d.id, 2);
  if ($("#sy-emu")) $("#sy-emu").onclick = () => partiesOpen(d.id, 3);
  if ($("#sy-config")) $("#sy-config").onclick = () => { SY.pf = 6; partiesOpen(d.id, 1, true); };
  const pErr = ["parties", "apps", "dlc", "emu"].some((k) => ((SY[k] || {})[d.id] || {}).error);  // pas de réponse : nouvel essai au bout de 20 s
  if ((d.kind === "ps5" || d.kind === "nas") && (!(SY.parties || {})[d.id] || (pErr && Date.now() - (SY.partiesAt || 0) > 20000))) loadParties(d.id, d.kind === "nas");
  if ($("#sy-main")) $("#sy-main").onclick = () => {
    if (d.kind === "switch") { state.party.player = "switch:" + d.id; setTab("saves"); return; }
    if (!c.chosen) { synchroOpenPick(d.id); return; }
    if (!c.need.length) { toast(T("Tout est déjà au même niveau.")); return; }
    syStartFollowed(d, $("#sy-main"));
  };
  if ($("#sy-games")) $("#sy-games").onclick = () => setTab("transfers");
}
// « Mettre à niveau » : où ranger les sauvegardes ? Plusieurs disques (là-bas ou ici) → on demande (comme la télé).
function syStartFollowed(d, anchor) {
  const pl = syPlanOf(d.id) || {}, need = (pl.titles || []).filter(syNeeds);
  const toPeer = need.some((t) => t.push || (t.send_full && t.full_push_bytes)), toHere = need.some((t) => t.pull || (t.send_full && t.full_pull_bytes));
  const first = (list, id) => [...list.filter((p) => p.id === id), ...list.filter((p) => p.id !== id)];
  const there = first(state.places.filter((p) => p.type === "peer" && p.id.startsWith("peer:" + d.id + ":")), pl.peer_place);
  const here = first(state.places.filter((p) => p.type === "internal" || p.type === "usb"), pl.here_place);
  const lbl = (p) => (p.type === "internal" ? T("Interne") : p.label.includes(" · ") ? p.label.split(" · ").slice(1).join(" · ") : p.label);
  const go = (hid, tid) => syStart(d.id, d.name, { followed: true, ...(hid ? { here_place: hid } : {}), ...(tid ? { peer_place: tid } : {}) });
  const askHere = (tid) => {
    if (!toHere || here.length <= 1) { go(here[0] && here[0].id, tid); return; }
    openMenu(anchor, here.map((p) => ({ label: T("%s · %s libres", lbl(p), size(p.free)), hint: T("Où ranger ici les sauvegardes qui arrivent ?"), run: () => go(p.id, tid) })));
  };
  if (!toPeer || there.length <= 1) { askHere(there[0] && there[0].id); return; }
  openMenu(anchor, there.map((p) => ({ label: T("%s · %s libres", lbl(p), size(p.free)), hint: T("Où ranger les sauvegardes sur %s ?", d.name), run: () => setTimeout(() => askHere(p.id), 0) })));
}
// ---- Parties actives (comme la télé) : la plus récente gagne, celle qu'elle remplace est d'abord sauvegardée ----
async function loadParties(peer, nas) {
  SY.partiesAt = Date.now();
  SY.parties = SY.parties || {};
  SY.parties[peer] = { loading: true };
  try { SY.parties[peer] = await api(`/sync/parties/plan?peer=${encodeURIComponent(peer)}`); } catch (e) { SY.parties[peer] = { error: e.message }; }
  SY.apps = SY.apps || {};
  try { SY.apps[peer] = await api(`/sync/apps/plan?peer=${encodeURIComponent(peer)}`); } catch (e) { SY.apps[peer] = { error: e.message }; }
  SY.dlc = SY.dlc || {};
  if (!nas) try { SY.dlc[peer] = await api(`/sync/dlc/plan?peer=${encodeURIComponent(peer)}`); } catch (e) { SY.dlc[peer] = { error: e.message }; }  // DLC : entre deux PS5
  SY.emu = SY.emu || {};
  if (!nas) try { SY.emu[peer] = await api(`/sync/emu/plan?peer=${encodeURIComponent(peer)}`); } catch (e) { SY.emu[peer] = { error: e.message }; }  // émulateurs : entre deux PS5
  try { SY.pstatus = await api("/sync/parties/status"); } catch (e) { /* rien */ }
  if (state.tab === "synchro" && !SY.pick && !SY.pp) synchroDraw();
}
// Réglages : les applis qui ont des réglages (même écran que Applis, filtre « Avec réglages »).
function configCard(d, n, col) {
  const ap = (SY.apps || {})[d.id], t = (ap && ap.titles) || [];
  const withCfg = t.filter((x) => x.config_state && x.config_state !== "undefined"), moving = withCfg.filter((x) => x.followed && x.config_moves).length;
  const line = !ap || ap.loading ? T("lecture…") : ap.error ? T("autre PS5 injoignable") : moving ? T("%d à mettre à niveau", moving) : T("%d appli(s) avec réglages", withCfg.length);
  return `<button class="sy-card" id="sy-config"><div class="sy-h"><i style="--c:${col}"></i><b>${esc(n)}</b><span class="sy-chip ${moving ? "warn" : ""}">${esc(line)}</span></div>
    <p class="hint">${esc(T("Les réglages des applis : appli + réglages ou réglages seuls, selon un profil par appli."))}</p></button>`;
}
// DLC : ce qui manque d'un côté (lecture seule pour l'instant, comme la télé).
function dlcCard(d, n, col) {
  const dp = (SY.dlc || {})[d.id], sm = (dp && dp.summary) || {}, k = (sm.to_complete_here || 0) + (sm.to_complete_there || 0);
  const ps = SY.pstatus || {}, run = ps.state === "running" && ps.peer === d.id && ps.section === "dlc";
  const line = run ? T("En cours : %lld / %lld", ps.done || 0, ps.total || 0) : !dp ? T("lecture…") : dp.error ? T("autre PS5 injoignable") : k ? T("%lld jeu(x) à compléter", k) : T("rien ne manque");
  return `<button class="sy-card" id="sy-dlc"><div class="sy-h"><i style="--c:${col}"></i><b>${esc(n)}</b><span class="sy-chip ${dp && (dp.error || k) ? "warn" : dp ? "ok" : ""}">${esc(line)}</span></div>
    <p class="hint">${esc(T("Les extensions et mises à jour qui manquent d'un côté, sans jamais renvoyer le jeu entier."))}</p></button>`;
}
function dlcDraw() {
  const p = SY.pp, d = synchroDevices().find((x) => x.id === p.peer) || { name: "" }, name = d.name, pl = p.plan, titles = (pl && pl.titles) || [], sm = (pl && pl.summary) || {};
  const H = (t) => t.state === "missing_here" || t.state === "both", U = (t) => t.state === "missing_there" || t.state === "both", G = (t) => String(t.state).startsWith("game_missing");
  const filt = [[T("Tous · %d", titles.length), () => true], [T("↓ À compléter ici · %d", titles.filter(H).length), H], [T("↑ À compléter sur %s · %d", name, titles.filter(U).length), U],
    [T("Jeu absent · %d", titles.filter(G).length), G], [T("Pareil · %d", titles.filter((t) => t.state === "same").length), (t) => t.state === "same"]];
  if (!(SY.pf < filt.length)) SY.pf = 0;
  const what = (dlc, upd, bytes) => [dlc ? T("%d DLC", dlc) : "", upd ? T("mise à jour") : ""].filter(Boolean).join(" + ") + (bytes > 0 && (dlc || upd) ? " · " + size(bytes) : "");
  const lbl = (x) => { const m = /\[DLC ([^\]]+)\]/.exec(x.file || ""); return m ? m[1] : x.file || x.id; };
  const row = (t) => {
    const kind = t.kind === "switch" ? "Switch" : t.kind === "ps4" ? "PS4" : "PS5";
    const det = T("Ici : %d DLC", t.here || 0) + (t.here_version ? " · " + t.here_version : "") + " · " + T("Sur %s : %d DLC", name, t.there || 0) + (t.there_version ? " · " + t.there_version : "") + " · " + kind;
    let chip;
    if (t.state === "game_missing_there") chip = `<span class="sy-chip">${esc(T("jeu absent sur %s : Transferts", name))}</span>`;
    else if (t.state === "game_missing_here") chip = `<span class="sy-chip">${esc(T("jeu absent ici : Transferts"))}</span>`;
    else if (t.state === "same") chip = `<span class="sy-chip ok">${esc(T("✓ pareil des deux côtés"))}</span>`;
    else {
      const dn = what(t.missing_here || 0, t.update === "there_newer", t.bytes_missing_here || 0), up = what(t.missing_there || 0, t.update === "here_newer", t.bytes_missing_there || 0);
      chip = `<span class="sy-chip warn">${esc([dn ? "↓ " + dn : "", up ? "↑ " + up : ""].filter(Boolean).join("   "))}</span>`;
    }
    const miss = [...(t.missing_here_list || []).map((x) => T("Manque ici : %s", lbl(x))), ...(t.missing_there_list || []).map((x) => T("Manque sur %s : %s", name, lbl(x)))];
    const lib = t.library || [], libtxt = lib.length ? T("Paquets rangés ici : %d (%s)", lib.length, [...new Set(lib.map((x) => x.place))].join(", ")) : "";
    if (t.state === "same" && lib.length) chip = `<span class="sy-chip info">${esc(T("paquet rangé ici : pour l'installeur"))}</span>`;
    return `<div class="sy-item"><div class="thumb">${mgCover({ item: "title:" + t.title_id, title_id: t.title_id, title: t.title })}</div>
      <div class="grow"><b>${esc(t.title)}</b><div class="hint">${esc(det)}</div>${miss.length ? `<div class="hint small">${esc(miss.slice(0, 6).join(" · ") + (miss.length > 6 ? " · " + T("… et %d autre(s)", miss.length - 6) : ""))}</div>` : ""}${libtxt ? `<div class="hint small" title="${esc(lib.map((x) => x.file + " · " + size(x.bytes || 0)).join("\n"))}">${esc(libtxt)}</div>` : ""}</div>${chip}</div>`;
  };
  const stores = ((pl && pl.stores) || []).map((x) => x.label).join(", ");
  $("#view").innerHTML = `<div class="sy-top"><h1>${esc(T("DLC et mises à jour"))}</h1><span class="hint">${esc(T("avec %s", name))}</span>
      ${pl && !pl.error ? `<span class="hint sy-count">${esc(T("%lld à compléter ici · %lld sur %s", sm.to_complete_here || 0, sm.to_complete_there || 0, name))}</span>` : ""}</div>
    <p class="hint">${esc(T("Les extensions et mises à jour de chaque jeu, des deux côtés. Seul ce qui manque est copié, jamais le jeu entier (lui passe par Transferts). Switch : copie directe ; PS5 / PS4 : par l'installeur (prochaine étape)."))}</p>
    ${pl && !pl.error ? `<p class="${stores ? "hint" : "warn"}">${esc(stores ? T("Points de stockage pour les paquets : %s", stores) : T("Pour installer plus tard, il faudra un point de stockage : une clé USB sur la console ou un NAS."))}</p>` : ""}
    <div class="actions">${filt.map(([l], i) => `<button class="${i === SY.pf ? "primary" : "secondary"} small" data-ppf="${i}">${esc(l)}</button>`).join("")}</div>
    <div class="actions"><button class="primary" id="dl-go">${esc(T("Compléter…"))}</button><button class="secondary" id="pp-back">${esc(T("Retour à Synchro"))}</button></div>
    ${!pl ? `<p class="hint">${esc(T("Lecture…"))}</p>` : pl.error ? `<p class="bad">${esc(pl.error)}</p>` : !titles.length ? `<p class="hint">${esc(T("Aucun DLC ni aucune mise à jour différente entre les deux consoles."))}</p>` : `<div class="sy-list">${titles.filter(filt[SY.pf][1]).map(row).join("") || `<p class="hint">${esc(T("Rien à afficher avec ce filtre."))}</p>`}</div>`}`;
  $("#pp-back").onclick = () => { SY.pp = null; synchroDraw(); };
  $$("[data-ppf]").forEach((b) => b.onclick = () => { SY.pf = +b.dataset.ppf; dlcDraw(); });
  $("#dl-go").onclick = async () => {
    const sw = titles.filter((t) => t.kind === "switch"), ps = titles.filter((t) => t.kind !== "switch" && (H(t) || U(t))).length;
    const h = sw.filter(H), u = sw.filter(U), bh = h.reduce((a, t) => a + (t.bytes_missing_here || 0), 0), bu = u.reduce((a, t) => a + (t.bytes_missing_there || 0), 0);
    if (!h.length && !u.length) { toast(ps ? T("Jeux PS5 / PS4 : leurs DLC s'installeront par l'installeur (prochaine étape).") : T("Rien à compléter.")); return; }
    const note = T("Jeux Switch : seuls les fichiers qui manquent sont copiés dans le dossier du jeu. Rien n'est écrasé ni supprimé ; jamais pendant qu'Eden est ouvert.") + (ps ? " " + T("Jeux PS5 / PS4 : leurs DLC s'installeront par l'installeur (prochaine étape).") : "");
    const run = async (dir) => { try { await post("/sync/dlc/start", { peer: p.peer, direction: dir }); toast(dir === "here" ? T("DLC : copie vers cette PS5 lancée") : T("DLC : copie vers %s lancée", name)); } catch (e) { toast(e.message); } };
    if (h.length && await ask(T("Compléter les DLC et mises à jour ?"), `<p class="hint">${esc(note)}</p>`, T("Compléter ici · %lld jeu(x) · %s", h.length, size(bh)))) await run("here");
    if (u.length && await ask(T("Compléter les DLC et mises à jour ?"), `<p class="hint">${esc(note)}</p>`, T("Compléter sur %s · %lld jeu(x) · %s", name, u.length, size(bu)))) await run("there");
  };
}
function partiesCard(d, n, col, sec) {
  const pp = (sec === 3 ? SY.emu || {} : sec ? SY.apps || {} : SY.parties || {})[d.id], ps = SY.pstatus || {};
  let line = T("lecture…"), tone = "";
  if (ps.state === "running" && ps.peer === d.id && (ps.section || "") === (sec === 3 ? "emu" : sec ? "apps" : "")) { line = T("En cours : %lld / %lld", ps.done || 0, ps.total || 0); tone = "info"; }
  else if (pp && pp.error) { line = d.kind === "nas" ? T("NAS injoignable") : T("autre PS5 injoignable"); tone = "warn"; }
  else if (pp && !pp.loading) { line = !pp.chosen ? (sec ? T("aucune appli choisie") : T("aucun jeu choisi")) : pp.need ? T("%lld à mettre à niveau", pp.need) : T("au même niveau"); tone = !pp.chosen ? "" : pp.need ? "warn" : "ok"; }
  if (sec === 3) return `<button class="sy-card" id="sy-emu"><div class="sy-h"><i style="--c:${col}"></i><b>${esc(n)}</b><span class="sy-chip ${tone}">${esc(line)}</span></div>
    <p class="hint">${esc(T("La sauvegarde la plus récente de ProsperoEden ou d'Églantine arrive sur l'autre PS5 ; l'ancienne est gardée."))}</p></button>`;
  return `<button class="sy-card" id="${sec ? "sy-apps" : "sy-parties"}"><div class="sy-h"><i style="--c:${col}"></i><b>${esc(n)}</b><span class="sy-chip ${tone}">${esc(line)}</span></div>
    <p class="hint">${esc(d.kind === "nas" ? (sec ? T("Le NAS garde la dernière version de chaque appli choisie ; une PS5 en retard l'installe depuis le NAS.") : T("Le NAS garde la partie la plus récente ; une PS5 en retard la récupère, la sienne d'abord sauvegardée."))
      : sec ? T("La version la plus récente d'une appli remplace l'ancienne de l'autre côté. Même version : rien.") : T("La partie la plus récente arrive sur l'autre PS5 ; celle qu'elle remplace est d'abord sauvegardée."))}</p></button>`;
}
async function partiesOpen(peer, sec, keepFilter) {
  if (sec === undefined) sec = SY.pp ? SY.pp.sec : 0;
  if (!keepFilter && (!SY.pp || SY.pp.sec !== sec)) SY.pf = 0;
  SY.pp = { peer, plan: null, sec };
  partiesDraw();
  try { SY.pp.plan = await api(`/sync/${sec === 3 ? "emu" : sec === 2 ? "dlc" : sec ? "apps" : "parties"}/plan?peer=${encodeURIComponent(peer)}`); } catch (e) { if (SY.pp) SY.pp.plan = { error: e.message }; }
  if (SY.pp) partiesDraw();
}
function partiesDraw() {
  if (SY.pp.sec === 2) { dlcDraw(); return; }
  const p = SY.pp, d = synchroDevices().find((x) => x.id === p.peer) || { name: "" }, name = d.name, pl = p.plan, apps = p.sec === 1, emu = p.sec === 3;
  const moving = (t) => apps ? !!(t.app_moves || t.config_moves) : t.state !== "same" && t.state !== "conflict" && t.state !== "unknown" && t.state !== "manual" && t.state !== "no_profile";
  const titles = (pl && pl.titles) || [], chosen = titles.filter((t) => t.followed).length, need = titles.filter((t) => t.followed && moving(t)).length;
  const sw = (on, attr) => `<button class="sy-switch ${on ? "on" : ""}" ${attr}><i></i></button>`;
  // Filtres (comme la télé) : tous, choisis, à mettre à jour, à trancher, vers là-bas, vers ici
  const isUp = (t) => t.state === "here_newer" || t.state === "only_here", isDown = (t) => t.state === "there_newer" || t.state === "only_there";
  const filt = [[T("Tous · %d", titles.length), () => true], [T("Choisis · %d", chosen), (t) => t.followed],
    [T("À mettre à jour · %d", need), (t) => t.followed && moving(t)],
    apps ? [T("Par Transferts · %d", titles.filter((t) => t.state === "unknown" || t.state === "manual").length), (t) => t.state === "unknown" || t.state === "manual"] : [T("À trancher · %d", titles.filter((t) => t.conflict).length), (t) => t.conflict],
    [T("↑ Vers %s · %d", name, titles.filter(isUp).length), isUp], [T("↓ Vers ici · %d", titles.filter(isDown).length), isDown],
    ...(apps ? [[T("Avec réglages · %d", titles.filter((t) => t.config_state && t.config_state !== "undefined").length), (t) => t.config_state && t.config_state !== "undefined"]] : [])];
  if (SY.pf >= filt.length) SY.pf = 0;
  SY.pf = SY.pf || 0;
  const why = (t) => ({ more_play_time: T("plus de temps de jeu"), played_here: T("rejouée ici"), played_there: T("rejouée là-bas"), both_played: T("votre choix") })[t.reason] || "";
  const row = (t) => {
    const cs = t.config_state || "", cfg = !apps || !t.mode || t.mode === "app" || !cs ? "" : " · " + (cs === "same" ? T("réglages identiques") : cs === "here_newer" ? T("réglages : ↑ à envoyer") : cs === "there_newer" ? T("réglages : ↓ à recevoir")
      : cs === "conflict" ? T("réglages modifiés des deux côtés") : cs === "version_differs" ? T("réglages : versions différentes") : T("réglages : dossier inconnu"));
    const det = apps ? (t.here_key ? T("Ici : v%s", t.here_version || "?") : T("Pas installée ici")) + " · " + (t.there_key ? T("Sur %s", name) + " : v" + (t.there_version || "?") : pl.nas ? T("Pas encore dans le dépôt") : T("Pas installée sur %s", name)) + cfg
      : (t.detail ? t.detail + " · " : "") + (t.here_at ? T("Ici : %s", syDay(t.here_at)) : T("Pas de partie ici")) + " · " + (t.there_at ? T("Sur %s", name) + " : " + syDay(t.there_at) : T("Pas de partie sur %s", name));
    const up = t.state === "here_newer" || t.state === "only_here";
    const chip = t.running ? `<span class="sy-chip warn">${esc(T("en cours de jeu"))}</span>`
      : t.state === "conflict" ? `<span class="sy-chip warn">${esc(apps ? T("même version, fichiers différents : R2") : T("Jouée des deux côtés : laquelle garder ?"))}</span><button class="secondary small" data-ppr="${esc(t.title_id)}" data-k="${apps ? "apps_resolve" : "resolve"}" data-v="here">${esc(T("Celle d'ici"))}</button><button class="secondary small" data-ppr="${esc(t.title_id)}" data-k="${apps ? "apps_resolve" : "resolve"}" data-v="there">${esc(T("Celle de %s", name))}</button>`
      : apps && t.mode !== "app" && cs === "conflict" ? `<span class="sy-chip warn">${esc(T("réglages modifiés des deux côtés"))}</span><button class="secondary small" data-ppr="${esc(t.title_id)}" data-k="config_resolve" data-v="here">${esc(T("Celle d'ici"))}</button><button class="secondary small" data-ppr="${esc(t.title_id)}" data-k="config_resolve" data-v="there">${esc(T("Celle de %s", name))}</button>`
      : t.state === "no_profile" ? `<span class="sy-chip">${esc(T("aucun profil de ce nom ici"))}</span>`
      : t.state === "manual" ? `<span class="sy-chip">${esc(T("en image : par Transferts"))}</span>` : t.state === "unknown" ? `<span class="sy-chip">${esc(T("version inconnue"))}</span>`
      : !t.followed ? `<span class="sy-chip">${esc(T("pas choisi"))}</span>`
      : apps && t.state === "only_here" ? `<span class="sy-chip warn">${esc(T("↑ à installer sur %s", name))}</span>` : apps && t.state === "only_there" ? `<span class="sy-chip warn">${esc(T("↓ à installer ici"))}</span>`
      : t.state === "same" ? `<span class="sy-chip ok">${esc(T("✓ au même niveau"))}</span>` : `<span class="sy-chip warn">${esc((up ? T("↑ la plus récente est ici → %s", name) : T("↓ la plus récente est sur %s", name)) + (why(t) ? " · " + why(t) : ""))}</span>`;
    return `<div class="sy-item ${t.followed ? "" : "off"}"><div class="thumb">${mgCover({ item: "title:" + (t.game_id || t.title_id), title_id: t.game_id || t.title_id, title: t.title, kind: apps ? "app" : undefined })}</div>
      <div class="grow"><b>${esc(t.title)}</b><div class="hint">${esc(det)}</div></div>${apps ? `<select data-ppm="${esc(t.title_id)}">${[["app", T("Appli seule")], ["app_config", T("Appli + réglages")], ["config", T("Réglages seuls")]].map(([v, l]) => `<option value="${v}" ${(t.mode || "app") === v ? "selected" : ""}>${esc(l)}</option>`).join("")}</select>` : ""}${chip}${sw(t.followed, `data-ppk="${esc(t.title_id)}"`)}</div>`;
  };
  $("#view").innerHTML = `<div class="sy-top"><h1>${esc(apps ? T("Applis à garder au même niveau") : emu ? T("Émulateurs : progression à garder au même niveau") : T("Parties actives à garder au même niveau"))}</h1><span class="hint">${esc(T("avec %s", name))}</span>
      <span class="hint sy-count">${esc((apps ? T("%d appli(s) choisie(s) sur %d", chosen, titles.length) : T("%d jeu(x) choisi(s) sur %d", chosen, titles.length)) + (need ? " · " + T("%lld à mettre à niveau", need) : ""))}</span></div>
    <p class="hint">${esc(pl && pl.nas ? (apps ? T("Le NAS sert de dépôt : il garde la dernière version de chaque appli choisie. Plus récente ici : elle y est déposée ; plus récente sur le NAS : elle est installée ici, à la place de l'ancienne. Jamais de retour en arrière.")
        : T("Le NAS sert de dépôt : il garde la partie la plus récente de chaque jeu choisi. Plus récente ici : une copie y part ; plus récente sur le NAS : elle est restaurée ici, celle d'ici d'abord sauvegardée. Jamais pendant que le jeu tourne."))
      : emu ? T("Les sauvegardes de ProsperoEden et d'Églantine, profil par profil (associés par leur nom). La plus récente gagne ; celle qu'elle remplace est d'abord copiée. Jamais pendant que l'émulateur est ouvert.")
      : apps ? T("Les applis installées sur chaque PS5. Même version : rien. Plus récente d'un côté : elle remplace l'ancienne de l'autre côté — jamais deux versions, jamais de retour en arrière. Une appli en image (.ffpkg, .exfat) se met à jour par Transferts.")
      : T("Les parties qui sont SUR les consoles, celles que vous reprenez en lançant le jeu. La plus récente gagne ; celle qu'elle remplace est d'abord sauvegardée. Jamais pendant que le jeu tourne."))}</p>
    ${pl && !pl.error && !apps && pl.nas ? `<p class="hint">${esc(T("Compte : la partie de chaque compte de cette PS5 est comparée à sa dernière copie sur le NAS."))}</p>` : ""}
    ${pl && !pl.error && !apps && !emu && !pl.nas ? `<div class="actions"><span class="hint">${esc(T("Compte ici :"))}</span><select id="pp-here"><option value="">${esc(T("le même compte PSN des deux côtés (automatique)"))}</option>${(pl.here_accounts || []).map((a) => `<option value="${esc(a.key)}" ${a.key === pl.here_account ? "selected" : ""}>${esc(a.name + " (" + a.user_id + ")")}</option>`).join("")}</select>
      <span class="hint">${esc(T("Compte sur %s :", name))}</span><select id="pp-there"><option value="">${esc(T("automatique"))}</option>${(pl.there_accounts || []).map((a) => `<option value="${esc(a.key)}" ${a.key === pl.there_account ? "selected" : ""}>${esc(a.name + " (" + a.user_id + ")")}</option>`).join("")}</select></div>` : ""}
    <div class="actions">${filt.map(([lbl], i) => `<button class="${i === SY.pf ? "primary" : "secondary"} small" data-ppf="${i}">${esc(lbl)}</button>`).join("")}</div>
    <div class="actions"><button class="primary" id="pp-go" ${need ? "" : "disabled"}>${esc(need ? T("Mettre à niveau…") : T("Rien à mettre à niveau"))}</button>
      <button class="secondary small" id="pp-all">${esc(T("Tout choisir / tout retirer"))}</button><button class="secondary" id="pp-back">${esc(T("Retour à Synchro"))}</button></div>
    ${!pl ? `<p class="hint">${esc(T("Lecture…"))}</p>` : pl.error ? `<p class="bad">${esc(pl.error)}</p>` : !titles.length ? `<p class="hint">${esc(apps ? T("Aucune appli à comparer.") : pl.nas ? T("Aucune partie à comparer : il faut des parties sur cette PS5, ou des copies sur le NAS d'un compte présent ici.") : T("Aucune partie à comparer : il faut le même compte PSN sur les deux PS5, et des parties sur au moins l'une d'elles."))}</p>` : `<div class="sy-list">${titles.filter(filt[SY.pf][1]).map(row).join("") || `<p class="hint">${esc(T("Rien à afficher avec ce filtre."))}</p>`}</div>`}`;
  const prefs = async (b) => { try { await post("/sync/parties/prefs", { peer: p.peer, ...(apps ? { section: "apps" } : emu ? { section: "emu" } : {}), ...b }); } catch (e) { toast(e.message); } if (SY.parties) SY.parties[p.peer] = null; partiesOpen(p.peer, p.sec); };
  $("#pp-back").onclick = () => { SY.pp = null; synchroDraw(); };
  $$("[data-ppf]").forEach((b) => b.onclick = () => { SY.pf = +b.dataset.ppf; partiesDraw(); });
  $$("[data-ppr]").forEach((b) => b.onclick = () => prefs({ [b.dataset.k || "resolve"]: { [b.dataset.ppr]: b.dataset.v } }));
  $$("[data-ppm]").forEach((c) => c.onchange = () => prefs({ apps_mode: { [c.dataset.ppm]: c.value } }));
  const acc = () => { const h = $("#pp-here").value, t = $("#pp-there").value; if ((h && t) || (!h && !t)) prefs({ here_account: h, there_account: t }); };
  if ($("#pp-here")) { $("#pp-here").onchange = acc; $("#pp-there").onchange = acc; }
  $("#pp-all").onclick = () => prefs({ follow_all: !(titles.length && titles.every((t) => t.followed)) });
  $$("[data-ppk]").forEach((b) => b.onclick = () => { const t = titles.find((x) => x.title_id === b.dataset.ppk); prefs({ follow: { [t.title_id]: !t.followed } }); });
  $("#pp-go").onclick = async () => {
    const todo = titles.filter((t) => t.followed && moving(t));
    const run = todo.find((t) => t.running);
    if (run) { toast(T("« %s » est lancé : fermez le jeu d'abord.", run.title)); return; }
    const up = (t) => t.state === "here_newer" || t.state === "only_here";
    const what = (t) => !apps ? (up(t) ? T("la partie d'ici remplace celle de %s", name) : T("la partie de %s remplace celle d'ici", name))
      : !t.app_moves ? (t.config_state === "here_newer" ? T("réglages d'ici → %s", name) : T("réglages de %s → ici", name))
      : up(t) ? (t.there_version ? T("v%s d'ici remplace v%s sur %s", t.here_version, t.there_version, name) : T("installée sur %s (v%s)", name, t.here_version))
      : (t.here_version ? T("v%s de %s remplace v%s d'ici", t.there_version, name, t.here_version) : T("installée ici (v%s)", t.there_version));
    const list = todo.map((t) => `<li>${esc(t.title)}${t.detail ? " (" + esc(t.detail) + ")" : ""} : ${esc(what(t))}</li>`).join("");
    const note = emu ? T("Avant de remplacer une sauvegarde d'émulateur, une copie de sécurité est faite. Jamais pendant que l'émulateur est ouvert.") : apps ? T("L'ancienne version n'est retirée qu'une fois la nouvelle installée, vérifiée et ses droits relus ; sinon elle reste en place.") : T("Avant de remplacer une partie, elle est sauvegardée (onglet Sauvegardes). Jamais pendant que le jeu tourne.");
    if (!(await ask(apps ? T("Mettre à niveau les applis ?") : T("Mettre à niveau les parties actives ?"), `<ul>${list}</ul><p class="hint">${esc(note)}</p>`, apps ? T("Mettre à niveau %d appli(s)", todo.length) : T("Mettre à niveau %d partie(s)", todo.length)))) return;
    try { await post(emu ? "/sync/emu/start" : apps ? "/sync/apps/start" : "/sync/parties/start", { peer: p.peer }); toast(apps ? T("Applis : mise à niveau avec %s lancée", name) : T("Parties : mise à niveau avec %s lancée", name)); SY.pp = null; if (SY.parties) SY.parties[p.peer] = null; synchroDraw(); } catch (e) { toast(e.message); }
  };
}
async function synchroOpenPick(peer) {
  SY.pick = SY.pick && SY.pick.peer === peer ? SY.pick : { peer, loading: true };
  try { const p = await api(`/sync/copies/plan?peer=${encodeURIComponent(peer)}&full=1`); SY.pick = p; const sp = syncState.plans.find((x) => x.plan.peer === peer); if (sp) sp.plan = p; }
  catch (e) { toast(e.message); SY.pick = null; }
  if (state.tab === "synchro") synchroDraw();
}
function synchroPickRows() {
  const f = SY.filter;
  const up = (t) => t.push || t.full_push_bytes, down = (t) => t.pull || t.full_pull_bytes;  // filtres de direction (comme la télé)
  const rows = (SY.pick.titles || []).filter((t) => !(f === 1 && !t.followed) && !(f === 2 && !syNeeds(t)) && !(f === 3 && t.followed) && !(f === 4 && !up(t)) && !(f === 5 && !down(t)));
  const at = (t) => Math.max(t.here_at || 0, t.there_at || 0);
  return rows.sort((a, b) => SY.sort === 1 ? at(b) - at(a) : String(a.title).localeCompare(String(b.title)));
}
function synchroPickDraw() {
  const p = SY.pick, d = synchroDevices().find((x) => x.id === p.peer) || { name: p.name || "" }, name = d.name;
  if (p.loading) { $("#view").innerHTML = `<h1>${esc(T("Sauvegardes à garder au même niveau"))}</h1><p class="hint">${esc(T("Lecture…"))}</p>`; return; }
  if (SY.versions) { synchroVersionsDraw(name); return; }
  const titles = p.titles || [], chosen = titles.filter((t) => t.followed).length, need = titles.filter(syNeeds);
  const bytes = need.reduce((a, t) => a + (t.bytes || 0) + (t.send_full ? (t.full_push_bytes || 0) + (t.full_pull_bytes || 0) : 0), 0);
  const fl = [T("Tous · %d", titles.length), T("Choisis · %d", chosen), T("À mettre à jour · %d", need.length), T("Pas choisis · %d", titles.length - chosen),
    T("↑ Vers %s · %d", name, titles.filter((t) => t.push || t.full_push_bytes).length), T("↓ Vers ici · %d", titles.filter((t) => t.pull || t.full_pull_bytes).length)];
  const sw = (on, attr) => `<button class="sy-switch ${on ? "on" : ""}" ${attr}><i></i></button>`;
  const row = (t) => {
    const ha = t.here_at || 0, ta = t.there_at || 0;
    let det = ha && ta && ha === ta ? T("Ici et %s : %s", name, syDay(ha)) : ha && ta ? T("Ici : %s", syDay(ha)) + " · " + T("Sur %s", name) + " : " + syDay(ta) : ha ? T("Seulement ici : %s", syDay(ha)) : ta ? T("Seulement sur %s : %s", name, syDay(ta)) : "";
    const pt = syPlay(Math.max(t.here_play || 0, t.there_play || 0)); if (pt) det += " · " + pt;
    const chip = !t.followed ? `<span class="sy-chip">${esc(T("pas choisi"))}</span>` : !syNeeds(t) ? `<span class="sy-chip ok">${esc(T("✓ au même niveau"))}</span>`
      : `<span class="sy-chip warn">${esc(t.state === "push" ? T("↑ part vers %s", name) : t.state === "pull" ? T("↓ arrive ici") : t.state === "both" ? T("↕ des deux côtés") : t.full_push_bytes ? T("↑ le jeu part vers %s", name) : T("↓ le jeu arrive ici"))}</span>`;
    const full = t.followed && (t.full_push_bytes || t.full_pull_bytes);
    const extra = full ? `<label class="sy-extra"><input type="checkbox" data-syfull="${esc(t.title_id)}" ${t.send_full ? "checked" : ""}> ${esc(t.full_push_bytes
        ? T("Le jeu n'est pas sur %s : l'envoyer aussi, une seule fois (%s). Ensuite, seule la sauvegarde suit.", name, size(t.full_push_bytes))
        : T("Le jeu n'est pas ici : le recevoir aussi, une seule fois (%s). Ensuite, seule la sauvegarde suit.", size(t.full_pull_bytes)))}</label>` : "";
    return `<div class="sy-item ${t.followed ? "" : "off"}"><div class="thumb">${mgCover({ item: "title:" + t.title_id, title_id: t.title_id, title: t.title, kind: t.kind })}</div>
      <div class="grow"><b>${esc(t.title)}</b><div class="hint">${esc(det)}</div>${extra}</div>${chip}${sw(t.followed, `data-syk="${esc(t.title_id)}"`)}</div>`;
  };
  const rows = synchroPickRows();
  $("#view").innerHTML = `<div class="sy-top"><h1>${esc(T("Sauvegardes à garder au même niveau"))}</h1><span class="hint">${esc(T("avec %s", name))}</span>
      <span class="hint sy-count">${esc(T("%d jeu(x) choisi(s) sur %d", chosen, titles.length) + (need.length ? " · " + T("%s à échanger", size(bytes)) : ""))}</span></div>
    <div class="sy-item sy-auto"><div class="grow"><b>${esc(T("Ajouter aussi les nouveaux jeux automatiquement"))}</b><div class="hint">${esc(T("Un jeu sauvegardé pour la première fois est choisi tout seul. Vous pouvez toujours le retirer."))}</div></div>${sw(p.auto_new, 'id="sy-auto"')}</div>
    <div class="actions">${fl.map((x, i) => `<button class="${i === SY.filter ? "primary" : "secondary"} small" data-syf="${i}">${esc(x)}</button>`).join("")}
      <button class="secondary small" id="sy-sort">${esc(SY.sort === 1 ? T("Trier par nom") : T("Trier par date"))}</button>
      <button class="secondary small" id="sy-all">${esc(T("Tout choisir / tout retirer"))}</button>
      <button class="secondary small" id="sy-vers">${esc(T("Anciennes versions"))}</button></div>
    <div class="sy-list">${rows.map(row).join("") || `<p class="hint">${esc(T("Rien à afficher avec ce filtre."))}</p>`}</div>
    <div class="actions"><button class="secondary" id="sy-back">${esc(T("Retour à Synchro"))}</button></div>`;
  $$("[data-syf]").forEach((b) => b.onclick = () => { SY.filter = +b.dataset.syf; synchroDraw(); });
  $("#sy-sort").onclick = () => { SY.sort = SY.sort === 1 ? 0 : 1; synchroDraw(); };
  $("#sy-back").onclick = () => { SY.pick = null; synchroDraw(); };
  $("#sy-vers").onclick = () => { SY.versions = true; SY.vsel = new Set(); synchroDraw(); };
  $("#sy-auto").onclick = () => syPrefs({ peer: p.peer, auto_new: !p.auto_new }, !p.auto_new ? T("Les nouveaux jeux seront choisis tout seuls") : T("Les nouveaux jeux seront à choisir"));
  $("#sy-all").onclick = () => { const all = titles.length && titles.every((t) => t.followed); syPrefs({ peer: p.peer, follow_all: !all }, all ? T("Plus aucun jeu choisi : rien ne se synchronise") : T("Tous les jeux sont choisis")); };
  $$("[data-syk]").forEach((b) => b.onclick = () => { const t = titles.find((x) => x.title_id === b.dataset.syk); t.followed = !t.followed; synchroDraw(); syPrefs({ peer: p.peer, follow: { [t.title_id]: t.followed } }); });
  $$("[data-syfull]").forEach((c) => c.onchange = () => syPrefs({ peer: p.peer, send_full: { [c.dataset.syfull]: c.checked } }));
}
// Anciennes versions : chaque sauvegarde qui manque d'un côté, pour en envoyer aussi de plus vieilles.
function synchroVersionsDraw(name) {
  const p = SY.pick;
  const rows = [...(p.pull || []).map((c) => [c, true]), ...(p.push || []).map((c) => [c, false])].filter(([c]) => !c.full)
    .sort(([a], [b]) => String(a.title).localeCompare(String(b.title)) || (b.created_unix || 0) - (a.created_unix || 0));
  $("#view").innerHTML = `<h1>${esc(T("Anciennes versions"))}</h1><p class="hint">${esc(T("Chaque sauvegarde qui manque d'un côté, avec %s : cochez celles à envoyer, une par une, sans choisir tout le jeu.", name))}</p>
    <div class="sy-list">${rows.map(([c, here]) => `<label class="sy-item"><input type="checkbox" data-syv="${esc(c.bundle)}" ${SY.vsel.has(c.bundle) ? "checked" : ""}>
      <div class="thumb">${mgCover({ item: "title:" + c.title_id, title_id: c.title_id, title: c.title, kind: c.kind })}</div>
      <div class="grow"><b>${esc(c.title)}</b><div class="hint">${esc((here ? T("↓ arrive ici") : T("↑ part vers %s", name)) + " · " + T("copie du %s", syDay(c.created_unix)) + (syPlay(c.play_time || 0) ? " · " + syPlay(c.play_time) : ""))}</div></div>
      <span class="hint">${esc(size(c.bytes || 0))}</span></label>`).join("") || `<p class="hint">${esc(T("Rien à afficher : aucune ancienne version ne manque."))}</p>`}</div>
    <div class="actions"><button class="primary" id="sy-vgo" ${SY.vsel.size ? "" : "disabled"}>${esc(T("Envoyer (%d)", SY.vsel.size))}</button><button class="secondary" id="sy-vback">${esc(T("Retour"))}</button></div>`;
  $$("[data-syv]").forEach((c) => c.onchange = () => { if (c.checked) SY.vsel.add(c.dataset.syv); else SY.vsel.delete(c.dataset.syv); synchroDraw(); });
  $("#sy-vback").onclick = () => { SY.versions = false; synchroDraw(); };
  $("#sy-vgo").onclick = () => { syStart(p.peer, name, { bundles: [...SY.vsel] }); SY.versions = false; SY.pick = null; synchroDraw(); };
}
// Backups disponibles (comme la télé) : des copies existent (ici, NAS, autre PS5) mais le jeu ou sa sauvegarde n'est pas
// sur cette console. Un clic : leurs versions, pour restaurer ou récupérer.
// Les endroits qui ont vraiment une de ces sauvegardes : « Ici », « NAS », le nom de l'autre PS5 (comme la télé).
function bkWhere(list) {
  let here = false, nas = false;
  const peers = [];
  for (const g of list) for (const c of g.copies || []) {
    if (c.on_peer) { if (c.peer_name && !peers.includes(c.peer_name)) peers.push(c.peer_name); }
    else if (String(c.place || "").startsWith("remote:")) nas = true;
    else here = true;
  }
  return [...(here ? [T("Ici")] : []), ...(nas ? ["NAS"] : []), ...peers].join(" · ");
}
function backupsAvailable() {
  const sum = (typeof mg !== "undefined" && mg.sum) || null;
  if (!sum || !sum.games) return [];
  const ignored = (tid) => state.items.some((i) => i.ignored && i.title_id && i.title_id === tid);
  return sum.games.filter((g) => g.copies && g.copies.length && !g.other_account && !ignored(g.title_id) &&
      (g.full ? !state.items.some((i) => i.title_id === g.title_id && !i.key.startsWith("save:")) : !g.console))
    .map((g) => [Math.max(0, ...g.copies.filter((c) => c.complete !== false).map((c) => c.created_unix || 0)), g])
    .filter(([t]) => t > 0).sort((a, b) => b[0] - a[0]).slice(0, 16).map(([, g]) => g);
}
// Backups rangés sur chaque autre PS5 (même pour des jeux présents ici), comme la télé.
function peerBackups() {
  const sum = (typeof mg !== "undefined" && mg.sum) || null, per = new Map();
  for (const g of (sum && sum.games) || []) {
    const seen = new Set();
    for (const c of g.copies) if (c.on_peer && !seen.has(c.peer)) {
      seen.add(c.peer);
      if (!per.has(c.peer)) per.set(c.peer, { instance: c.peer, name: c.peer_name, games: [] });
      per.get(c.peer).games.push(g);
    }
  }
  return [...per.values()];
}
// Récemment joués : les jeux de la console, de la partie la plus récente à la plus ancienne.
function recentPlayed() {
  const seen = new Set();
  return entries().filter((e) => e.console && e.console.mtime).sort((a, b) => b.console.mtime - a.console.mtime)
    .map((e) => state.items.find((i) => i.key === e.item)).filter((it) => it && !it.ignored && !seen.has(it.key) && seen.add(it.key)).slice(0, 16);
}
function heroHtml() {
  const hero = heroItems();
  if (!hero.length) return "";
  state.hero = Math.min(state.hero, hero.length - 1);
  const it = hero[state.hero];
  let why = T("DANS VOTRE COLLECTION"), tone = "info", last = "";
  const e = entries().find((x) => x.item === it.key);
  if (e) {
    if (e.status === "console_newer") { why = T("À SAUVEGARDER · JOUÉ DEPUIS LA DERNIÈRE SAUVEGARDE"); tone = "warn"; }
    else if (e.status === "not_backed_up") { why = T("À SAUVEGARDER · JAMAIS SAUVEGARDÉ"); tone = "warn"; }
    else if (e.status === "up_to_date") { why = T("SAUVEGARDE À JOUR"); tone = "ok"; }
    const b = (e.backups || []).slice().sort((x, y) => y.created_unix - x.created_unix)[0];
    if (b) last = T("Dernière sauvegarde : %s", new Date(b.created_unix * 1000).toLocaleDateString(I18N.code)) + ` · ${b.place_label}`;
  }
  const go = it.kind === "app" ? T("Sauvegarder l'application…") : it.kind === "rom" ? T("Sauvegarder la ROM…") : T("Sauvegarder le jeu…");
  return `<section class="hero" id="hero">
    <div class="hero-bg" style="background-image:url('${coverUrl(it, 512)}')"></div>
    <div class="hero-text">
      <span class="why ${tone}">${esc(why)}</span>
      <h1 class="hero-title">${esc(cleanTitle(it.title))}</h1>
      <div class="chips">${[kindLabel(it), placeLabel(it.place), last].filter(Boolean).map((c) => `<span class="chip">${esc(c)}</span>`).join("")}</div>
      <div class="actions"><button class="primary" id="hero-go">${esc(go)}</button><button class="secondary" id="hero-search">${T("Rechercher")}</button></div>
      <div class="dots">${hero.map((_, i) => `<button data-hero="${i}" class="${i === state.hero ? "on" : ""}" aria-label="${esc(T("Élément %d", i + 1))}"></button>`).join("")}</div>
    </div>
    <button class="hero-cover" id="hero-cover" aria-label="${esc(T("Ouvrir la fiche"))}">${coverHtml(it, true)}</button>
    <button class="hero-nav prev" data-step="-1" aria-label="${esc(T("Précédent"))}">‹</button><button class="hero-nav next" data-step="1" aria-label="${esc(T("Suivant"))}">›</button>
  </section>`;
}
function bindHero() {
  const hero = heroItems(), h = $("#hero");
  if (!h) return;
  const it = hero[state.hero];
  $("#hero-go").onclick = () => openItem(it);
  $("#hero-cover").onclick = () => openItem(it);
  $("#hero-search").onclick = openSearch;
  $$("[data-hero]", h).forEach((b) => b.onclick = () => { state.hero = +b.dataset.hero; state.heroAt = Date.now(); updateHero(); });
  $$("[data-step]", h).forEach((b) => b.onclick = () => { state.hero = (state.hero + +b.dataset.step + hero.length) % hero.length; state.heroAt = Date.now(); updateHero(); });
}
function updateHero() { const h = $("#hero"); if (!h) return; h.outerHTML = heroHtml(); bindHero(); }
// Défilement automatique toutes les 8 s, sauf si une fiche ou une fenêtre est ouverte ou si la souris est sur le carrousel.
setInterval(() => {
  if (state.tab !== "home" || !$("#sheet").classList.contains("hidden") || $(".modal") || $("#hero:hover")) return;
  if (Date.now() - (state.heroAt || 0) < 8000) return;
  const n = heroItems().length;
  if (n > 1) { state.hero = (state.hero + 1) % n; state.heroAt = Date.now(); updateHero(); }
}, 1000);
function home() {
  if (!state.overview || Date.now() - state.overviewAt > 15000) loadOverview().then(() => { if (state.tab === "home") render(); });
  if (state.overview && !mg.sum && !mg.loadP && !mg.error) mgReady();  // « À faire » : doublons et copies à vérifier
  if (!state.items.length) { $("#view").innerHTML = `<div class="empty"><div class="icon">⌕</div>${T("Votre collection apparaît ici dès le premier relevé.")}</div>`; return; }
  const todo = homeTodo(), recent = recentPlayed(), bkAvail = backupsAvailable();
  const todoHtml = todo.length ? todo.map((c, i) => `<button class="todo ${c.tone}" data-todo="${i}"><span class="todo-img">${todoImg(c.games[0])}</span>
      <span class="grow"><span class="eyebrow">${esc(c.eyebrow)}</span><b>${esc(c.title)}</b><span class="hint todo-name">${esc(todoName(c.games[0]))}</span>${todoBar(c.games[0])}${"progress" in c.games[0] ? "" : todoDots(c.games.length, 0)}</span></button>`).join("")
    : mg.sum ? `<div class="todo ok done"><span class="todo-img"><span class="dev-ico">${ICONS.ps5}</span></span><span class="grow"><b>${T("Rien à faire : tout est en ordre")}</b><span class="hint">${T("Pas de doublon, pas de copie à vérifier, aucune tâche en cours.")}</span></span></div>`
    : `<div class="loading small">${SPIN}${T("Lecture des sauvegardes…")}</div>`;
  $("#view").innerHTML = heroHtml() + (state.overview ? "" : `<div class="loading small">${SPIN}${T("Lecture de l'état des sauvegardes…")}</div>`) +
    `<section class="shelf"><div class="shelf-head"><h2>${T("À faire")} <span class="n">${todo.length || ""}</span></h2></div><div class="todo-row" data-scroll="home-todo">${todoHtml}</div></section>` +
    (recent.length ? `<section class="shelf"><div class="shelf-head"><h2>${T("Récemment joués")} <span class="n">${recent.length}</span></h2>
      <button class="link" id="home-parties">${T("Tout voir")}</button></div>
      <div class="shelf-row" data-scroll="home-recent">${recent.map((it) => tileHtml(it, null, true)).join("")}</div></section>` : "") +
    categoryShelves().filter((x) => !x.later).map((s, i) => `<section class="shelf"><div class="shelf-head"><h2>${esc(s.title)} <span class="n">${s.items.length}</span></h2></div>
      <div class="shelf-row" data-scroll="home-cat-${i}">${s.items.map((it) => tileHtml(it, null, true)).join("")}</div></section>`).join("") +
    (bkAvail.length ? `<section class="shelf"><div class="shelf-head"><h2>${T("Sauvegardes disponibles")} <span class="n">${bkAvail.length}</span> ${bkWhere(bkAvail) ? `<span class="tag info away">${esc(bkWhere(bkAvail))}</span>` : ""}</h2></div>
      <div class="shelf-row" data-scroll="home-bk">${bkAvail.map((g, i) => `<div class="tile" data-bk="${i}" tabindex="0" role="button">${mgCover(g)}<div class="name">${esc(cleanTitle(g.title))}</div>
        <div class="meta">${esc(mgPlaces(g))}</div></div>`).join("")}</div></section>` : "") +
    peerGames.filter((p) => p.games && p.games.length).map((p, pi) => `<section class="shelf"><div class="shelf-head"><h2>${esc(T("%s · jeux installés", p.name))} <span class="n">${p.games.length}</span> <span class="tag info away">${esc(T("sur l'autre PS5"))}</span></h2></div>
      <div class="shelf-row" data-scroll="home-peer-${pi}">${p.games.slice(0, 24).map((g, gi) => `<div class="tile" data-pg="${pi}:${gi}" tabindex="0" role="button">${mgCover({ item: (g.kind === "game_ps4" ? "tid:ps4:" : "tid:ps5:") + g.title_id, title_id: g.title_id, title: g.title, kind: g.kind })}<div class="name">${esc(cleanTitle(g.title))}</div>
        <div class="meta">${esc(size(g.bytes || 0))}</div></div>`).join("")}</div></section>`).join("") +
    peerBackups().map((p, pi) => `<section class="shelf"><div class="shelf-head"><h2>${esc(T("%s · sauvegardes", p.name))} <span class="n">${p.games.length}</span> <span class="tag info away">${esc(T("sur l'autre PS5"))}</span></h2></div>
      <div class="shelf-row" data-scroll="home-pbk-${pi}">${p.games.slice(0, 24).map((g, gi) => `<div class="tile" data-pbk="${pi}:${gi}" tabindex="0" role="button">${mgCover(g)}<div class="name">${esc(cleanTitle(g.title))}</div>
        <div class="meta">${esc(T("%d copie(s)", g.copies.filter((c) => c.on_peer && c.peer === p.instance).length))}</div></div>`).join("")}</div></section>`).join("") +
    categoryShelves().filter((x) => x.later).map((s, i) => `<section class="shelf"><div class="shelf-head"><h2>${esc(s.title)} <span class="n">${s.items.length}</span></h2></div>
      <div class="shelf-row" data-scroll="home-later-${i}">${s.items.map((it) => tileHtml(it, null, true)).join("")}</div></section>`).join("");
  bindHero();
  bindTiles($("#view"));
  const peersShown = peerGames.filter((p) => p.games && p.games.length);
  $$("[data-pg]", $("#view")).forEach((t) => t.onclick = () => { const [pi, gi] = t.dataset.pg.split(":").map(Number); const p = peersShown[pi]; if (p && p.games[gi]) openRemoteItem(p.instance, p.name, { ...p.games[gi], here: "missing" }, true); });
  loadPeerGames();
  loadSyncState().then(() => { if (state.tab === "home" && JSON.stringify(state.todo && state.todo.map((c) => c.title)) !== JSON.stringify(homeTodo().map((c) => c.title))) home(); });
  const pbk = peerBackups();
  $$("[data-pbk]", $("#view")).forEach((t) => t.onclick = () => { const [pi, gi] = t.dataset.pbk.split(":").map(Number); const g = pbk[pi] && pbk[pi].games[gi]; if (g) openVersions({ key: g.item, title_id: g.title_id }); });
  $$("[data-bk]", $("#view")).forEach((t) => t.onclick = () => { const g = bkAvail[+t.dataset.bk]; if (g) openVersions({ key: g.item, title_id: g.title_id }); });
  $$("[data-tm]", $("#view")).forEach((b) => b.onclick = (e) => { e.stopPropagation(); const it = state.items.find((i) => i.key === b.dataset.tm); if (it) openMenu(b, itemActions([it])); });
  $$("[data-todo]").forEach((b) => b.onclick = () => todo[+b.dataset.todo].go());
  state.todo = todo;
  if ($("#home-parties")) $("#home-parties").onclick = () => setTab("saves");
}

// ---- Bibliothèque ---------------------------------------------------------------------
// Comme la télé : filtres Tout / PS5 / PS4 / Applications / ROM, puis les éléments regroupés
// (jeux PS5, jeux PS4, une rangée par console rétro, applications, sauvegardes sans le jeu).
function libraryShelves(items) {
  const ps5 = { title: T("Jeux PS5"), items: [] }, ps4 = { title: T("Jeux PS4"), items: [] }, apps = { title: T("Applications"), items: [] }, orphans = { title: T("Sauvegardes sans le jeu"), items: [] };
  const retro = new Map();
  for (const i of items) {
    if (i.key.startsWith("save:") && i.kind !== "app") orphans.items.push(i);
    else if (i.kind === "game_ps5") ps5.items.push(i);
    else if (i.kind === "game_ps4") ps4.items.push(i);
    else if (i.kind === "app") apps.items.push(i);
    else { if (!retro.has(i.platform)) retro.set(i.platform, { title: i.platform || T("Autres"), items: [] }); retro.get(i.platform).items.push(i); }
  }
  return [ps5, ps4, ...[...retro.values()].sort((a, b) => b.items.length - a.items.length), apps, orphans].filter((s) => s.items.length);
}
function library() {
  if (state.libPeer) return remoteLibrary();
  const f = state.filter;
  const apps = [...new Set(state.items.flatMap((i) => i.apps))].sort();
  state.libSel = state.libSel || new Set();
  for (const k of [...state.libSel]) if (!state.items.some((i) => i.key === k)) state.libSel.delete(k);
  const items = state.items.filter((i) => (f.kind === "ignored" ? i.ignored : !f.kind || i.kind === f.kind) && (!f.app || i.apps.includes(f.app)) && (!f.place || i.place === f.place) &&
    (!f.q || fold(i.title + " " + i.title_id).includes(fold(f.q))));
  const st = state.status || {};
  const kinds = [["", T("Tout")], ["game_ps5", "PS5"], ["game_ps4", "PS4"], ["app", T("Applications")], ["rom", "ROM"], ["ignored", T("Ignorés")]];
  const first = items.find((i) => i.has_cover);
  const shelves = libraryShelves(items);
  const sel = state.libSel;
  $("#view").innerHTML = `<div class="banner" id="lib-banner" ${first ? `style="background-image:url('${coverUrl(first, 512)}')"` : ""}></div>
    <h1>${T("Bibliothèque")}<small>${T("%d sur %d", items.length, state.items.length)} · ${st.scanning ? T("actualisation…") : st.scanned_at ? T("relevé %s", new Date(st.scanned_at * 1000).toLocaleTimeString(I18N.code)) : T("premier relevé…")}</small></h1>
    <div class="filters">
      <div class="pills">${kinds.map(([k, l]) => `<button data-kind="${k}" class="${f.kind === k ? "active" : ""}">${l}</button>`).join("")}</div>
      <select id="f-app"><option value="">${T("Émulateur")}</option>${apps.map((a) => `<option value="${esc(a)}" ${f.app === a ? "selected" : ""}>${esc(a)}</option>`).join("")}</select>
      <select id="f-place"><option value="">${T("Emplacement")}</option>${state.places.filter((p) => !p.remote).map((p) => `<option value="${esc(p.id)}" ${f.place === p.id ? "selected" : ""}>${esc(p.label)}</option>`).join("")}</select>
      <input type="search" id="f-q" placeholder="${esc(T("Rechercher un titre ou un identifiant"))}" value="${esc(f.q)}">
      <button class="secondary" id="rescan">${st.scanning ? SPIN : ""}${T("Actualiser")}</button>
      ${consoleSelectHtml()}
    </div>
    <div class="${sel.size ? "selecting" : ""}">${items.length ? shelves.map((s, si) => `<section class="lib-shelf"><h2>${esc(s.title)} <span class="n">${s.items.length}</span>
      ${allBoxHtml("lib-all-" + si, s.items.filter((i) => sel.has(i.key)).length, s.items.length).replace("<label", `<label data-shelf="${si}"`)}</h2>
      <div class="grid">${s.items.map((it) => tileHtml(it, sel)).join("")}</div></section>`).join("")
      : `<div class="empty"><div class="icon">⌕</div>${T("Aucun élément ne correspond à ces filtres.")}</div>`}</div>`;
  $$(".pills button[data-kind]").forEach((b) => b.onclick = () => { f.kind = b.dataset.kind; library(); });
  $("#f-app").onchange = (e) => { f.app = e.target.value; library(); };
  $("#f-place").onchange = (e) => { f.place = e.target.value; library(); };
  $("#f-q").oninput = (e) => { f.q = e.target.value; clearTimeout(library.t); library.t = setTimeout(() => { library(); $("#f-q").focus(); $("#f-q").setSelectionRange(f.q.length, f.q.length); }, 250); };
  $("#rescan").onclick = async () => { try { await post("/scan"); toast(T("Actualisation demandée")); } catch (e) { toast(e.message); } };
  bindConsoleSelect();
  bindTiles($("#view"));
  $$(".tile[data-key]").forEach((t) => t.onmouseenter = () => {
    const it = state.items.find((i) => i.key === t.dataset.key), b = $("#lib-banner");
    if (it && it.has_cover && b) b.style.backgroundImage = `url('${coverUrl(it, 512)}')`;
  });
  // Règle de sélection : case sur chaque jaquette, « Tout » par rangée, « ⋯ » pour un seul élément.
  $$("[data-tk]").forEach((c) => c.onchange = () => { c.checked ? sel.add(c.dataset.tk) : sel.delete(c.dataset.tk); library(); });
  shelves.forEach((s, si) => {
    const box = $("#lib-all-" + si);
    if (!box) return;
    setAllBox(box, s.items.filter((i) => sel.has(i.key)).length, s.items.length);
    box.onchange = () => { for (const it of s.items) box.checked ? sel.add(it.key) : sel.delete(it.key); library(); };
  });
  $$("[data-tm]").forEach((b) => b.onclick = (e) => { e.stopPropagation(); const it = state.items.find((i) => i.key === b.dataset.tm); if (it) openMenu(b, itemActions([it])); });
  const picked = state.items.filter((i) => sel.has(i.key));
  selBar(picked.length, itemActions(picked, true), () => { sel.clear(); library(); });
}
// ---- Bibliothèque d'une autre PS5 -----------------------------------------------------------------------------
// Menu « Console » : cette PS5 ou une PS5 jumelée. Ses éléments viennent du cache du moteur (aucun appel tant qu'on ne
// relit pas) ; chacun dit s'il est aussi ici et où est la partie la plus récente ; les actions se font là-bas.
function consoleSelectHtml() {
  const peers = (state.linkPeers || []).filter((p) => p.paired);
  if (!peers.length) return "";
  return `<select id="f-console"><option value="">${esc(T("Console : %s", T("cette PS5")))}</option>${peers.map((p) =>
    `<option value="${esc(p.instance)}" ${state.libPeer === p.instance ? "selected" : ""}>${esc(T("Console : %s", p.name || p.host))}${p.online ? "" : " · " + esc(T("hors ligne"))}</option>`).join("")}</select>`;
}
function bindConsoleSelect() {
  if (!state.linkPeers) { mgLoadPeers().then(() => { if (state.tab === "library" && (state.linkPeers || []).some((p) => p.paired)) library(); }); return; }
  const s = $("#f-console");
  if (s) s.onchange = (e) => { state.peerPick = new Set(); state.libPeer = e.target.value; state.peerLib = null; state.libPresence = ""; library(); };
}
async function loadPeerLibrary(fresh) {
  const inst = state.libPeer;
  state.peerLibLoading = true;
  try {
    const d = await api(`/link/peer-library?instance=${encodeURIComponent(inst)}${fresh ? "&fresh=1" : ""}`);
    if (state.libPeer !== inst) return;
    if (d.error) { toast(d.error); state.peerLib = { items: [], name: "", online: false }; }
    else {
      state.peerLib = d;
      if (d.read_error) toast(`${d.name} : ${d.read_error}`);
      if (!fresh && !d.items.length && d.online) return loadPeerLibrary(true);  // rien en cache mais en ligne : une lecture
    }
  } catch (e) { toast(e.message); }
  finally { state.peerLibLoading = false; }
  if (state.tab === "library" && state.libPeer === inst) library();
}
const peerBadge = (it) => it.save === "there" ? [T("À jour là-bas"), "info"] : it.save === "here" ? [T("À jour ici"), "info"]
  : it.here === "missing" ? [T("Manque ici"), "warn"] : it.here === "both" ? [T("Aussi ici"), "ok"] : null;
function remoteLibrary() {
  const d = state.peerLib;
  if (!d) { $("#view").innerHTML = loading(T("Lecture de l'autre PS5…")); if (!state.peerLibLoading) loadPeerLibrary(false); return; }
  const f = state.filter, pres = state.libPresence || "";
  const name = d.name || T("autre PS5");
  const items = (d.items || []).filter((i) => (!f.kind || f.kind === "ignored" || i.kind === f.kind) && (!pres || i.here === pres) &&
    (!f.q || fold(i.title + " " + i.title_id).includes(fold(f.q))));
  const kinds = [["", T("Tout")], ["game_ps5", "PS5"], ["game_ps4", "PS4"], ["app", T("Applications")], ["rom", "ROM"]];
  const age = d.read_at ? Math.max(0, Math.round(Date.now() / 1000 - d.read_at)) : -1;
  const when = !d.online ? T("hors ligne : dernière liste connue") : age < 0 ? "" : age < 90 ? T("lue il y a %d s", age) : T("lue il y a %d min", Math.round(age / 60));
  // Sauvegarde sans le jeu là-bas : rangée « Sauvegardes sans le jeu », comme ici.
  const shelves = libraryShelves(items.map((i) => ({ ...i, key: i.here === "save_only" && !String(i.key).startsWith("save:") ? "save:" + i.key : i.key, _src: i })));
  $("#view").innerHTML = `<h1>${esc(T("Bibliothèque de %s", name))}<small>${T("%d sur %d", items.length, (d.items || []).length)} · ${esc(when)}</small></h1>
    <div class="remote-banner">${esc(T("Vous regardez %s : les actions se font là-bas", name))}</div>
    ${(state.peerPick && state.peerPick.size) ? `<div class="actions pick-bar"><b>${esc(T("%d élément(s) coché(s) sur %s", state.peerPick.size, name))}</b>
      <button class="secondary" id="rp-saves">${esc(T("Sauvegarder leurs parties là-bas (%d)", [...state.peerPick].filter((i) => peerHasSave(d.items[i])).length))}</button>
      <button class="secondary" id="rp-full">${esc(T("Faire un backup complet là-bas (%d)", [...state.peerPick].filter((i) => peerFull(d.items[i])).length))}</button>
      <button class="secondary" id="rp-newer">${esc(T("Prendre ici les parties plus récentes là-bas (%d)", [...state.peerPick].filter((i) => (d.items[i] || {}).save === "there").length))}</button>
      <button class="secondary" id="rp-clear">${esc(T("Tout décocher"))}</button></div>` : ""}
    <div class="filters">
      <div class="pills">${kinds.map(([k, l]) => `<button data-kind="${k}" class="${(f.kind === "ignored" ? "" : f.kind) === k ? "active" : ""}">${l}</button>`).join("")}</div>
      <select id="f-pres"><option value="">${T("Tous ses jeux")}</option><option value="missing" ${pres === "missing" ? "selected" : ""}>${T("Absents d'ici")}</option><option value="both" ${pres === "both" ? "selected" : ""}>${T("Aussi ici")}</option></select>
      <input type="search" id="f-q" placeholder="${esc(T("Rechercher un titre ou un identifiant"))}" value="${esc(f.q)}">
      <button class="secondary" id="peer-reread" ${d.online ? "" : "disabled"}>${state.peerLibLoading ? SPIN : ""}${T("Actualiser")}</button>
      ${consoleSelectHtml()}
    </div>
    ${items.length ? shelves.map((s) => `<section class="lib-shelf"><h2>${esc(s.title)} <span class="n">${s.items.length}</span></h2>
      <div class="grid">${s.items.map((it) => { const b = peerBadge(it); const i = d.items.indexOf(it._src);
        return `<div class="tile ${(state.peerPick || new Set()).has(i) ? "picked" : ""}" data-ri="${i}" tabindex="0" role="button"><label class="pick" title="${esc(T("Cocher"))}"><input type="checkbox" data-rpick="${i}" ${(state.peerPick || new Set()).has(i) ? "checked" : ""}></label>${coverHtml({ key: it.cover_key, has_cover: true, title: it.title, kind: it.kind, platform: it.platform })}${b ? `<span class="peer-badge ${b[1]}">${esc(b[0])}</span>` : ""}
        <div class="name">${esc(cleanTitle(it.title))}</div><div class="meta">${esc(it.image_type || it.platform || "")}</div></div>`; }).join("")}</div></section>`).join("")
      : `<div class="empty"><div class="icon">⌕</div>${T("Aucun élément ne correspond à ces filtres.")}</div>`}`;
  $$(".pills button[data-kind]").forEach((b) => b.onclick = () => { f.kind = b.dataset.kind; library(); });
  $("#f-pres").onchange = (e) => { state.libPresence = e.target.value; library(); };
  $("#f-q").oninput = (e) => { f.q = e.target.value; clearTimeout(library.t); library.t = setTimeout(() => { library(); $("#f-q").focus(); $("#f-q").setSelectionRange(f.q.length, f.q.length); }, 250); };
  $("#peer-reread").onclick = () => { loadPeerLibrary(true); library(); };
  bindConsoleSelect();
  $$("[data-ri]").forEach((t) => t.onclick = (ev) => { if (ev.target.closest(".pick")) return; const it = d.items[+t.dataset.ri]; if (it) openRemoteItem(state.libPeer, name, it, d.online); });
  $$("[data-rpick]").forEach((c) => c.onchange = () => { state.peerPick = state.peerPick || new Set(); const i = +c.dataset.rpick; if (c.checked) state.peerPick.add(i); else state.peerPick.delete(i); library(); });
  const picked = () => [...(state.peerPick || [])].map((i) => d.items[i]).filter(Boolean);
  const run = async (action, keep) => {
    if (!d.online) { toast(T("%s est hors ligne : les actions reviendront quand elle sera allumée.", name)); return; }
    let n = 0;
    for (const it of picked().filter(keep)) { try { await post("/link/peer-action", { instance: state.libPeer, action, item: it.key }); n++; } catch (e) { toast(e.message); } }
    toast(T("%s : %d sauvegarde(s) lancée(s) là-bas (onglet Tâches)", name, n)); state.peerPick = new Set(); library();
  };
  if ($("#rp-saves")) $("#rp-saves").onclick = () => run("backup_saves", peerHasSave);
  if ($("#rp-full")) $("#rp-full").onclick = () => run("backup", peerFull);
  if ($("#rp-newer")) $("#rp-newer").onclick = async () => {
    const ids = picked().filter((it) => it.save === "there" && it.title_id).map((it) => it.title_id);
    try { await post("/sync/parties/start", { peer: state.libPeer, title_ids: ids, one: true }); toast(T("Parties : mise à niveau lancée (l'ancienne est sauvegardée d'abord)")); } catch (e) { toast(e.message); }
    state.peerPick = new Set(); library();
  };
  if ($("#rp-clear")) $("#rp-clear").onclick = () => { state.peerPick = new Set(); library(); };
}
const peerHasSave = (it) => !!it && (String(it.key).startsWith("save:") || it.here === "save_only" || (it.components || []).some((c) => c.part === "save"));
const peerFull = (it) => !!it && !String(it.key).startsWith("save:") && it.here !== "save_only" && (it.components || []).length > 0;
// Fiche d'un élément d'une autre PS5 : ses actions, faites là-bas par son moteur (ou ici quand on récupère).
function openRemoteItem(inst, name, it, online) {
  const saveOnly = String(it.key || "").startsWith("save:");
  const comps = it.components || [];
  const hasSave = saveOnly || comps.some((c) => c.part === "save");
  const bytes = it.bytes || comps.reduce((a, c) => a + c.bytes, 0);
  const game = (it.kind === "game_ps5" || it.kind === "game_ps4") && !saveOnly;
  const acts = [];
  if (online) {
    if (game && it.here !== "both") acts.push(["rs-pull", "primary", T("Récupérer ici…")]);
    if (it.title_id && (it.save === "there" || it.save === "here")) acts.push(["rs-parties", "secondary", it.save === "there" ? T("Prendre ici sa partie (plus récente là-bas)") : T("Envoyer là-bas la partie d'ici (plus récente ici)")]);
    if (hasSave) acts.push(["rs-bksave", "secondary", T("Sauvegarder sa partie là-bas")]);
    if (!saveOnly && comps.length) acts.push(["rs-bkfull", "secondary", T("Faire un backup complet là-bas")]);
    if (canUninstall(it)) acts.push(["rs-uninstall", "danger", T("Désinstaller de %s…", name)]);
  }
  const chips = [it.image_type, bytes ? size(bytes) : "", it.here === "both" ? T("Aussi ici") : it.here === "missing" ? T("Absent d'ici") : T("Sauvegarde seule"),
    it.save === "there" ? T("Partie plus récente là-bas") : it.save === "here" ? T("Partie plus récente ici") : it.save === "same" ? T("Parties identiques") : it.save === "conflict" ? T("Parties : à trancher dans Synchro") : ""].filter(Boolean);
  const ck = it.cover_key || (it.title_id ? (it.kind === "game_ps4" ? "tid:ps4:" : "tid:ps5:") + it.title_id : "");
  $("#sheet-panel").innerHTML = `
    <div class="panel-head">${coverHtml({ key: ck, has_cover: !!ck, title: it.title, kind: it.kind, platform: it.platform }, true)}
      <div><div class="eyebrow">${esc(kindLabel(it))}${it.title_id ? " · " + esc(it.title_id) : ""} · ${esc(T("sur %s", name))}</div>
        <h3>${esc(it.title)}</h3>
        <div class="chips">${chips.map((c) => `<span class="chip">${esc(c)}</span>`).join("")}</div></div></div>
    <p class="hint">${esc(online ? T("Les actions sont faites par %s ; elles apparaissent dans Tâches.", name) : T("%s est hors ligne : dernière liste connue, actions indisponibles.", name))}</p>
    <div class="actions">${acts.map(([id, cls, l]) => `<button class="${cls}" id="${id}">${esc(l)}</button>`).join("")}<button class="secondary" id="rs-close">${T("Fermer")}</button></div>`;
  $("#sheet").classList.remove("hidden");
  $("#rs-close").onclick = closeSheet;
  const backup = async (action) => {
    try { await post("/link/peer-action", { instance: inst, action, item: it.key }); toast(T("%s : sauvegarde de %s lancée là-bas", name, it.title)); closeSheet(); }
    catch (e) { toast(e.message); }
  };
  if ($("#rs-pull")) $("#rs-pull").onclick = (ev) => receivePeerGame(ev.currentTarget, { instance: inst, name }, { key: it.key, title: it.title, title_id: it.title_id, kind: it.kind, bytes });
  if ($("#rs-parties")) $("#rs-parties").onclick = async () => {
    try { await post("/sync/parties/start", { peer: inst, title_ids: [it.title_id], one: true }); toast(T("Parties de %s : mise à niveau lancée (l'ancienne est sauvegardée d'abord)", it.title)); closeSheet(); }
    catch (e) { toast(e.message); }
  };
  if ($("#rs-bksave")) $("#rs-bksave").onclick = () => backup("backup_saves");
  if ($("#rs-bkfull")) $("#rs-bkfull").onclick = () => backup("backup");
  if ($("#rs-uninstall")) $("#rs-uninstall").onclick = () => uninstallGame(inst, name, it.title_id, it.title, it.image_type || "");
}
// Désinstaller : jeu ou appli installés (pas une sauvegarde seule, jamais Backup Center).
const canUninstall = (it) => ["game_ps5", "game_ps4", "app"].includes(it.kind) && !String(it.key || "").startsWith("save:") &&
  /^(PPSA|CUSA)\d{5}$/.test(it.title_id || "") && it.title_id !== "PPSA99950";
// Trois validations : ce qui va se passer + garder (par défaut) ou supprimer l'image ; « définitif » si l'image part ;
// taper DESINSTALLER. Le travail se fait dans le moteur de la console qui a le jeu (ici, ou une PS5 jumelée).
async function uninstallGame(inst, consoleName, tid, title, detail) {
  window.__unDel = "0";
  const head = consoleName ? T("Désinstaller %s de %s ?", title, consoleName) : T("Désinstaller %s de cette PS5 ?", title);
  const html = `<p class="hint">${esc(T("Vos parties sont sauvegardées d'abord (copie restaurable) et restent sur la console."))}<br>${esc(T("Le jeu est retiré de ShadowMountPlus puis désinstallé."))}${detail ? "<br>" + esc(detail) : ""}</p>
    <label><input type="radio" name="un-img" checked onchange="window.__unDel='0'"> ${esc(T("Désinstaller et garder l'image"))}</label><br>
    <label><input type="radio" name="un-img" onchange="window.__unDel='1'"> ${esc(T("Désinstaller et supprimer l'image"))}</label>`;
  if (!await ask(head, html, T("Continuer"))) return;
  const del = window.__unDel === "1";
  if (del && !await ask(T("Définitif"), `<p>${esc(T("Définitif : pas de corbeille."))}</p>`, T("Continuer"))) return;
  window.__unOk = "";
  if (!await ask(T("Dernière confirmation"), `<p>${esc(T("Tapez DESINSTALLER pour confirmer."))}</p><input class="input" autocomplete="off" oninput="window.__unOk=this.value">`, T("Désinstaller"))) return;
  if ((window.__unOk || "").trim().toUpperCase() !== "DESINSTALLER") { toast(T("Désinstallation annulée")); return; }
  try {
    const body = { title_id: tid, delete_source: del, confirm: "DESINSTALLER" };
    if (inst) await post("/link/peer-uninstall", { ...body, instance: inst }); else await post("/games/uninstall", body);
    toast(consoleName ? T("%s : désinstallation de %s lancée", consoleName, title) : T("Désinstallation de %s lancée : suivez-la dans Tâches", title));
    closeSheet(); refresh(true);
  } catch (e) { toast(e.message); }
}
async function peerUninstallList(inst, name) {
  let items = [];
  try { items = await api(`/link/peer?instance=${encodeURIComponent(inst)}&what=items`); } catch (e) { toast(e.message); return; }
  const list = (Array.isArray(items) ? items : items.items || []).filter(canUninstall);
  if (!list.length) { toast(T("Aucun jeu à désinstaller là-bas")); return; }
  window.__unPick = "0";
  const html = `<select onchange="window.__unPick=this.value">${list.map((it, i) => `<option value="${i}">${esc(it.title)}${it.image_type ? " · " + esc(it.image_type) : ""}</option>`).join("")}</select>`;
  if (!await ask(T("Désinstaller un jeu sur %s", name), html, T("Continuer"))) return;
  const it = list[Number(window.__unPick) || 0];
  uninstallGame(inst, name, it.title_id, it.title, it.image_type || "");
}
// Actions sur des éléments de la bibliothèque (menu « ⋯ » d'un élément, ou barre d'actions de la sélection).
function itemActions(list, bar = false) {
  const one = list.length === 1 ? list[0] : null, out = [];
  const done = () => { state.libSel.clear(); refresh(true); if (state.tab === "library") library(); };
  if (one && !bar) out.push({ label: T("Ouvrir la fiche"), run: () => openItem(one) });
  if (one && !bar) out.push({ label: T("Toutes les versions"), run: () => openVersions(one) });
  const live = list.filter((i) => !i.ignored), gone = list.filter((i) => i.ignored);
  if (live.length) out.push({ label: T("Ignorer…"), cls: bar ? "danger" : "bad", run: () => ignoreDialog(live.map(ignoreTarget), -1, done) });
  if (gone.length) out.push({ label: T("Ne plus ignorer"), run: async () => {
    for (const it of gone) { const t = ignoreTarget(it); try { await post("/ignore", { [t.field]: t.value, on: false }); } catch (e) { toast(e.message); } }
    toast(gone.length > 1 ? T("%d élément(s) ne sont plus ignorés", gone.length) : T("« %s » n'est plus ignoré", gone[0].title)); state.overview = null; done();
  } });
  if (one && !bar && hasConsoleSave(one.title_id)) out.push({ label: T("Supprimer la partie de la console…"), cls: "bad", run: () => deleteConsoleSave(document.body, one.title_id, one.title, done) });
  if (one && !bar && canUninstall(one)) out.push({ label: T("Désinstaller de cette PS5…"), cls: "bad", run: () => uninstallGame("", "", one.title_id, one.title, one.image_type || "") });
  const movable = list.filter((i) => i.components && i.components.length && !i.key.startsWith("save:"));
  if (movable.length) out.push({ label: T("Copier vers…"), run: () => copyTo(movable) });
  return out;
}
// Copier vers… : Transferts, depuis la PS5, éléments déjà cochés ; il reste à choisir la destination.
function copyTo(list) {
  const tr = state.tr, cat = TR_CATS.findIndex(([k]) => k === list[0].kind);
  tr.from = "ps5"; tr.screen = "main"; tr.action = 0; tr.cat = cat < 0 ? 0 : cat;
  tr.sel = new Set(list.map((i) => i.key));
  if (!tr.to || tr.to === tr.from) tr.to = (trDevices().find((d) => d.id !== "ps5" && d.kind !== "switch" && d.kind !== "internal") || {}).id || "";
  state.libSel.clear();
  closeSheet(); setTab("transfers");
  toast(T("Choisissez la destination, puis Copier ou Déplacer."));
}

// ---- Fiche d'un élément : que sauvegarder, où ; outils de l'image pour les jeux --------------
function openItem(it) {
  const parts = it.components.map((c) => `
    <label class="part"><input type="checkbox" data-part="${c.part}" checked><span class="label">${esc(c.label || PARTS[c.part])}</span><span class="size">${size(c.bytes)}</span></label>`).join("");
  const total = it.components.reduce((a, c) => a + c.bytes, 0);
  // Destinations : interne, disques USB, appareils réseau (NAS / FTP). Une Switch est une source, pas une destination.
  const dests = state.places.filter((p) => p.type !== "switch" && p.use !== "sync");  // NAS « Synchro seulement » : pas une destination
  const game = it.kind === "game_ps5" || it.kind === "game_ps4";
  $("#sheet-panel").innerHTML = `
    <div class="panel-head">${coverHtml(it, true)}
      <div><div class="eyebrow">${esc(kindLabel(it))}${it.title_id ? " · " + esc(it.title_id) : ""}</div>
        <h3>${esc(it.title)}${it.ignored ? ` <span class="tag warn">${T("Ignoré")}</span>` : ""}</h3>
        <div class="chips"><span class="chip">${esc(placeLabel(it.place))}</span>${it.image_type ? `<span class="chip">${esc(it.image_type)}</span>` : ""}${it.apps.map((a) => `<span class="chip">${esc(a)}</span>`).join("")}</div></div></div>
    <div id="it-compat" class="compat"></div>
    <h2>${T("Que sauvegarder ?")}</h2>
    <div class="parts">${parts}</div>
    <div class="actions">
      <select id="dest"><option value="auto">${T("Automatique (premier disque USB, sinon interne)")}</option>${dests.map((p) => `<option value="${esc(p.id)}">${esc(p.label)} · ${p.remote || p.type === "ftp" ? T("réseau") : T("%s libres", size(p.free))}</option>`).join("")}</select>
      <button class="primary" id="go">${T("Sauvegarder (%s)", size(total))}</button>
      <button class="secondary" id="close">${T("Fermer")}</button>
    </div>
    <div class="actions"><button class="secondary" id="it-versions">${T("Toutes les versions")}</button><button class="secondary" id="it-plans">${T("Plans de ce jeu…")}<span class="hint" id="it-plans-n"></span></button><button class="secondary" id="it-browse">${T("Parcourir l'emplacement")}</button><button class="secondary" id="it-send">${T("Envoyer à une PS5…")}</button>
      <button class="${it.ignored ? "secondary" : "danger"}" id="it-ignore">${it.ignored ? T("Sauvegarder de nouveau") : T("Ne plus sauvegarder…")}</button>
      ${hasConsoleSave(it.title_id) ? `<button class="danger" id="it-delsave">${T("Supprimer la partie de la console…")}</button>` : ""}
      ${canUninstall(it) ? `<button class="danger" id="it-uninstall">${T("Désinstaller…")}</button>` : ""}</div>
    <p class="hint">${T("La sauvegarde continue même si vous fermez cette page ou l'application sur la télé.")}</p>
    ${game ? `<h2>${T("Outils de l'image")}</h2><div id="img-tools">${loading(T("Lecture des images…"))}</div>` : ""}
    <table class="info">
      <tr><td>${T("Emplacement")}</td><td>${esc(placeLabel(it.place))}</td></tr>
      <tr><td>${T("Format")}</td><td>${esc(it.image_type || "—")}</td></tr>
      <tr><td>${T("Utilisé par")}</td><td>${esc(it.apps.join(", ") || "—")}</td></tr>
      <tr><td>${T("Taille totale")}</td><td>${size(total)}</td></tr>
      <tr><td>${T("Identifiant")}</td><td>${esc(it.title_id || "—")}</td></tr>
    </table>`;
  $("#sheet").classList.remove("hidden");
  const sel = () => $$(".part input:checked").map((x) => x.dataset.part);
  const upd = () => {
    const chosen = sel();
    const bytes = it.components.filter((c) => chosen.includes(c.part)).reduce((a, c) => a + c.bytes, 0);
    $("#go").textContent = T("Sauvegarder (%s)", size(bytes)); $("#go").disabled = !chosen.length;
  };
  $$(".part input").forEach((x) => x.onchange = upd);
  $("#close").onclick = closeSheet;
  $("#go").onclick = async () => {
    const chosen = [...new Set(sel())], all = [...new Set(it.components.map((c) => c.part))];
    try {
      await post("/tasks", { type: "backup", item: it.key, parts: chosen.length === all.length ? [] : chosen, place: $("#dest").value });
      closeSheet(); toast(T("Sauvegarde ajoutée aux tâches")); refresh(true);
    } catch (e) { toast(e.message); }
  };
  $("#it-plans").onclick = () => itemPlans(it);
  // Compatibilité avec cette console : identité du jeu (version, SDK des exécutables, minimum déclaré) et état.
  if (it.kind === "game_ps5" || it.kind === "app") api(`/compat?item=${encodeURIComponent(it.key)}`).then((c) => {
    const el = $("#it-compat");
    if (!el || !c || !c.state) return;
    const id = c.identity || {};
    const idl = [id.version && T("Version %s", id.version), id.exe_sdk_max && T("SDK %s", id.exe_sdk_max), id.declared_min && T("minimum déclaré %s", id.declared_min)].filter(Boolean).join(" · ");
    const why = compatReasons(c);
    const bt = compatBackportText(c), bl = bt ? T("Backport : %s", bt) : T("Aucun backport trouvé pour ce jeu");
    const peers = (c.peers || []).map((pc) => `<div class="${compatClass(pc)}">● ${esc(compatStateText(pc))}</div>`).join("");
    el.innerHTML = `<div class="${compatClass(c)}">● ${esc(compatStateText(c))}</div>${idl ? `<div class="hint">${esc(idl)}</div>` : ""}<div class="hint">${esc(bl)}</div>${peers}`;
  }).catch(() => {});
  $("#it-versions").onclick = () => openVersions(it);
  // Envoyer ce jeu à une PS5 jumelée : copie vérifiée sur son disque, puis installation et sauvegarde re-signée là-bas.
  $("#it-send").onclick = (ev) => {
    const peers = state.places.filter((p) => p.type === "peer");
    if (!peers.length) { toast(T("Aucune autre PS5 jumelée et allumée : Appareils › PlayStation 5.")); return; }
    const btn = ev.currentTarget;
    const disks = () => openMenu(btn, peers.map((p) => ({ label: `${p.label} · ${T("%s libres", size(p.free))}`, run: async () => (await systemOk("/link/send-check", { place: p.id, item: it.key }, it.title)) && chooseFormat(btn, async (format) => {
      const inst = p.id.split(":")[1];
      pickAccount(btn, inst, p.label, (uid) => {
        const go = async (keep) => {
          try { await post("/link/send-game", { place: p.id, item: it.key, format, user_id: uid, keep_copy: keep }); closeSheet(); toast(T("%s part vers %s : copie, installation et sauvegarde, suivies dans Tâches ; il apparaît là-bas une fois l'application fermée", it.title, p.label)); refresh(true); }
          catch (e) { toast(e.message); }
        };
        // Par défaut, pas de doublon : le jeu n'existera qu'une fois là-bas (dossier de jeux).
        setTimeout(() => openMenu(btn, [
          { label: T("Envoyer (le jeu n'est gardé qu'une fois, installé)"), run: () => go(false) },
          { label: T("Envoyer et garder aussi une copie de secours là-bas"), run: () => go(true) },
        ]), 0);
      });
    }) })));
    // Le jeu (installé là-bas avec sa sauvegarde), ou seulement sa sauvegarde, comme sur la télé.
    if (!it.components.some((c) => c.part === "save")) return disks();
    openMenu(btn, [
      { label: T("Le jeu, installé là-bas avec sa sauvegarde"), run: () => setTimeout(disks, 0) },
      { label: T("Sa sauvegarde seulement"), run: () => sendSaveToPeer(btn, it.key, it.title, "") },
    ]);
  };
  if ($("#it-delsave")) $("#it-delsave").onclick = (ev) => deleteConsoleSave(ev.currentTarget, it.title_id, it.title, () => { closeSheet(); refresh(true); });
  if ($("#it-uninstall")) $("#it-uninstall").onclick = () => uninstallGame("", "", it.title_id, it.title, it.image_type || "");
  $("#it-ignore").onclick = async () => {
    if (!it.ignored) { ignoreDialog([ignoreTarget(it)], -1, () => { closeSheet(); refresh(true); }); return; }
    const t = ignoreTarget(it);
    try { await post("/ignore", { [t.field]: t.value, on: false }); toast(T("« %s » n'est plus ignoré", it.title)); closeSheet(); state.overview = null; refresh(true); }
    catch (e) { toast(e.message); }
  };
  (state.plans ? Promise.resolve(state.plans) : loadPlans()).then((pl) => {
    const n = pl.filter((x) => (x.items || []).includes(it.key)).length, el = $("#it-plans-n");
    if (el && n) el.textContent = " · " + T("dans %d plan(s)", n);
  });
  $("#it-browse").onclick = async () => {
    const loc = await itemLocation(it);
    if (!loc) { toast(T("Emplacement inconnu pour cet élément")); return; }
    openBrowser({ title: it.title, start: loc });
  };
  if (game) imageTools(it);
}
// Où vit l'élément sur la console : dossier de la ROM, image ou dossier du jeu, sinon son inscription.
async function itemLocation(it) {
  const dir = (p) => { const i = p.lastIndexOf("/"); return i <= 0 ? "/" : p.slice(0, i); };
  for (const c of it.components) {
    if (c.sources && c.sources.length) return c.sources[0].dir ? c.sources[0].source : dir(c.sources[0].source);
  }
  if (it.key.startsWith("rom:")) return dir(it.key.slice(4));
  const m = await loadImages();
  const g = m.get(it.key);
  if (g) return g.storage === "folder" ? g.path : dir(g.path);
  for (const c of it.components) { const mt = c.meta || {}; if (mt.image) return dir(mt.image); if (mt.registration) return mt.registration; }
  return "";
}
function closeSheet() { $("#sheet").classList.add("hidden"); }
$("#sheet").onclick = (e) => { if (e.target.id === "sheet") closeSheet(); };
document.addEventListener("keydown", (e) => { if (e.key === "Escape") { if ($("#popmenu")) return closeMenu(); const m = $$(".modal").pop(); if (m) m.remove(); else closeSheet(); } });

// Images des jeux (/images) : comment et où vit chaque jeu (installé, dossier, image .ffpfsc…).
async function loadImages(force = false) {
  if (!force && state.images && Date.now() - state.imagesAt < 20000) return state.images;
  if (loadImages.p) return loadImages.p;
  loadImages.p = api("/images?measure=0").then((j) => {
    const m = new Map();
    for (const g of j.images || []) if (g.item && !m.has(g.item)) m.set(g.item, g);
    state.images = m; state.imagesAt = Date.now(); return m;
  }).catch(() => state.images || new Map()).finally(() => { loadImages.p = null; });
  return loadImages.p;
}
function imageActions(g) {
  const st = g.storage, ct = g.container;
  if (st === "native") return [["speed", T("Test de vitesse de lecture")], ["native", T("Copier / déplacer : par le menu Stockage de la PS5")]];
  if (st === "image" && ct === "ffpfsc") return [["validate", T("Vérifier l'image")], ["repair", T("Vérifier et réparer")], ["uncompress", T("Décompresser en dossier")],
    ["uncompress_exfat", T("Décompresser en image .exfat")], ["read_only", T("Lecture seule (ShadowMount)")], ["speed", T("Test de vitesse de lecture")], ["transfer", T("Copier ou déplacer vers un autre disque…")]];
  if (st === "image") return [["validate", T("Vérifier l'image")], ["extract", T("Extraire en dossier")], ...(ct !== "ffpkg" ? [["compress", T("Compresser en .ffpfsc")]] : []),
    ["speed", T("Test de vitesse de lecture")], ["transfer", T("Copier ou déplacer vers un autre disque…")]];
  return [["compress", T("Compresser en .ffpfsc")], ["make_image", T("Créer une image .exfat")], ["ampr", T("Construire l'index AMPR")],
    ["speed", T("Test de vitesse de lecture")], ["transfer", T("Copier ou déplacer vers un autre disque…")]];
}
const storageText = (g) => g.storage === "native" ? T("installé (natif)") : g.storage === "image" ? T("image .%s", g.container) : T("dossier");
async function imageTools(it) {
  const m = await loadImages();
  const box = $("#img-tools");
  if (!box) return;
  const g = m.get(it.key);
  if (!g) { box.innerHTML = `<p class="hint">${T("Aucune image ni dossier de jeu trouvé pour ce jeu.")}</p>`; return; }
  const where = g.disk && g.disk.label ? g.disk.label : "";
  // Le vrai disque du jeu (l'image peut être sur une clé USB alors que l'inscription est sur l'interne).
  const chip = $("#sheet-panel .panel-head .chip");
  if (chip && where) chip.textContent = where;
  box.innerHTML = `<p class="hint">${esc(storageText(g))}${where ? " · " + esc(where) : ""}${g.running ? " · " + T("en cours de jeu") : g.mounted ? " · " + T("monté") : ""} · <span class="mono">${esc(g.path)}</span></p>
    <div class="actions"><button class="secondary" data-tools>${T("Outils de l'image…")}</button></div>`;
  // Un seul bouton : les outils s'ouvrent dans un menu (comme sur la télé).
  const browse = () => {
    const dir = g.storage === "folder" ? g.path : g.path.replace(/\/[^/]*$/, "") || "/";
    openBrowser({ title: it.title, start: dir });
  };
  $("[data-tools]", box).onclick = (ev) => openMenu(ev.currentTarget, [
    ...imageActions(g).map(([a, l]) => ({ label: l, hint: ({ validate: T("relit tout et contrôle chaque bloc, sans rien modifier"), repair: T("contrôle, puis corrige les blocs abîmés"), uncompress: T("recrée le dossier du jeu à côté"), uncompress_exfat: T("image non compressée, pour les consoles qui ne lisent pas le .ffpfsc"), read_only: T("l'image est montée sans écriture (conseillé pour APR-EMU)"), speed: T("mesure la vitesse de lecture du disque du jeu"), transfer: T("vers un autre disque, copie vérifiée"), extract: T("recrée le dossier du jeu à côté"), compress: T("prend moins de place, se lance tel quel"), make_image: T("regroupe le dossier en une seule image"), ampr: T("pour les jeux APR-EMU : prépare leur index") })[a], run: () => runImageTool(it, g, a) })),
    { label: T("Parcourir le dossier"), run: browse },
  ]);
}
const TOOL_NAMES = { validate: T("Vérifier l'image"), repair: T("Vérifier et réparer"), uncompress: T("Décompresser en dossier"), uncompress_exfat: T("Décompresser en image .exfat"),
  read_only: T("Lecture seule (ShadowMount)"), speed: T("Test de vitesse de lecture"), extract: T("Extraire en dossier"), compress: T("Compresser en .ffpfsc"),
  make_image: T("Créer une image .exfat"), ampr: T("Construire l'index AMPR") };
async function runImageTool(it, g, a) {
  if (a === "native") { toast(T("Jeu installé par la PS5 : Réglages › Stockage pour le déplacer, ou extrayez-le d'abord en dossier.")); return; }
  if (a === "transfer") {  // Transferts, source = disque du jeu, jeu déjà coché
    const tr = state.tr, devs = trDevices();
    tr.from = g.disk && devs.some((d) => d.id === g.disk.id) ? g.disk.id : "ps5";
    if (!tr.to || tr.to === tr.from) tr.to = (devs.find((d) => d.id !== tr.from && d.id !== "ps5" && d.kind !== "switch") || {}).id || "";
    tr.screen = "main"; tr.action = 0; tr.sel = new Set([it.key]);
    closeSheet(); setTab("transfers");
    toast(T("Choisissez la destination, puis Copier ou Déplacer."));
    return;
  }
  const bodies = { validate: { type: "validate_image" }, repair: { type: "validate_image", repair: true }, uncompress: { type: "uncompress", to: "folder" },
    uncompress_exfat: { type: "uncompress", to: "exfat" }, read_only: { type: "set_read_only" }, speed: { type: "read_speed_test", seconds: 20 },
    extract: { type: "extract_image" }, compress: { type: "compress" }, make_image: { type: "make_image" }, ampr: { type: "build_ampr_index" } };
  const body = { ...bodies[a], item: it.key };
  // D'abord le plan (place nécessaire, jeu en cours, image montée…), puis la tâche.
  let p;
  try { p = await post("/images/plan", body); } catch (e) { toast(e.message); return; }
  if (p && p.ok === false) { toast(p.error || T("Opération impossible")); return; }
  const rows = [p.source && [T("Source"), p.source], p.output && [T("Résultat"), p.output], p.source_bytes && [T("Taille"), size(p.source_bytes)],
    p.required_bytes && [T("Place nécessaire"), p.estimate ? T("environ %s", size(p.required_bytes)) : size(p.required_bytes)], p.free_bytes && [T("Place libre"), size(p.free_bytes)],
    p.missing_bytes && [T("Manque"), size(p.missing_bytes)]].filter(Boolean);
  const ok = await ask(`${TOOL_NAMES[a]} · ${cleanTitle(it.title)}`, `<table class="info">${rows.map(([k, v]) => `<tr><td>${esc(k)}</td><td class="mono">${esc(v)}</td></tr>`).join("")}</table>
    <p class="hint">${T("Un jeu en cours d'utilisation n'est jamais touché : la tâche attend qu'il soit fermé.")}</p>`, T("Lancer"));
  if (!ok) return;
  try { await post("/tasks", body); toast(T("Ajouté aux tâches : suivez-le dans Transferts › Transferts en cours")); closeSheet(); refresh(true); }
  catch (e) { toast(e.message); }
}

// ---- Explorateur de dossiers (console, disque ou appareil réseau) ------------------------------
function openBrowser({ title, start, remote = "", config = null, pick = null, files = false }) {
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog wide"><h2>${esc(title)}</h2><div class="br-path"><button class="secondary small" id="br-up">↑ ${T("Dossier parent")}</button><span class="mono" id="br-path"></span></div>
    <div class="br-list" id="br-list"></div>
    <div class="actions">${pick && !files ? `<button class="primary" id="br-pick">${T("Choisir ce dossier")}</button>` : ""}${pick && files ? `<span class="hint">${T("Touchez un fichier pour le choisir.")}</span>` : ""}<button class="secondary" id="br-close">${T("Fermer")}</button></div></div>`;
  document.body.appendChild(box);
  let path = start || "/";
  const go = async (p) => {
    path = p || "/";
    $("#br-path", box).textContent = path;
    $("#br-list", box).innerHTML = loading(T("Lecture du dossier…"));
    try {
      const j = config ? await post("/remotes/browse", { ...config, path })
        : await api(remote.startsWith("peer:") ? `/link/peer-list?instance=${encodeURIComponent(remote.slice(5))}&path=${encodeURIComponent(path)}`
          : remote ? `/remotes/${encodeURIComponent(remote)}/list?path=${encodeURIComponent(path)}` : `/fs/list?path=${encodeURIComponent(path)}`);
      if (path !== p && p) return;
      const list = (j.entries || []).slice().sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
      $("#br-list", box).innerHTML = list.length ? list.map((e, i) => `<button class="br-row ${e.dir ? "dir" : ""}" data-i="${i}" ${e.dir ? "" : "disabled"}>
        <span class="br-ico">${e.dir ? "📁" : "📄"}</span><span class="grow">${esc(e.name)}</span><span class="hint">${e.dir ? "" : size(e.size)}</span></button>`).join("").replace(/ disabled>/g, pick && files ? ">" : " disabled>")
        : `<div class="empty">${T("Dossier vide.")}</div>`;
      $$(".br-row.dir", box).forEach((b) => b.onclick = () => go((path === "/" ? "" : path) + "/" + list[+b.dataset.i].name));
      if (pick && files) $$(".br-row:not(.dir)", box).forEach((b) => b.onclick = () => { pick((path === "/" ? "" : path) + "/" + list[+b.dataset.i].name); box.remove(); });
    } catch (e) { $("#br-list", box).innerHTML = `<div class="empty">${esc(e.message || T("dossier illisible"))}</div>`; }
  };
  $("#br-up", box).onclick = () => { if (path === "/" || !path) return; const i = path.lastIndexOf("/"); go(i <= 0 ? "/" : path.slice(0, i)); };
  $("#br-close", box).onclick = () => box.remove();
  if (pick && !files) $("#br-pick", box).onclick = () => { if (path === "/") { toast(T("Choisissez un dossier.")); return; } pick(path); box.remove(); };
  go(path);
}

// ---- Appareils (ex-Destinations) ---------------------------------------------------------------------
// Images d'appareils : les mêmes que l'app télé (engine/web/devices/*.png, tirées de make_devices.py).
const DEVICE_FILES = { ps5: "internal", internal: "internal", usb: "usb", switch: "switch", consoles: "consoles", nas: "nas",
  network: "network", cloud: "cloud", synology: "synology", qnap: "qnap", x: "x", youtube: "youtube", reddit: "reddit", discord: "discord" };
const ICONS = new Proxy({}, { get: (_, k) => `<img class="dev-img" src="/devices/${DEVICE_FILES[k] || "network"}.png" alt="">` });
function devCardHtml(p) {
  const def = state.settings.default_place === p.id;
  const tag = def ? T("Par défaut") : p.type === "usb" ? (p.marked ? T("Reconnu") : T("Nouveau")) : "PS5";
  const used = p.total ? (p.total - p.free) / p.total : 0;
  const links = (p.type === "internal" ? [`<button class="go" data-go-party="ps5">${T("Ses parties")}</button>`] : []).concat(`<button class="go" data-go-backups="${esc(p.id)}">${T("Ses sauvegardes")}</button>`);
  return `<div class="dev ${def ? "selected" : ""}"><div class="dev-ico">${p.type === "usb" ? ICONS.usb : ICONS.ps5}</div><div class="dev-body">
    <div class="title"><b>${esc(p.label)}</b><span class="tag ${def ? "info" : ""}">${tag}</span></div>
    <div class="hint">${p.type === "usb" ? T("Disque USB") + " · " + esc(p.mount) : T("Stockage interne de la PS5")}</div>
    <div class="bar"><span style="width:${Math.max(2, Math.round(used * 100))}%"></span></div>
    <div><b>${T("%s libres", size(p.free))}</b> <span class="hint">${T("sur %s", size(p.total))}</span></div>
    <div class="dev-actions">${links.join("")}${def ? "" : `<button data-default="${esc(p.id)}">${T("Destination par défaut")}</button>`}<button data-browse-place="${esc(p.id)}">${T("Parcourir")}</button></div></div></div>`;
}
function remoteCardHtml(r) {
  const sw = r.type === "switch", c = r.config || {};
  const links = sw ? [`<button class="go" data-go-party="switch:${esc(r.id)}">${T("Ses parties")}</button>`, `<button class="go" data-go-sync="switch:${esc(r.id)}">${T("Synchroniser")}</button>`]
    : [`<button class="go" data-go-backups="${esc(r.id)}">${T("Ses sauvegardes")}</button>`, `<button class="go" data-go-sync="nas:${esc(r.id)}">${T("Synchroniser")}</button>`];
  const drive = r.type === "gdrive" || r.type === "onedrive" || r.type === "dropbox";
  const s3 = r.type === "s3" || drive;  // stockage dans le nuage : service et compte plutôt qu'une adresse
  const where = drive ? esc(c.bucket || "") : s3 ? `${c.service === "b2" ? "Backblaze B2" : c.service === "other" ? esc(c.host || "") : "Amazon S3"}${c.region ? " · " + esc(c.region) : ""} · ${esc(c.bucket || "")}`
    : `${esc(c.host || "")}:${esc(c.port || "")}`;
  return `<div class="dev"><div class="dev-ico">${sw ? ICONS.switch : s3 ? ICONS.cloud : ICONS.nas}</div><div class="dev-body">
    <div class="title"><b>${esc(r.name)}</b><span class="tag ${sw ? "sw" : "info"}">${sw ? "Switch" : drive ? (r.type === "gdrive" ? "Drive" : r.type === "dropbox" ? "Dropbox" : "OneDrive") : s3 ? (c.service === "b2" ? "B2" : "S3") : r.type === "webdav" ? "WebDAV" : "FTP"}</span></div>
    <div class="hint">${where}${c.root && c.root !== "/" ? " · " + esc(c.root) : ""}</div>
    <div class="dev-line">${sw ? T("Un joueur : ses parties et leur synchro avec Eden") : `<span class="tag ${r.use === "sync" ? "off" : "ok"}">${T("Sauvegardes")}</span><span class="tag ${r.use === "backups" ? "off" : "ok"}">${T("Synchro")}</span>`}</div>
    <div class="dev-actions">${links.join("")}<button data-edit-remote="${esc(r.id)}">${T("Régler")}</button><button data-browse-remote="${esc(r.id)}">${T("Parcourir")}</button></div></div></div>`;
}
// Mettre à jour une autre PS5 depuis celle-ci (comme la télé) : elle télécharge notre version, vérifiée chez elle.
const verCmp = (a, b) => { const x = String(a || "").split("."), y = String(b || "").split("."); for (let i = 0; i < Math.max(x.length, y.length); i++) { const d = (parseInt(x[i], 10) || 0) - (parseInt(y[i], 10) || 0); if (d) return d < 0 ? -1 : 1; } return 0; };
function peerUpdateButton(p, pname) {
  // Sa version en marche et sa version installée (lues chez elle), comparées à la nôtre : la bonne action seulement.
  const mine = (state.status || {}).version, ps = (state.peerStatus || {})[p.instance] || {};
  const running = ps.version || p.version, installed = (ps.update || {}).installed || running;
  const open = `<button data-peer-open="${esc(p.instance)}" data-name="${esc(pname)}">${esc(T("Ouvrir Backup Center sur %s", pname))}</button>`;
  if (installed && mine && verCmp(installed, mine) > 0) return `<button data-peer-pull="${esc(p.instance)}" data-name="${esc(pname)}" data-theirs="${esc(installed)}">${esc(T("Récupérer la %s depuis %s", installed, pname))}</button>` + open + `<button data-peer-closeapp="${esc(p.instance)}" data-name="${esc(pname)}">${esc(T("Fermer Backup Center sur %s", pname))}</button>`;
  if (installed && mine && verCmp(installed, mine) < 0) return `<button data-peer-update="${esc(p.instance)}" data-name="${esc(pname)}" data-theirs="${esc(running)}">${esc(T("Mettre à jour vers %s (elle est en %s)", mine, running))}</button>` + open;
  const close = `<button data-peer-closeapp="${esc(p.instance)}" data-name="${esc(pname)}">${esc(T("Fermer Backup Center sur %s", pname))}</button>`;
  if (installed && running && verCmp(running, installed) < 0) return `<button data-peer-restart="${esc(p.instance)}" data-name="${esc(pname)}" data-v="${esc(installed)}">${esc(T("Relancer son moteur (%s installée, %s en marche)", installed, running))}</button>` + open + close;
  // même version : relancer son moteur quand même (bloqué, lent…) — le jeu en cours n'est pas touché
  return `<button data-peer-restart="${esc(p.instance)}" data-name="${esc(pname)}" data-v="">${esc(T("Relancer le moteur de Backup Center sur %s", pname))}</button>` + open + close;
}
const pbcUpdatedAt = () => { try { return Number(sessionStorage.getItem("pbcUpdatedAt")) || 0; } catch (e) { return 0; } };
function peerUpdateLine(p) {
  if (state.updPush === undefined) { state.updPush = {}; setTimeout(pollUpdPush, 0); }  // état au premier affichage
  // Comme la télé : seulement quand c'est utile (en cours, échouée, pas encore en marche, versions différentes).
  const u = state.updPush || {}, me = state.status || {};
  const mine = (me.update || {}).installed || me.version, theirs = p.version, ours = u.peer === p.instance && u.state, v = u.version || "", st = u.state;
  let line = "", cls = "";
  const lu = me.update || {};
  if (!ours && (lu.state === "downloading" || lu.state === "installing") && lu.received_from === (p.name || p.host)) line = lu.state === "downloading" ? T("Mise à jour : téléchargement…") : T("Mise à jour : installation…");
  else if (ours && !u.finished) line = st === "downloading" ? T("Mise à jour : téléchargement…") : st === "installing" ? T("Mise à jour : installation…") : st === "restarting" ? T("Mise à jour : relance du moteur…") : T("Mise à jour : préparation…");
  else if (ours && st === "failed" && Date.now() / 1000 - u.finished < 600) { line = T("Mise à jour impossible : %s", u.message || ""); cls = "bad"; }
  else if (ours && st === "done" && Date.now() / 1000 - u.finished < 60) { line = T("Mise à jour : %s en marche", v); cls = "ok"; }
  else if (!ours && lu.received_from === (p.name || p.host) && pbcUpdatedAt() && Date.now() - pbcUpdatedAt() < 60000) { line = T("Mise à jour : %s en marche", me.version); cls = "ok"; }
  else if (ours && st === "installed_not_restarted" && theirs !== v) { line = T("Mise à jour : %s installée, active au prochain démarrage", v); cls = "warn"; }
  else if (p.installed && p.installed !== theirs && mine && verCmp(p.installed, mine) >= 0) { line = T("Mise à jour : %s installée, active au prochain démarrage", p.installed); cls = "warn"; }  // déjà installée là-bas
  else if (theirs && mine && verCmp(theirs, mine) < 0) { line = T("Mise à jour : %s → %s", p.installed && verCmp(p.installed, theirs) > 0 ? p.installed : theirs, mine); cls = "warn"; }
  else if (theirs && mine && verCmp(theirs, mine) > 0) { line = T("Mise à jour disponible : %s", theirs); cls = "warn"; }
  return line ? `<div class="hint ${cls}">${esc(line)}</div>` : "";
}
async function pollUpdPush() {
  try { state.updPush = await api("/update/push"); } catch (e) { return; }
  if (state.tab === "devices") render();
  if (state.updPush && state.updPush.state && !state.updPush.finished) setTimeout(pollUpdPush, 2000);
}
// Disques des PS5 jumelées : une carte par disque (comme ceux d'ici) ; hors ligne : une carte pour la console.
function peerCardsHtml() {
  return (state.linkPeers || []).filter((p) => p.paired).map((p) => {
    const disks = state.places.filter((x) => x.type === "peer" && x.id.startsWith(`peer:${p.instance}:`));
    if (!disks.length) return peerCardHtml(p);
    const pname = p.name || p.host;
    // Une carte par console : son nom en titre, puis ses disques et leur place libre (ce sont SES disques, pas ceux d'ici).
    const cut0 = disks[0].label.lastIndexOf(" · "), fwm = (cut0 < 0 ? "" : disks[0].label.slice(0, cut0)).match(/\(([^)]+)\)$/);
    const rows = disks.map((d) => {
      const c = d.label.lastIndexOf(" · "), dn = (c < 0 ? d.label : d.label.slice(c + 3)).trim();
      const pct = d.total ? Math.round(((d.total - d.free) / d.total) * 100) : 0;
      const start = dn === "Interne" || dn === T("Interne") ? "/data" : (d.label.match(/USB (\d+)/) ? "/mnt/usb" + d.label.match(/USB (\d+)/)[1] : "/");
      return `<div class="peer-disk"><button class="link" data-peer-browse="${esc(p.instance)}" data-start="${esc(start)}" data-title="${esc(pname + " · " + dn)}"><b>${esc(dn)}</b></button><span class="bar"><span style="width:${Math.max(2, pct)}%"></span></span><span class="hint">${T("%s libres", size(d.free))}${d.total ? " · " + T("sur %s", size(d.total)) : ""}</span></div>`;
    }).join("");
    return `<div class="dev"><div class="dev-ico">${ICONS.ps5}</div><div class="dev-body">
        ${peerUpdateLine(p)}<div class="title"><b>${esc(pname)}</b>${p.remote ? `<span class="tag ok">${T("À distance")}</span>` : `<span class="tag info">${T("Autre PS5")}</span>`}</div>
        <div class="hint">${(p.remote ? esc(wanWhere(p)) + (fwm ? " · " + esc(fwm[1]) : "") : p.host ? esc(p.host) + (fwm ? " · " + esc(fwm[1]) : "") : fwm ? T("système %s · %d disque(s)", fwm[1], disks.length) : T("%d disque(s)", disks.length)) + peerVerMs(p)}</div>
        ${rows}
        <div class="dev-actions"><button data-peer-parties="${esc(p.instance)}" data-name="${esc(pname)}">${T("Ses parties")}</button><button class="go" data-peer-more>${T("Actions")} ⋯</button></div>
        <div class="peer-more" hidden>${peerUpdateButton(p, pname)}<button data-peer-uninstall="${esc(p.instance)}" data-name="${esc(pname)}">${esc(T("Désinstaller un jeu sur %s…", pname))}</button><button data-peer-sendgame data-name="${esc(pname)}">${T("Envoyer un jeu…")}</button><button data-peer-sendsave data-name="${esc(pname)}">${T("Envoyer une sauvegarde…")}</button><button data-peer-close="${esc(p.instance)}" data-name="${esc(pname)}">${T("Fermer l'application ouverte…")}</button>${p.remote ? `<button data-peer-wantest="${esc(p.instance)}" data-name="${esc(pname)}">${T("Tester la connexion")}</button><button data-peer-session="${esc(p.instance)}">${esc(T("Qui ouvre la session : %s", wanDirLabel(p, pname)))}</button>` : ""}<button data-peer-forget="${esc(p.instance)}" data-name="${esc(pname)}">${T("Oublier cette console…")}</button></div></div></div>`;
  }).join("");
}
// Autre PS5 jumelée (liaison) : une carte comme les autres appareils.
function peerCardHtml(p) {
  return `<div class="dev"><div class="dev-ico">${ICONS.ps5}</div><div class="dev-body">
    <div class="title"><b>${esc(p.name || p.host)}</b><span class="tag ${p.online ? "ok" : "muted"}">${p.online ? T("En ligne") : T("Hors ligne")}</span></div>
    <div class="hint">${esc(p.remote ? wanWhere(p) : T("PS5 jumelée · %s", p.host))}</div>
    <div class="dev-line">${T("Envoyez-lui des sauvegardes : Sauvegardes › un jeu › ⋯ › « Envoyer à … »")}</div>
    <div class="dev-actions"><button class="go" data-peer-disks="${esc(p.instance)}">${T("Voir ses disques")}</button>${p.remote ? `<button data-peer-wantest="${esc(p.instance)}" data-name="${esc(p.name || p.host)}">${T("Tester la connexion")}</button>` : ""}<button data-peer-forget="${esc(p.instance)}" data-name="${esc(p.name || p.host)}">${T("Oublier cette console…")}</button></div></div></div>`;
}
const wanWhere = (p) => ((p.wan_host || p.seen_ip) ? T("par Internet · %s", p.wan_host || p.seen_ip) : T("par Internet · elle nous appelle")) + (p.lan_ip ? " · " + T("locale %s", p.lan_ip) : "");
const wanDirLabel = (p, name) => p.wan_dir === "out" ? T("Cette PS5 appelle %s", name) : p.wan_dir === "in" ? T("%s appelle cette PS5", name) : T("Automatique");
// Qui ouvre la session avec une PS5 à distance (comme la télé) : au choix, changeable ; l'autre PS5 reçoit le choix inversé.
function wanSession(p) {
  const name = p.name || p.host, cur = p.wan_dir || "auto";
  const box = document.createElement("div");
  box.className = "modal";
  const opt = (v, label) => `<label class="radio"><input type="radio" name="wan-dir" value="${v}" ${cur === v ? "checked" : ""}><span class="grow">${esc(label)}</span></label>`;
  box.innerHTML = `<div class="dialog"><h2>${esc(T("Qui ouvre la session avec %s ?", name))}</h2>
    <p class="hint">${esc(T("Celle qui est derrière un pare-feu, en LTE ou sur Starlink devrait appeler l'autre : son trafic sort, rien à ouvrir chez elle. Celle qu'on appelle doit être joignable (redirection du port). Le choix est aussi fait sur l'autre PS5 ; changez-le si vous vous déplacez."))}</p>
    ${opt("auto", T("Automatique"))}${opt("out", T("Cette PS5 appelle %s", name))}${opt("in", T("%s appelle cette PS5", name))}
    <label class="field-row">${esc(T("Adresse publique de %s (ex. maison.exemple.net)", name))}<input class="field" id="wan-peer-host" value="${esc(p.wan_host || "")}"></label>
    <div class="actions"><button class="secondary" id="wan-s-cancel">${T("Annuler")}</button><button class="primary" id="wan-s-ok">${T("Enregistrer")}</button></div></div>`;
  document.body.appendChild(box);
  const closeBox = () => box.parentNode && box.parentNode.removeChild(box);
  box.querySelector("#wan-s-cancel").onclick = closeBox;
  box.querySelector("#wan-s-ok").onclick = async () => {
    const dir = box.querySelector("input[name=wan-dir]:checked").value, host = box.querySelector("#wan-peer-host").value.replace(/\s/g, "");
    try { await post("/wan/peer", { instance: p.instance, dir, host: host !== (p.wan_host || "") ? host : "" }); toast(T("Choix enregistré, aussi sur %s", name)); closeBox(); state.devicesAt = 0; render(); }
    catch (e) { toast(e.message); }
  };
}
// Comme la télé : sa version de Backup Center et son temps de réponse (« · v2.89.0 · 12 ms »).
const peerVerMs = (p) => (p.version ? " · v" + esc(p.version) : "") + (typeof p.rtt_ms === "number" ? " · " + p.rtt_ms + " ms" : "");
const ADD_DEVICES = [["ps5", "PlayStation 5", T("une autre PS5")], ["ps5", T("PS5 à distance"), T("par Internet")], ["usb", T("Disque USB"), T("disque externe")],
  ["switch", "Switch", T("réseau ou USB")], ["synology", "Synology", "NAS"], ["qnap", "QNAP", "NAS"], ["network", "FTP / SMB", T("PC, serveur")], ["cloud", T("Cloud"), T("Drive, OneDrive, S3…")]];
// Appareils (ex-Destinations) : cartes avec images et barres d'espace, raccourcis directs (Ses parties, Ses backups,
// Synchroniser) ; les réglages de connexion vivent ici seulement (disque par défaut, téléphones et ordinateurs associés).
function devicesView() {
  if (!state.devicesAt || Date.now() - state.devicesAt > 30000) {
    state.devicesAt = Date.now();
    api("/devices").then((d) => { state.devices = d; if (state.tab === "devices") render(); }).catch(() => {});
    api("/link/peers").then((d) => {
      state.linkPeers = d; if (state.tab === "devices") render();
      for (const p of (d || []).filter((x) => x.paired && x.online))  // leur état (version installée) pour la bonne action
        api(`/link/peer?instance=${encodeURIComponent(p.instance)}&what=status`).then((st) => { state.peerStatus = state.peerStatus || {}; state.peerStatus[p.instance] = st; if (state.tab === "devices") render(); }).catch(() => {});
    }).catch(() => {});
  }
  const places = state.places.filter((p) => p.type === "usb" || p.type === "internal");
  $("#view").innerHTML = `<h1>${T("Appareils")}</h1><p class="sub">${T("Chaque appareil dit à quoi il sert, avec des raccourcis directs. Les disques sont reconnus par leur témoin, même s'ils changent de port USB.")}</p>
    <div class="devs">${places.map(devCardHtml).join("")}${state.remotes.map(remoteCardHtml).join("")}${peerCardsHtml()}</div>
    <h2 class="eyebrow-h">${T("Ajouter un appareil")}</h2>
    <div class="adds">${ADD_DEVICES.map(([ico, name, sub], i) => { const brand = ico === "synology" || ico === "qnap";  // le logo porte déjà le nom
      return `<button class="add${brand ? " add-brand" : ""}${ico === "cloud" || ico === "network" ? " add-wide" : ""}" data-add="${i}"><span class="dev-ico">${ICONS[ico]}</span><b>${esc(name)}</b><span class="hint">${esc(sub)}</span></button>`; }).join("")}</div>
    <h2 class="eyebrow-h">${T("Disque par défaut")}</h2><p class="sub">${T("Les nouvelles sauvegardes vont sur ce disque, dans le dossier « PBC Backup ».")}</p>
    <div class="two-col">
      <label class="radio"><input type="radio" name="dp" value="auto" ${state.settings.default_place === "auto" ? "checked" : ""}><span class="grow"><b>${T("Choisir automatiquement")}</b><br><span class="hint">${T("Le premier disque USB, sinon le stockage interne")}</span></span></label>
      ${state.places.filter((p) => p.type !== "switch").map((p) => `<label class="radio"><input type="radio" name="dp" value="${esc(p.id)}" ${state.settings.default_place === p.id ? "checked" : ""}><span class="grow"><b>${esc(p.label)}</b><br><span class="hint">${esc(p.mount)}</span></span><span class="hint">${p.remote || p.type === "ftp" ? T("réseau") : T("%s libres sur %s", size(p.free), size(p.total))}</span></label>`).join("")}
    </div>
    <h2 class="eyebrow-h">${T("Téléphones et ordinateurs associés")}</h2><p class="sub">${T("Ouvrez cette adresse sur un appareil du même réseau, puis saisissez le code affiché sur la télé.")}</p>
    <div class="card"><div class="kv">${T("Adresse")}<b>${esc(location.origin)}</b></div></div>
    ${state.devices.length ? `<div class="list">${state.devices.map((d) => `<div class="row"><div><div class="t">${esc(d.name)}</div><div class="d">${esc(T("Depuis le %s", new Date(d.created).toLocaleDateString(I18N.code)))}</div></div><div class="btns"><button data-forget="${esc(d.id)}">${T("Oublier")}</button></div></div>`).join("")}</div>` : `<p class="sub">${T("Aucun.")}</p>`}`;
  $$("[data-default]").forEach((b) => b.onclick = async () => {
    try { state.settings = await post("/settings", { default_place: b.dataset.default }); toast(T("Destination par défaut enregistrée")); devicesView(); } catch (e) { toast(e.message); }
  });
  $$("input[name=dp]").forEach((r) => r.onchange = async () => { try { state.settings = await post("/settings", { ...state.settings, default_place: r.value }); toast(T("Disque par défaut enregistré")); devicesView(); } catch (e) { toast(e.message); } });
  $$("[data-forget]").forEach((b) => b.onclick = async () => { try { await post(`/devices/${b.dataset.forget}/forget`); state.devicesAt = 0; } catch (e) { toast(e.message); } devicesView(); });
  $$("[data-go-party]").forEach((b) => b.onclick = () => { const v = b.dataset.goParty; state.party.player = v === "ps5" ? "" : v; state.party.sel.clear(); setTab("saves"); });
  $$("[data-go-backups]").forEach((b) => b.onclick = () => { mg.place = b.dataset.goBackups; mg.view = 0; mg.game = ""; mg.back = ""; mg.gsel.clear(); setTab("backups"); });
  $$("[data-go-sync]").forEach((b) => b.onclick = () => openSync(b.dataset.goSync));
  $$("[data-browse-place]").forEach((b) => b.onclick = () => { const p = state.places.find((x) => x.id === b.dataset.browsePlace); openBrowser({ title: p.label, start: p.type === "internal" ? "/data" : p.mount }); });
  $$("[data-browse-remote]").forEach((b) => b.onclick = () => { const r = state.remotes.find((x) => x.id === b.dataset.browseRemote); openBrowser({ title: r.name, start: (r.config && r.config.root) || "/", remote: r.id }); });
  $$("[data-edit-remote]").forEach((b) => b.onclick = () => { const r = state.remotes.find((x) => x.id === b.dataset.editRemote); openEditor(r.id, r.type); });
  $$("[data-peer-disks]").forEach((b) => b.onclick = async () => {
    try {
      const pl = await api(`/link/peer?instance=${encodeURIComponent(b.dataset.peerDisks)}&what=places`);
      toast(pl.filter((p) => p.type === "usb" || p.type === "internal").map((p) => `${p.label} : ${T("%s libres", size(p.free))}`).join(" · "));
    } catch (e) { toast(e.message); }
  });
  // Ses parties (lecture), et raccourcis pour envoyer un jeu ou une sauvegarde à cette PS5.
  $$("[data-peer-parties]").forEach((b) => b.onclick = async () => {
    let j = null;
    try { j = await api(`/link/peer?instance=${encodeURIComponent(b.dataset.peerParties)}&what=overview`); } catch (e) { toast(e.message); return; }
    const rows = (j.entries || []).filter((x) => x.console).map((x) => `<li>${esc(x.title)} · ${x.account && x.account.startsWith("ps4:") ? "PS4" : "PS5"}${x.console.progress && x.console.progress.percent != null ? " · " + x.console.progress.percent + " %" : ""}</li>`);
    if (!rows.length) { toast(T("Aucune partie sur %s", b.dataset.name)); return; }
    ask(T("Parties sur %s (%d)", b.dataset.name, rows.length), `<ul class="hint">${rows.join("")}</ul>`, T("Fermer"), "");
  });
  $$("[data-peer-browse]").forEach((b) => b.onclick = () => openBrowser({ title: b.dataset.title, start: b.dataset.start, remote: "peer:" + b.dataset.peerBrowse }));
  $$("[data-peer-sendgame]").forEach((b) => b.onclick = () => { setTab("library"); toast(T("Choisissez un jeu, puis « Envoyer à une PS5… » sur sa fiche (vers %s).", b.dataset.name)); });
  $$("[data-peer-sendsave]").forEach((b) => b.onclick = () => { setTab("saves"); toast(T("Choisissez une partie, puis Options › « Envoyer la sauvegarde à une PS5… » (vers %s).", b.dataset.name)); });
  // Comme la télé : la carte reste simple, ses actions s'ouvrent en menu (les boutons cachés gardent leur action).
  $$("[data-peer-more]").forEach((b) => b.onclick = (e) => {
    e.stopPropagation();
    const btns = $$(".peer-more button", b.closest(".dev-body"));
    openMenu(b, btns.map((x) => ({ label: x.textContent, run: () => x.click() })));
  });
  $$("[data-peer-uninstall]").forEach((b) => b.onclick = () => peerUninstallList(b.dataset.peerUninstall, b.dataset.name));
  $$("[data-peer-restart]").forEach((b) => b.onclick = async () => {
    const v = b.dataset.v;
    try { await post("/link/peer-restart-engine", { instance: b.dataset.peerRestart }); toast(v ? T("%s : relance du moteur demandée (%s)", b.dataset.name, v) : T("%s : relance du moteur demandée", b.dataset.name)); state.devicesAt = 0; }
    catch (e) { toast(v ? T("%s refuse la relance : %s — %s sera active à son prochain démarrage", b.dataset.name, e.message, v) : T("%s refuse la relance : %s", b.dataset.name, e.message)); }
  });
  $$("[data-peer-pull]").forEach((b) => b.onclick = async () => {
    const note = T("Cette PS5 est en %s. Elle télécharge la version de %s, vérifie chaque fichier et garde l'ancienne si quelque chose échoue. Son moteur est ensuite relancé ; s'il refuse, la nouvelle version sera active au prochain démarrage. Le jeu en cours n'est jamais touché.", (state.status || {}).version, b.dataset.name);
    if (!(await ask(T("Mettre cette PS5 en %s ?", b.dataset.theirs), `<p class="hint">${esc(note)}</p>`, T("Récupérer la %s", b.dataset.theirs)))) return;
    try { await post("/link/peer-update-me", { instance: b.dataset.peerPull }); toast(T("Mise à jour demandée à %s : l'avancement s'affiche sur sa carte et à l'Accueil", b.dataset.name)); } catch (e) { toast(e.message); }
  });
  $$("[data-peer-closeapp]").forEach((b) => b.onclick = async () => {
    if (!(await ask(T("Fermer Backup Center sur %s ?", b.dataset.name), `<p class="hint">${esc(T("Seulement si Backup Center est l'application ouverte là-bas : un jeu n'est jamais fermé. Son moteur continue (sauvegardes, synchro)."))}</p>`, T("Fermer sur %s", b.dataset.name)))) return;
    try { await post("/link/close-app", { instance: b.dataset.peerCloseapp, title_id: "PPSA99950" }); toast(T("Backup Center se ferme sur %s", b.dataset.name)); } catch (e) { toast(T("%s : %s", b.dataset.name, e.message)); }
  });
  $$("[data-peer-open]").forEach((b) => b.onclick = async () => {
    if (!(await ask(T("Ouvrir Backup Center sur %s ?", b.dataset.name), `<p class="hint">${esc(T("S'il y a un jeu en cours là-bas, il passe en arrière-plan (sa partie n'est pas fermée). À faire seulement si personne n'y joue."))}</p>`, T("Ouvrir sur %s", b.dataset.name)))) return;
    try { await post("/link/peer-open-app", { instance: b.dataset.peerOpen }); toast(T("Backup Center s'ouvre sur %s", b.dataset.name)); } catch (e) { toast(e.message); }
  });
  $$("[data-peer-update]").forEach((b) => b.onclick = async () => {
    const mine = (state.status || {}).version;
    const note = T("Elle est en %s. Elle télécharge la version de cette PS5, vérifie chaque fichier et garde l'ancienne si quelque chose échoue. Son moteur est ensuite relancé ; s'il refuse, la nouvelle version sera active au prochain démarrage de la console. Le jeu en cours n'est jamais touché.", b.dataset.theirs);
    if (!(await ask(T("Mettre %s en %s ?", b.dataset.name, mine), `<p class="hint">${esc(note)}</p>`, T("Mettre à jour %s", b.dataset.name)))) return;
    try { await post("/update/push", { peer: b.dataset.peerUpdate }); toast(T("Mise à jour de %s lancée : l'avancement s'affiche sur sa carte", b.dataset.name)); pollUpdPush(); } catch (e) { toast(e.message); }
  });
  $$("[data-peer-close]").forEach((b) => b.onclick = async () => {
    let fg = null;
    try { fg = await api(`/link/peer?instance=${encodeURIComponent(b.dataset.peerClose)}&what=foreground`); } catch (e) { toast(e.message); return; }
    confirmCloseRemote(b.dataset.peerClose, b.dataset.name, fg);
  });
  $$("[data-peer-forget]").forEach((b) => b.onclick = async () => {
    if (!confirm(T("Défaire le lien avec %s ?", b.dataset.name))) return;
    try { await post("/link/forget", { instance: b.dataset.peerForget }); state.devicesAt = 0; render(); } catch (e) { toast(e.message); }
  });
  $$("[data-peer-wantest]").forEach((b) => b.onclick = () => wanTest(b.dataset.peerWantest, b.dataset.name));
  $$("[data-peer-session]").forEach((b) => b.onclick = () => { const p = (state.linkPeers || []).find((x) => x.instance === b.dataset.peerSession); if (p) wanSession(p); });
  $$("[data-add]").forEach((b) => b.onclick = () => {
    const i = +b.dataset.add;  // 0 PS5, 1 PS5 à distance, 2 USB, 3 Switch, 4 Synology, 5 QNAP, 6 FTP / SMB, 7 Cloud
    if (i === 0) otherPs5();
    else if (i === 1) remotePs5();
    else if (i === 2) toast(T("Branchez le disque : il apparaît tout seul, et reste reconnu même sur un autre port."));
    else if (i === 3) openEditor("", "switch");
    else if (i >= 4 && i <= 6) openEditor("", "ftp");
    else cloudPick();
  });
}
// Nuage : Amazon S3, Backblaze B2, autre service S3 ; Google Drive et OneDrive viendront ensuite.
const S3_SERVICES = () => [["aws", "Amazon S3", T("clés S3")], ["b2", "Backblaze B2", T("clés S3")], ["wasabi", "Wasabi", T("clés S3")], ["r2", "Cloudflare R2", T("clés S3")],
  ["scaleway", "Scaleway", T("clés S3")], ["ovh", "OVHcloud", T("clés S3")], ["c2", "Synology C2", T("clés S3")], ["other", T("Autre S3"), T("MinIO, iDrive e2…")]];
// WebDAV : le réglage du NAS, adresse et dossier déjà remplis.
const DAV_SERVICES = () => [["pCloud", "webdav.pcloud.com", "/", T("pCloud : e-mail et mot de passe du compte. Compte européen : ewebdav.pcloud.com.")],
  ["Koofr", "app.koofr.net", "/dav/Koofr", T("Koofr : e-mail et un mot de passe d'appli (Koofr › Réglages › Mots de passe).")],
  ["Nextcloud", "", "/remote.php/dav/files/", T("Nextcloud : adresse de votre serveur ; ajoutez votre identifiant à la fin du dossier.")]];
const S3_HOST_TYPED = (k) => k === "other" || k === "r2";
function cloudPick() {
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog"><h2>${T("Ajouter un stockage dans le nuage")}</h2>
    <div class="adds">${[["gdrive", "Google Drive", T("connexion par code")], ["onedrive", "OneDrive", T("connexion par code")], ["dropbox", "Dropbox", T("depuis le téléphone")]].map(([k, n, sub]) => `<button class="add" data-drive="${k}"><span class="dev-ico">${ICONS.cloud}</span><b>${n}</b><span class="hint">${esc(sub)}</span></button>`).join("")}${S3_SERVICES().filter(([k]) => k !== "other").map(([k, n, sub]) => `<button class="add" data-s3="${k}"><span class="dev-ico">${ICONS.cloud}</span><b>${esc(n)}</b><span class="hint">${esc(sub)}</span></button>`).join("")}${DAV_SERVICES().map(([n], i) => `<button class="add" data-dav="${i}"><span class="dev-ico">${ICONS.cloud}</span><b>${esc(n)}</b><span class="hint">${T("compte WebDAV")}</span></button>`).join("")}<button class="add" data-s3="other"><span class="dev-ico">${ICONS.cloud}</span><b>${T("Autre S3")}</b><span class="hint">${T("MinIO, iDrive e2…")}</span></button></div>
    <div class="actions"><button class="secondary" id="cp-close">${T("Fermer")}</button></div></div>`;
  document.body.appendChild(box);
  const close = () => box.remove();
  $("#cp-close", box).onclick = close;
  $$("[data-s3]", box).forEach((b) => b.onclick = () => { close(); openS3Editor("", b.dataset.s3); });
  $$("[data-drive]", box).forEach((b) => b.onclick = () => { close(); driveLink(b.dataset.drive); });
  $$("[data-dav]", box).forEach((b) => b.onclick = () => { close(); const [name, host, root, hint] = DAV_SERVICES()[+b.dataset.dav]; openEditor("", "webdav", { name, host, port: "443", root, hint }); });
}
// Google Drive, OneDrive : connexion par code. Sur un téléphone, le bouton ouvre directement la page du service.
const DRIVE_NAME = { gdrive: "Google Drive", onedrive: "OneDrive", dropbox: "Dropbox" };
// Dropbox : ouvrir la page d'autorisation, puis coller le code affiché par Dropbox (ici, ou sur le téléphone).
function dropboxSteps(id, authorize, done) {
  return `<p>${T("1. Ouvrez Dropbox, connectez-vous et appuyez sur « Autoriser » :")}</p>
    <p><a class="primary button-link" href="${esc(authorize)}" target="_blank" rel="noopener">${T("Se connecter à Dropbox")}</a></p>
    <p>${T("2. Dropbox affiche un code : copiez-le et collez-le ici.")}</p>
    <p class="row-inline"><input class="field" id="dbx-code" autocomplete="off" placeholder="${esc(T("Code de Dropbox"))}"><button class="primary" id="dbx-ok">${T("Valider")}</button></p>
    <p class="hint" id="dbx-state"></p>`;
}
async function dropboxSend(id, root, after) {
  const code = $("#dbx-code", root).value.trim();
  if (!code) return;
  $("#dbx-state", root).innerHTML = `${SPIN}${T("Vérification…")}`;
  try {
    const r = await fetch("/api/v2/cloud/link/code", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ id, code }) });
    if (!r.ok) { const j = await r.json().catch(() => ({})); throw new Error(j.error || r.statusText); }
    after();
  } catch (e) { $("#dbx-state", root).innerHTML = `<span class="bad">${esc(e.message)}</span>`; }
}
// Page ouverte sur le téléphone par le code QR de la télé (« …/#link=<id> ») : pas besoin d'associer le téléphone.
function linkPage() {
  if (!location.hash.startsWith("#link=")) return false;
  const id = decodeURIComponent(location.hash.slice(6));
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog"><div class="ed-head"><span class="dev-ico">${ICONS.cloud}</span><div><h2>${esc(T("Connecter %s", "Dropbox"))}</h2>
    <p class="hint">${T("L'appli n'aura accès qu'à son propre dossier, jamais à vos autres fichiers.")}</p></div></div><div id="lp-body">${SPIN}</div></div>`;
  document.body.appendChild(box);
  fetch(`/api/v2/cloud/link/info?id=${encodeURIComponent(id)}`).then((r) => r.json()).then((info) => {
    if (info.state !== "waiting_code" || !info.authorize) { $("#lp-body", box).innerHTML = `<p class="bad">${T("Lien expiré : recommencez depuis la télé.")}</p>`; return; }
    $("#lp-body", box).innerHTML = dropboxSteps(id, info.authorize);
    $("#dbx-ok", box).onclick = () => dropboxSend(id, box, () => { $("#lp-body", box).innerHTML = `<p class="ok">${T("C'est fait : la suite se passe sur la télé.")}</p>`; });
  }).catch(() => { $("#lp-body", box).innerHTML = `<p class="bad">${T("Console injoignable.")}</p>`; });
  return true;
}
async function driveLink(provider) {
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog"><div class="ed-head"><span class="dev-ico">${ICONS.cloud}</span><div><h2>${esc(T("Connecter %s", DRIVE_NAME[provider]))}</h2>
    <p class="hint">${T("Rien à taper ici : la connexion se fait sur votre téléphone, chez le service.")}</p></div></div>
    <div id="dl-body">${SPIN}</div>
    <p class="hint">${T("L'appli n'aura accès qu'à son propre dossier, jamais à vos autres fichiers.")}</p>
    <div class="actions"><button class="secondary" id="dl-new">${T("Nouveau code")}</button><button class="secondary" id="dl-close">${T("Annuler")}</button></div></div>`;
  document.body.appendChild(box);
  let timer = 0, alive = true;
  const close = () => { alive = false; clearTimeout(timer); box.remove(); };
  $("#dl-close", box).onclick = close;
  const show = (html) => { $("#dl-body", box).innerHTML = html; };
  const start = async () => {
    clearTimeout(timer);
    show(SPIN);
    let l;
    try { l = await post("/cloud/link", { provider }); } catch (e) { show(`<p class="bad">${esc(T("Connexion impossible : %s", e.message))}</p>`); return; }
    if (provider === "dropbox") {  // sur cette page : le lien d'autorisation et le code à coller, sans code QR
      const info = await (await fetch(`/api/v2/cloud/link/info?id=${encodeURIComponent(l.id)}`)).json();
      show(dropboxSteps(l.id, info.authorize));
      $("#dbx-ok", box).onclick = () => dropboxSend(l.id, box, async () => {
        const s = await api(`/cloud/link?id=${encodeURIComponent(l.id)}`);
        if (s.state === "ok") { close(); openDriveEditor("", provider, l.id, s); }
      });
      return;
    }
    show(`<div class="row-inline" style="gap:20px;align-items:center;flex-wrap:wrap"><div class="qr">${qrSvg(l.qr)}</div><div>
      <p>${T("1. Scannez le code avec votre téléphone, ou ouvrez :")} <a href="${esc(l.url)}" target="_blank" rel="noopener">${esc(l.url)}</a></p>
      <p>${T("2. Saisissez ce code :")} <b style="font-size:1.6em;letter-spacing:.1em">${esc(l.user_code)}</b></p>
      <p>${T("3. Connectez-vous (e-mail, mot de passe, code de validation…) et acceptez.")}</p>
      <p class="hint" id="dl-state">${SPIN}${T("En attente de votre accord…")}</p></div></div>`);
    const poll = async () => {
      if (!alive) return;
      let s = {};
      try { s = await api(`/cloud/link?id=${encodeURIComponent(l.id)}`); } catch (e) {}
      if (!alive) return;
      if (s.state === "ok") { close(); openDriveEditor("", provider, l.id, s); return; }
      if (s.state === "denied") { $("#dl-state", box).innerHTML = `<span class="bad">${T("Refusé sur le téléphone : demandez un nouveau code.")}</span>`; return; }
      if (s.state === "expired") { $("#dl-state", box).innerHTML = `<span class="bad">${T("Code expiré : demandez un nouveau code.")}</span>`; return; }
      if (s.state === "error") { $("#dl-state", box).innerHTML = `<span class="bad">${esc(T("Connexion impossible : %s", s.error || ""))}</span>`; return; }
      timer = setTimeout(poll, 2000);
    };
    timer = setTimeout(poll, 2000);
  };
  $("#dl-new", box).onclick = start;
  start();
}
// Réglage d'un compte Drive : nom, dossier, usage ; le compte vient de la connexion par code.
function openDriveEditor(id, provider, link, linked) {
  const r = state.remotes.find((x) => x.id === id), c = (r && r.config) || {};
  provider = (r && r.type) || provider;
  const account = (linked && linked.account) || c.bucket || "";
  const box = document.createElement("div");
  box.className = "modal";
  const place = linked && linked.total ? T("%s libres sur %s", size(linked.free), size(linked.total)) : "";
  box.innerHTML = `<div class="dialog"><div class="ed-head"><span class="dev-ico">${ICONS.cloud}</span><div><h2>${esc(DRIVE_NAME[provider])}</h2>
      <p class="hint">${T("Par Internet : plus lent qu'un NAS. Le service peut facturer le stockage et les téléchargements.")}</p></div></div>
    <label class="field-row">${T("Compte")}<input class="field" value="${esc(account)}" disabled></label>
    ${place ? `<label class="field-row">${T("Place")}<input class="field" value="${esc(place)}" disabled></label>` : ""}
    <label class="field-row">${T("Nom")}<input class="field" id="dr-name" value="${esc(r ? r.name : account)}"></label>
    <label class="field-row">${T("Dossier")}<input class="field" id="dr-root" value="${esc(c.root || "/")}"></label>
    <label class="field-row">${T("Utilisé pour")}<select id="dr-use"><option value="backups">${T("Sauvegardes seulement")}</option><option value="both" ${r && !r.use ? "selected" : ""}>${T("Sauvegardes et Synchro")}</option><option value="sync" ${r && r.use === "sync" ? "selected" : ""}>${T("Synchro seulement")}</option></select></label>
    <div class="actions"><button class="primary" id="dr-save">${T("Enregistrer")}</button><button class="secondary" id="dr-test">${T("Tester la connexion")}</button>${id ? `<button class="danger" id="dr-forget">${T("Oublier")}</button>` : ""}<button class="secondary" id="dr-close">${T("Fermer")}</button></div>
    <p class="hint" id="dr-status"></p></div>`;
  document.body.appendChild(box);
  const close = () => box.remove();
  const status = (html) => { $("#dr-status", box).innerHTML = html; };
  const busy = (on) => { $$("button", box).forEach((b) => { b.disabled = on && b.id !== "dr-close"; }); if (on) status(`${SPIN}${T("Connexion au service…")}`); };
  const body = () => ({ type: provider, id, link, name: $("#dr-name", box).value.trim(), root: $("#dr-root", box).value.trim() || "/", use: $("#dr-use", box).value });
  $("#dr-close", box).onclick = close;
  $("#dr-test", box).onclick = async () => {
    busy(true);
    try { await post("/remotes/test-config", body()); busy(false); status(`<span class="ok">${T("Connexion réussie : vous pouvez enregistrer")}</span>`); }
    catch (e) { busy(false); status(`<span class="bad">${esc(e.message)}</span>`); }
  };
  $("#dr-save", box).onclick = async () => {
    busy(true);
    try {
      await post(id ? `/remotes/${encodeURIComponent(id)}/update` : "/remotes", body());
      toast(T("Appareil enregistré : connexion vérifiée")); close(); reloadRemotes();
    } catch (e) {
      busy(false);
      const taken = e.message.startsWith("sync_taken:") ? e.message.slice(11) : "";
      status(`<span class="bad">${esc(taken ? T("Ce dossier sert déjà à la synchro de « %s ». Choisissez un autre dossier, ou « Sauvegardes seulement ».", taken) : T("Connexion impossible : %s", e.message))}</span>`);
    }
  };
  let armed = false;
  if (id) $("#dr-forget", box).onclick = async () => {
    if (!armed) { armed = true; $("#dr-forget", box).textContent = T("Confirmer : oublier"); status(T("Appuyez encore pour oublier cet appareil (rien n'est effacé dessus).")); return; }
    try { await post(`/remotes/${encodeURIComponent(id)}/forget`); toast(T("Appareil oublié")); close(); reloadRemotes(); } catch (e) { status(`<span class="bad">${esc(e.message)}</span>`); }
  };
}
const S3_REGION = { aws: "eu-west-3", b2: "us-west-004", wasabi: "eu-central-1", r2: "auto", scaleway: "fr-par", ovh: "gra", c2: "eu-002", other: "" };
function openS3Editor(id, service) {
  const r = state.remotes.find((x) => x.id === id), c = (r && r.config) || {};
  service = c.service || service || "aws";
  const box = document.createElement("div");
  box.className = "modal";
  const field = (k, label, val, extra = "") => `<label class="field-row">${label}<input class="field" id="s3-${k}" value="${esc(val || "")}" autocomplete="off" ${extra}></label>`;
  const svcName = (k) => (S3_SERVICES().find((x) => x[0] === k) || [k, k])[1];
  box.innerHTML = `<div class="dialog"><div class="ed-head"><span class="dev-ico">${ICONS.cloud}</span><div>
      <h2 id="s3-title">${id ? T("Régler l'appareil") : esc(T("Ajouter un stockage %s", svcName(service)))}</h2>
      <p class="hint">${T("Les clés se créent sur le site du service (Amazon : IAM › Clés d'accès ; Backblaze : Application Keys).")}</p></div></div>
    ${field("name", T("Nom"), r ? r.name : "", `placeholder="${esc(T("Mes sauvegardes"))}"`)}
    <label class="field-row">${T("Service")}<select id="s3-service">${S3_SERVICES().map(([k, n]) => `<option value="${k}" ${service === k ? "selected" : ""}>${esc(n)}</option>`).join("")}</select></label>
    ${field("host", service === "r2" ? T("N° de compte") : T("Adresse du service"), S3_HOST_TYPED(c.service) ? c.host : "", 'placeholder="https://s3.example.com"')}
    ${field("region", T("Région"), c.region || S3_REGION[service])}
    ${field("bucket", T("Compartiment"), c.bucket)}
    ${field("user", T("Clé d'accès"), c.user && c.user !== "anonymous" ? c.user : "")}
    <label class="field-row">${T("Clé secrète")}<input class="field" id="s3-password" type="password" autocomplete="new-password" placeholder="${esc(id ? T("(inchangé)") : "")}"></label>
    ${field("root", T("Dossier"), c.root || "/")}
    <label class="field-row">${T("Utilisé pour")}<select id="s3-use"><option value="backups">${T("Sauvegardes seulement")}</option><option value="both" ${r && !r.use ? "selected" : ""}>${T("Sauvegardes et Synchro")}</option><option value="sync" ${r && r.use === "sync" ? "selected" : ""}>${T("Synchro seulement")}</option></select></label>
    <p class="hint">${T("Par Internet : plus lent qu'un NAS. Le service peut facturer le stockage et les téléchargements.")}</p>
    <div class="actions"><button class="primary" id="s3-save">${T("Enregistrer")}</button><button class="secondary" id="s3-test">${T("Tester la connexion")}</button>${id ? `<button class="danger" id="s3-forget">${T("Oublier")}</button>` : ""}<button class="secondary" id="s3-close">${T("Fermer")}</button></div>
    <p class="hint" id="s3-status"></p></div>`;
  document.body.appendChild(box);
  const close = () => box.remove();
  const status = (html) => { $("#s3-status", box).innerHTML = html; };
  const busy = (on) => { $$("button", box).forEach((b) => { b.disabled = on && b.id !== "s3-close"; }); if (on) status(`${SPIN}${T("Connexion au service…")}`); };
  const showHost = () => {
    const l = $("#s3-host", box).closest("label");
    l.style.display = S3_HOST_TYPED(service) ? "" : "none";
    l.firstChild.textContent = service === "r2" ? T("N° de compte") : T("Adresse du service");
    $("#s3-host", box).placeholder = service === "r2" ? "0123abcd…" : "https://s3.example.com";
  };
  showHost();
  $("#s3-service", box).onchange = () => {
    const before = service;
    service = $("#s3-service", box).value;
    if ($("#s3-region", box).value === S3_REGION[before]) $("#s3-region", box).value = S3_REGION[service];
    if (!id) $("#s3-title", box).textContent = T("Ajouter un stockage %s", svcName(service));
    showHost();
  };
  $("#s3-close", box).onclick = close;
  $("#s3-name", box).focus();
  const body = () => ({ type: "s3", id, service, name: $("#s3-name", box).value.trim(), host: S3_HOST_TYPED(service) ? $("#s3-host", box).value.trim() : "",
    region: $("#s3-region", box).value.trim(), bucket: $("#s3-bucket", box).value.trim(), user: $("#s3-user", box).value.trim(), password: $("#s3-password", box).value.trim(),
    root: $("#s3-root", box).value.trim() || "/", use: $("#s3-use", box).value });
  $("#s3-test", box).onclick = async () => {
    busy(true);
    try { await post("/remotes/test-config", body()); busy(false); status(`<span class="ok">${T("Connexion réussie : vous pouvez enregistrer")}</span>`); }
    catch (e) { busy(false); status(`<span class="bad">${esc(e.message)}</span>`); }
  };
  $("#s3-save", box).onclick = async () => {
    busy(true);
    try {
      await post(id ? `/remotes/${encodeURIComponent(id)}/update` : "/remotes", body());
      toast(T("Appareil enregistré : connexion vérifiée")); close(); reloadRemotes();
    } catch (e) {
      busy(false);
      const taken = e.message.startsWith("sync_taken:") ? e.message.slice(11) : "";
      status(`<span class="bad">${esc(taken ? T("Ce dossier sert déjà à la synchro de « %s ». Choisissez un autre dossier, ou « Sauvegardes seulement ».", taken) : T("Connexion impossible : %s", e.message))}</span>`);
    }
  };
  let armed = false;
  if (id) $("#s3-forget", box).onclick = async () => {
    if (!armed) { armed = true; $("#s3-forget", box).textContent = T("Confirmer : oublier"); status(T("Appuyez encore pour oublier cet appareil (rien n'est effacé dessus).")); return; }
    try { await post(`/remotes/${encodeURIComponent(id)}/forget`); toast(T("Appareil oublié")); close(); reloadRemotes(); } catch (e) { status(`<span class="bad">${esc(e.message)}</span>`); }
  };
}
// Une autre PS5 (liaison entre consoles, docs/PROTOCOLE-link.md) : même menu que la télé.
async function otherPs5() {
  let peers = [];
  try { peers = await api("/link/peers"); } catch (e) { toast(e.message); return; }
  const rows = peers.filter((p) => p.paired || p.online).map((p) => `<div class="row"><div class="grow"><div class="t">${esc(p.name || p.host)}</div>
      <div class="d">${esc(p.host)} · ${esc(p.app)} ${esc(p.version)} · ${p.paired ? (p.online ? T("jumelée ✓, en ligne") : T("jumelée ✓, hors ligne")) : T("vue sur le réseau")}</div></div>
      <span class="vbtns">${p.paired ? `<button class="secondary small" data-lk-disks="${esc(p.instance)}">${T("Ses disques")}</button><button class="secondary small" data-lk-forget="${esc(p.instance)}" data-name="${esc(p.name || p.host)}">${T("Oublier cette console…")}</button>`
        : `<button class="small" data-lk-pair="${esc(p.host)}">${T("Jumeler…")}</button>`}</span></div>`).join("");
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog wide"><h2>${T("Une autre PS5")}</h2>
    <ol class="hint lk-steps"><li>${T("1. Installez Backup Center sur les deux PS5, sur le même réseau.")}</li><li>${T("2. Sur l'une, choisissez l'autre PS5 dans cette liste.")}</li>
      <li>${T("3. Tapez le code affiché sur la télé de l'autre PS5. C'est tout : le lien marche dans les deux sens.")}</li><li>${T("Ensuite : Sauvegardes › un jeu › Options › « Envoyer à … ».")}</li></ol>
    <p class="hint">${T("Le code de cette console s'affiche sur sa télé : Appareils › PlayStation 5. Sur l'autre PS5, saisissez-le pour jumeler.")}</p>
    <div class="list">${rows || `<p class="hint">${T("Aucune autre PS5 vue sur le réseau pour l'instant.")}</p>`}</div>
    <h4>${T("Par adresse IP")}</h4>
    <div class="lk-row"><input id="lk-ip" placeholder="192.168.1.20"><input id="lk-code" placeholder="${esc(T("Code à 6 chiffres"))}" maxlength="7"><button class="primary" id="lk-go">${T("Jumeler")}</button></div>
    <div class="actions"><button class="secondary" id="lk-close">${T("Fermer")}</button></div></div>`;
  document.body.appendChild(box);
  const close = () => box.remove();
  box.querySelector("#lk-close").onclick = close;
  const pair = async (host, code) => {
    code = String(code || "").replace(/\D/g, "");
    if (!host || code.length !== 6) { toast(T("Le code a 6 chiffres")); return; }
    try { await post("/link/pair", { host, port: 52370, code }); toast(T("%s est jumelée", host)); close(); otherPs5(); } catch (e) { toast(e.message); }
  };
  box.querySelector("#lk-go").onclick = () => pair(box.querySelector("#lk-ip").value.trim(), box.querySelector("#lk-code").value);
  box.querySelectorAll("[data-lk-pair]").forEach((b) => b.onclick = () => { box.querySelector("#lk-ip").value = b.dataset.lkPair; box.querySelector("#lk-code").focus(); });
  box.querySelectorAll("[data-lk-forget]").forEach((b) => b.onclick = async () => { if (!confirm(T("Défaire le lien avec %s ?", b.dataset.name))) return; try { await post("/link/forget", { instance: b.dataset.lkForget }); close(); otherPs5(); } catch (e) { toast(e.message); } });
  box.querySelectorAll("[data-lk-disks]").forEach((b) => b.onclick = async () => {
    try {
      const pl = await api(`/link/peer?instance=${encodeURIComponent(b.dataset.lkDisks)}&what=places`);
      toast(pl.filter((p) => p.type === "usb" || p.type === "internal").map((p) => `${p.label} : ${T("%s libres", size(p.free))}`).join(" · "));
    } catch (e) { toast(e.message); }
  });
}
// PS5 à distance : liaison par Internet (docs/PROTOCOLE-link.md §11) — mêmes réglages et actions que la télé.
const N_ = (x) => x;
const WAN_TEXTS = [  // textes du test de connexion (envoyés par le moteur, traduits ici)
  N_("Liaison par Internet désactivée ici"), N_("Appareils › PS5 à distance : activez-la pour que l'autre PS5 puisse vous joindre."),
  N_("Écoute impossible sur le port %s"), N_("Un autre programme utilise ce port : choisissez-en un autre dans Appareils › PS5 à distance."),
  N_("Liaison par Internet active ici (port %s)"), N_("Adresse de « %s » inconnue"), N_("Elle n'a pas d'adresse joignable (LTE, Starlink…) : c'est elle qui appelle cette PS5."),
  N_("Port %s fermé ou injoignable chez « %s » (%s)"),
  N_("Il faut une redirection du port %s (TCP) vers sa PS5 dans son routeur ou son pare-feu (Fortinet : « Virtual IP »), ou que son adresse soit la bonne."),
  N_("« %s » répond : %s, %s ms"), N_("Canal chiffré refusé par « %s »"), N_("Le jumelage n'est plus reconnu là-bas : oubliez-la et invitez-la de nouveau."),
  N_("Canal chiffré établi avec « %s »"), N_("Cette PS5 est joignable depuis Internet (%s:%s)"), N_("Cette PS5 n'est pas joignable depuis Internet (%s)"),
  N_("Pas grave : elle garde une connexion ouverte vers « %s », qui s'en sert pour l'appeler."), N_("Pas de réponse dans le canal chiffré"),
  N_("« %s » garde une connexion ouverte vers cette PS5"), N_("Cette PS5 garde une connexion ouverte vers « %s »"), N_("Aucun chemin vers « %s »"),
  N_("Il faut qu'au moins une des deux PS5 soit joignable depuis Internet (redirection de port)."),
];
void WAN_TEXTS;
async function remotePs5() {
  let w, peers = [];
  try { [w, peers] = await Promise.all([api("/wan"), api("/link/peers")]); } catch (e) { toast(e.message); return; }
  const box = document.createElement("div");
  box.className = "modal";
  const remote = peers.filter((p) => p.remote && p.paired);
  box.innerHTML = `<div class="dialog wide"><h2>${T("PS5 à distance")}</h2>
    <p class="hint">${esc(w.enabled ? T("Une PS5 jumelée par Internet : ses disques, ses parties, envoyer un jeu ou une sauvegarde, la synchro et les mises à jour, comme avec une PS5 du salon. Tout est chiffré.")
                                    : T("Désactivée : rien n'écoute sur Internet. Activez-la sur les deux PS5 pour en jumeler une à distance."))}</p>
    <ol class="hint lk-steps"><li>${T("1. Activez la liaison par Internet sur les deux PS5.")}</li><li>${T("2. Il suffit qu'UNE des deux soit joignable : redirigez son port (TCP) vers elle dans le routeur ou le pare-feu.")}</li>
      <li>${T("3. Sur celle-là, « Créer une invitation » ; donnez le code à l'autre personne.")}</li><li>${T("4. Sur l'autre PS5 : « J'ai un code d'invitation ». Ensuite, c'est une PS5 jumelée comme les autres.")}</li></ol>
    <label class="radio"><input type="checkbox" id="wan-on" ${w.enabled ? "checked" : ""}><span class="grow"><b>${T("Liaison par Internet")}</b><br><span class="hint">${esc(w.enabled ? (w.listening ? T("Liaison par Internet active ici (port %s)", String(w.port)) : w.error || "") : T("Liaison par Internet : désactivée"))}</span></span></label>
    <div class="lk-row"><label class="field-row">${T("Port")}<input class="field" id="wan-port" value="${esc(String(w.port))}" inputmode="numeric"></label>
      <label class="field-row">${T("Adresse publique")}<input class="field" id="wan-host" value="${esc(w.public_host || "")}" placeholder="${esc(w.observed_ip || "maison.exemple.net")}"></label>
      <button class="secondary small" id="wan-detect">${T("Détecter l'adresse publique")}</button><button class="secondary small" id="wan-save">${T("Enregistrer")}</button></div>
    ${w.observed_ip ? `<p class="hint">${esc(T("Vue par l'autre PS5 : %s", w.observed_ip))}</p>` : ""}
    <h4>${T("Inviter une PS5 à distance")}</h4>
    <div class="lk-row"><button class="primary" id="wan-invite" ${w.enabled ? "" : "disabled"}>${T("Créer une invitation…")}</button><span id="wan-code" class="hint"></span></div>
    <h4>${T("J'ai un code d'invitation…")}</h4>
    <div class="lk-row"><input class="field" id="wan-join-code" placeholder="PBC-…"><button class="primary" id="wan-join" ${w.enabled ? "" : "disabled"}>${T("Rejoindre")}</button></div>
    ${remote.length ? `<h4>${T("Jumelées à distance")}</h4><div class="list">${remote.map((p) => `<div class="row"><div class="grow"><div class="t">${esc(p.name || p.host)}</div><div class="d">${esc(wanWhere(p))} · ${p.online ? T("En ligne") : T("Hors ligne")}</div></div><span class="vbtns"><button class="secondary small" data-wt="${esc(p.instance)}" data-name="${esc(p.name || p.host)}">${T("Tester la connexion")}</button></span></div>`).join("")}</div>` : ""}
    <div class="actions"><button class="secondary" id="wan-close">${T("Fermer")}</button></div></div>`;
  document.body.appendChild(box);
  const close = () => box.remove();
  const $b = (q) => box.querySelector(q);
  $b("#wan-close").onclick = close;
  const save = async (extra) => {
    const port = parseInt($b("#wan-port").value, 10);
    if (!(port >= 1024 && port <= 65535)) { toast(T("Port invalide")); return false; }
    try { state.settings = await post("/settings", { wan_enabled: $b("#wan-on").checked, wan_port: port, wan_public_host: $b("#wan-host").value.trim(), ...(extra || {}) }); return true; } catch (e) { toast(e.message); return false; }
  };
  $b("#wan-on").onchange = async () => { if (await save()) { toast($b("#wan-on").checked ? T("Liaison par Internet activée") : T("Liaison par Internet désactivée")); close(); remotePs5(); } };
  $b("#wan-save").onclick = async () => { if (await save()) { toast(T("Réglage enregistré")); close(); remotePs5(); } };
  $b("#wan-detect").onclick = async () => { try { const r = await post("/wan/detect-ip", {}); $b("#wan-host").value = r.ip; toast(T("Adresse publique : %s", r.ip)); } catch (e) { toast(e.message); } };
  $b("#wan-invite").onclick = async () => {
    if (!(await save())) return;
    try {
      const r = await post("/wan/invite", {});
      $b("#wan-code").innerHTML = `<b class="mono">${esc(r.code)}</b> <button class="secondary small" id="wan-copy">${T("Copier")}</button><br>${esc(T("Donnez ce code à la personne qui a l'autre PS5 (message, téléphone ; il se copie depuis le web). À usage unique, valable 24 h. Il mène à %s, port %s : ce port doit être redirigé vers cette PS5.", r.host, String(r.port)))}`;
      $b("#wan-copy").onclick = async () => { try { await navigator.clipboard.writeText(r.code); toast(T("Code copié")); } catch (e) { toast(r.code); } };
    } catch (e) { toast(e.message); }
  };
  $b("#wan-join").onclick = async () => {
    const code = $b("#wan-join-code").value.trim();
    if (!code) return;
    toast(T("Connexion à l'autre PS5…"));
    try { const r = await post("/wan/join", { code }); toast(T("%s est jumelée par Internet", r.name)); close(); state.devicesAt = 0; render(); } catch (e) { toast(e.message); }
  };
  box.querySelectorAll("[data-wt]").forEach((b) => b.onclick = () => wanTest(b.dataset.wt, b.dataset.name));
}
function wanFill(key, args) { let i = 0; return T(key).replace(/%s/g, () => String((args || [])[i++] ?? "")); }
async function wanTest(inst, name) {
  toast(T("Test de la connexion avec %s…", name));
  let r;
  try { r = await post("/wan/test", { instance: inst }); } catch (e) { toast(e.message); return; }
  const icon = { ok: "✓", bad: "✕", warn: "!" };
  const html = `<div class="list">${(r.steps || []).map((st) => `<div class="row"><div class="grow"><div class="t"><span class="${st.state === "ok" ? "ok" : st.state === "bad" ? "bad" : "warn"}">${icon[st.state] || "·"}</span> ${esc(wanFill(st.key, st.args))}</div>${st.detail_key ? `<div class="d">${esc(wanFill(st.detail_key, st.detail_args))}</div>` : ""}</div></div>`).join("")}</div>`;
  await ask(r.ok ? T("Connexion avec %s : OK", name) : T("Connexion avec %s : à corriger", name), html, T("Fermer"), null);
}
async function reloadRemotes() { try { state.remotes = await api("/remotes"); } catch (e) {} refresh(true); }

// Réglages d'un appareil réseau (Switch ou FTP / NAS) : mêmes champs et mêmes actions que sur la télé.
const PORTS = { ftp: "21", smb: "445", webdav: "5006" };
const titleFor = (type) => type === "smb" ? T("Ajouter un partage SMB") : type === "webdav" ? T("Ajouter un NAS en WebDAV") : T("Ajouter un serveur FTP");
function openEditor(id, type, preset) {
  type = type || "ftp";
  const r = state.remotes.find((x) => x.id === id), c = (r && r.config) || {};
  if (r && r.type === "s3") { openS3Editor(id); return; }
  if (r && (r.type === "gdrive" || r.type === "onedrive" || r.type === "dropbox")) { openDriveEditor(id); return; }  // stockage S3 : son propre formulaire
  if (r && (r.type === "smb" || r.type === "webdav")) type = r.type;
  const sw = type === "switch";
  let swProto = c.proto === "smb" ? "smb" : "ftp";  // Switch : FTP (ftpd, sphaira) ou SMB
  const v = preset ? { name: preset.name, host: preset.host, port: preset.port, user: "", root: preset.root, layout: "pbc" } : { name: r ? r.name : "", host: c.host || "", port: c.port ? String(c.port) : sw ? "5000" : "21", user: r ? c.user || "" : sw ? "anonymous" : "", root: c.root || "/", layout: c.layout === "platform" ? "platform" : "pbc" };
  const box = document.createElement("div");
  box.className = "modal";
  const field = (k, label, val, extra = "") => `<label class="field-row">${label}<input class="field" id="ed-${k}" value="${esc(val)}" ${extra}></label>`;
  box.innerHTML = `<div class="dialog"><div class="ed-head"><span class="dev-ico">${sw ? ICONS.switch : ICONS.nas}</span><div>
      <h2 id="ed-title">${id ? T("Régler l'appareil") : sw ? T("Ajouter une Switch") : titleFor(type)}</h2>
      <p class="hint">${sw ? T("Sur la Switch : ftpd ouvert (FTP), ou un serveur SMB ; l'adresse s'affiche sur son écran.") : T("Adresse, port et compte du serveur (NAS, PC…).")}</p></div></div>
    ${sw ? `<label class="field-row">${T("Protocole")}<select id="ed-proto"><option value="ftp" ${swProto !== "smb" ? "selected" : ""}>FTP (ftpd, sphaira…)</option><option value="smb" ${swProto === "smb" ? "selected" : ""}>SMB</option></select></label>` : `<label class="field-row">${T("Protocole")}<select id="ed-proto"><option value="ftp" ${type === "ftp" ? "selected" : ""}>FTP / FTPS</option><option value="smb" ${type === "smb" ? "selected" : ""}>SMB (Windows, Synology, QNAP…)</option><option value="webdav" ${type === "webdav" ? "selected" : ""}>WebDAV (Synology, QNAP, Nextcloud…)</option></select></label>`}
    ${field("name", T("Nom"), v.name, `placeholder="${sw ? esc(T("Switch du salon")) : "NAS"}"`)}
    ${field("host", T("Adresse IP"), v.host, 'placeholder="192.168.1.20" inputmode="decimal"')}
    ${field("port", T("Port"), v.port, 'inputmode="numeric"')}
    ${field("user", T("Utilisateur"), v.user, 'autocomplete="off"')}
    <label class="field-row">${T("Mot de passe")}<input class="field" id="ed-password" type="password" autocomplete="new-password" placeholder="${esc(id ? T("(inchangé)") : T("(aucun)"))}"></label>
    <label class="field-row">${T("Dossier")}<span class="row-inline"><input class="field" id="ed-root" value="${esc(v.root)}"><button class="secondary small" id="ed-browse" type="button">${T("Parcourir…")}</button></span></label>
    ${sw ? "" : `<label class="field-row">${T("Chiffrement (FTPS)")}<select id="ed-tls">${[["auto", T("Automatique (FTPS si le serveur le propose)")], ["on", T("Toujours (FTPS obligatoire)")], ["off", T("Jamais (FTP simple)")]].map(([k, l]) => `<option value="${k}" ${(c.tls || "auto") === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>`}
    ${sw ? "" : `<label class="field-row">${T("Utilisé pour")}<select id="ed-use"><option value="both">${T("Sauvegardes et Synchro")}</option><option value="backups" ${r && r.use === "backups" ? "selected" : ""}>${T("Sauvegardes seulement")}</option><option value="sync" ${r && r.use === "sync" ? "selected" : ""}>${T("Synchro seulement")}</option></select></label>`}
    ${sw ? "" : `<label class="field-row">${T("Rangement")}<select id="ed-layout"><option value="pbc" ${v.layout === "pbc" ? "selected" : ""}>${T("Dossier PBC Backup")}</option><option value="platform" ${v.layout === "platform" ? "selected" : ""}>${T("Par plateforme (PS5 backup…)")}</option></select></label>`}
    <div class="actions"><button class="primary" id="ed-save">${T("Enregistrer")}</button><button class="secondary" id="ed-test">${T("Tester la connexion")}</button>${id ? `<button class="secondary" id="ed-adopt" title="${esc(T("Déplace « PS5 backup », « Switch backup »… de la racine dans « PBC Backup » (renommage, rien n'est copié ni effacé)"))}">${T("Ranger sous PBC Backup")}</button>` : ""}${id ? `<button class="danger" id="ed-forget">${T("Oublier")}</button>` : ""}<button class="secondary" id="ed-close">${T("Fermer")}</button></div>
    <p class="hint" id="ed-status">${T("1. Saisissez l'adresse et le compte · 2. Parcourir… pour choisir le dossier · 3. Enregistrer (la connexion et l'écriture sont testées).")}</p></div>`;
  document.body.appendChild(box);
  const close = () => box.remove();
  const status = (html) => { $("#ed-status", box).innerHTML = html; };
  const busy = (on) => { $$("button", box).forEach((b) => { b.disabled = on && b.id !== "ed-close"; }); if (on) status(`${SPIN}${T("Connexion à l'appareil…")}`); };
  $("#ed-close", box).onclick = close;
  $("#ed-name", box).focus();
  if (preset && preset.hint) status(esc(preset.hint));  // service proposé par son nom : ce qu'il faut saisir
  if ($("#ed-proto", box)) $("#ed-proto", box).onchange = () => {  // FTP ↔ SMB : le port suit, le chiffrement FTPS ne sert qu'au FTP
    const p = $("#ed-port", box);
    if (sw) {  // une Switch reste une Switch : seul le protocole change (le port suit : 5000 ↔ 445)
      swProto = $("#ed-proto", box).value;
      if (p.value === (swProto === "smb" ? "5000" : "445")) p.value = swProto === "smb" ? "445" : "5000";
      if (swProto === "smb") status(T("SMB : le premier dossier est le partage (ex. /Partage/Sauvegardes)."));
      return;
    }
    const before = type;
    type = $("#ed-proto", box).value;
    if (p.value === PORTS[before]) p.value = PORTS[type];  // FTP 21, SMB 445, WebDAV 5006
    if ($("#ed-tls", box)) $("#ed-tls", box).closest("label").style.display = type === "ftp" ? "" : "none";
    if (!id) $("#ed-title", box).textContent = titleFor(type);
    if (type === "smb") status(T("SMB : le premier dossier est le partage (ex. /Partage/Sauvegardes)."));
    if (type === "webdav") status(T("WebDAV : activez « WebDAV Server » sur le NAS (Synology : port 5006 en https)."));
  };
  if (type !== "ftp" && $("#ed-tls", box)) $("#ed-tls", box).closest("label").style.display = "none";
  // Parcourir : connexion avec les réglages saisis (mot de passe gardé sur la console s'il n'est pas redonné),
  // pour choisir le dossier sans le taper — même avant d'enregistrer.
  $("#ed-browse", box).onclick = () => {
    const host = $("#ed-host", box).value.trim();
    if (!host) { status(T("Saisissez d'abord l'adresse IP.")); return; }
    const config = { type, id, host, port: parseInt($("#ed-port", box).value, 10) || 0, user: $("#ed-user", box).value.trim(), password: $("#ed-password", box).value,
      tls: $("#ed-tls", box) ? $("#ed-tls", box).value : "auto", proto: sw ? swProto : undefined };
    openBrowser({ title: $("#ed-name", box).value.trim() || host, start: $("#ed-root", box).value || "/", config, pick: (p) => { $("#ed-root", box).value = p; status(T("Dossier choisi : Enregistrer pour le garder.")); } });
  };
  const formConfig = () => ({ type, id, host: $("#ed-host", box).value.trim(), port: parseInt($("#ed-port", box).value, 10) || 0, user: $("#ed-user", box).value,
    password: $("#ed-password", box).value, root: $("#ed-root", box).value.trim() || "/", layout: sw ? "pbc" : $("#ed-layout", box).value, tls: sw ? (c.tls || "auto") : $("#ed-tls", box).value, proto: sw ? swProto : undefined });
  // Tester : connexion + écriture d'un témoin dans le dossier choisi, avec ce qui est saisi. Rien n'est gardé.
  $("#ed-test", box).onclick = async () => {
    const cfg = formConfig();
    if (!cfg.host) { status(`<span class="bad">${T("Indiquez l'adresse IP de l'appareil.")}</span>`); return; }
    busy(true);
    try { await post("/remotes/test-config", cfg); busy(false); status(`<span class="ok">${sw ? T("Connexion réussie") : esc(T("Connexion réussie : écriture possible dans « %s »", cfg.root))}</span>`); }
    catch (e) { busy(false); status(`<span class="bad">${esc(e.message)}</span>`); }
  };
  $("#ed-save", box).onclick = async () => {
    const body = { type, name: $("#ed-name", box).value.trim(), host: $("#ed-host", box).value.trim(), port: parseInt($("#ed-port", box).value, 10) || 0,
      user: $("#ed-user", box).value, password: $("#ed-password", box).value, root: $("#ed-root", box).value.trim() || "/", layout: sw ? "pbc" : $("#ed-layout", box).value, tls: sw ? (c.tls || "auto") : $("#ed-tls", box).value, proto: sw ? swProto : undefined, use: sw ? undefined : $("#ed-use", box).value };
    if (!body.host) { status(`<span class="bad">${T("Indiquez l'adresse IP de l'appareil.")}</span>`); return; }
    busy(true);
    try {
      await post(id ? `/remotes/${encodeURIComponent(id)}/update` : "/remotes", body);
      toast(T("Appareil enregistré : connexion vérifiée")); close(); reloadRemotes();
    } catch (e) {  // deux synchros dans le même dossier : le moteur dit laquelle a déjà ce dossier
      busy(false);
      const taken = e.message.startsWith("sync_taken:") ? e.message.slice(11) : "";
      status(`<span class="bad">${esc(taken ? T("Ce dossier sert déjà à la synchro de « %s ». Choisissez un autre dossier, ou « Sauvegardes seulement ».", taken) : T("Connexion impossible : %s", e.message))}</span>`);
    }
  };
  if (!id) return;
  if ($("#ed-adopt", box)) $("#ed-adopt", box).onclick = async () => {
    busy(true);
    try {
      const j = await post(`/remotes/${encodeURIComponent(id)}/adopt`);
      busy(false);
      status(j.moved + j.merged ? `<span class="ok">${j.skipped ? T("Rangé sous PBC Backup : %d dossier(s) déplacé(s), %d laissé(s) en place (déjà présents).", j.moved, j.skipped) : T("Rangé sous PBC Backup : %d dossier(s) déplacé(s).", j.moved)}</span>` : T("Rien à ranger : tout est déjà sous PBC Backup."));
    } catch (e) { busy(false); status(`<span class="bad">${esc(e.message)}</span>`); }
  };
  let armed = false;
  $("#ed-forget", box).onclick = async () => {
    if (!armed) { armed = true; $("#ed-forget", box).textContent = T("Confirmer : oublier"); status(T("Appuyez encore pour oublier cet appareil (rien n'est effacé dessus).")); return; }
    try { await post(`/remotes/${encodeURIComponent(id)}/forget`); toast(T("Appareil oublié")); close(); reloadRemotes(); } catch (e) { status(`<span class="bad">${esc(e.message)}</span>`); }
  };
}

// ---- Parties (ex-Sauvegardes, allégé) : « Où en sont mes parties ? » ---------------------------
// En haut, les JOUEURS (compte PS5 de la console, autres comptes vus dans les backups, chaque Switch sous son nom,
// Eden / ProsperoEden) ; dessous, les jeux du joueur choisi en grandes jaquettes avec leur état et leur avancée.
// Pour une Switch ou Eden : la comparaison Switch ⇄ Eden (ex-« Synchro »). À venir (moteur) : /players.
const hoursText = (sec) => sec < 3600 ? `${Math.max(1, Math.floor(sec / 60))} min` : `${Math.floor(sec / 3600)} h ${String(Math.floor(sec / 60) % 60).padStart(2, "0")}`;
const partyStatus = () => ({ console_newer: [T("À sauvegarder"), "warn"], not_backed_up: [T("Jamais sauvegardée"), "warn"], up_to_date: [T("À jour"), "ok"],
  backup_newer: [T("Sauvegarde plus récente"), "info"], backup_only: [T("Seulement en sauvegarde"), "muted"], system: [T("Données système"), "muted"] });
const partyViews = () => [["all", T("Tout")], ["todo", T("À sauvegarder")], ["backup_newer", T("Sauvegarde plus récente")], ["not_backed_up", T("Jamais sauvegardée")]];  // mêmes vues que la télé
const partyKey = (e) => e.account + "|" + e.title_id;
const consoleAccounts = () => ((state.overview && state.overview.accounts) || []).filter((a) => a.on_console);
function loadEden() {
  if (state.edenProfiles) return Promise.resolve(state.edenProfiles);
  if (!loadEden.p) loadEden.p = api("/eden/profiles").then((p) => { state.edenProfiles = p || []; }).catch(() => { state.edenProfiles = []; }).finally(() => { loadEden.p = null; });
  return loadEden.p;
}
const edenRoots = () => [...new Set((state.edenProfiles || []).map((p) => p.root))];
const edenName = (root) => (root.endsWith("prosperoeden") ? "ProsperoEden" : "ProsperoEden (/data/eden)");  // /data/eden : ancien dossier de ProsperoEden
// Nombre de jeux différents dans une comparaison déjà faite avec ce côté (-1 : pas encore comparé).
function syncDiff(spec) {
  for (const [k, v] of Object.entries(state.syncCache || {})) if (k.split("|").includes(spec)) return v.data.entries.filter((e) => ["a_newer", "b_newer", "same_day"].includes(e.status)).length;
  return -1;
}
function players() {
  const ov = state.overview || { accounts: [], entries: [] }, out = [];
  for (const a of ov.accounts.slice().sort((x, y) => (y.on_console - x.on_console) || x.name.localeCompare(y.name))) {
    const todo = ov.entries.filter((e) => e.account === a.key && (e.status === "console_newer" || e.status === "not_backed_up")).length;
    out.push({ id: a.key, kind: "ps5", name: a.name, sub: `${a.platform} · ${a.on_console ? T("cette console") : T("autre console")}`,
      state: todo ? T("%d jeu(x) à sauvegarder", todo) : T("à jour"), tone: todo ? "warn" : "ok", icon: ICONS.ps5, acc: a });
  }
  for (const r of state.remotes.filter((x) => x.type === "switch")) {
    const d = syncDiff("switch:" + r.id);
    out.push({ id: "switch:" + r.id, kind: "switch", name: r.name, sub: "Switch", state: d < 0 ? T("pas encore comparée") : d ? T("%d jeu(x) différent(s)", d) : T("à jour"),
      tone: d > 0 ? "warn" : d === 0 ? "ok" : "muted", icon: ICONS.switch });
  }
  for (const root of edenRoots()) {
    const n = state.edenProfiles.filter((p) => p.root === root).length;
    out.push({ id: "eden:" + root, kind: "eden", name: edenName(root), sub: T("Émulateur Switch sur la PS5"), state: T("%d profil(s)", n), tone: "muted", icon: ICONS.ps5 });
  }
  return out;
}
function partiesView() {
  const P = state.party;
  if (!state.overview || Date.now() - state.overviewAt > 15000) loadOverview().then(() => { if (state.tab === "saves") render(); });
  if (!state.edenProfiles && !loadEden.p) loadEden().then(() => { if (state.tab === "saves") render(); });
  if (!state.overview) { $("#view").innerHTML = `<h1>${T("Parties")}</h1>${loading(T("Lecture des sauvegardes des comptes…"))}`; return; }
  const list = players();
  if (!list.some((p) => p.id === P.player)) P.player = list.length ? list[0].id : "";
  const cur = list.find((p) => p.id === P.player);
  $("#view").innerHTML = `<h1>${T("Parties")}</h1><p class="hint tab-help">${esc(T("Les parties actives, par joueur, en vraie synchro : PS5 ↔ PS5, Switch ↔ émulateur, Switch ↔ Switch. À sauvegarder, à restaurer, à nettoyer."))}</p>
    <div class="tr-label">${T("JOUEURS")}</div>
    <div class="players" data-scroll="players">${list.map((p) => `<button class="tr-dev player ${p.id === P.player ? "chosen" : ""}" data-player="${esc(p.id)}">
      <span class="dev-ico">${p.icon}</span><span class="grow"><b>${esc(p.name)}</b><span class="hint">${esc(p.sub)}</span><span class="pstate ${p.tone}">${esc(p.state)}</span></span></button>`).join("")}</div>
    <div id="party-main"></div>`;
  $$("[data-player]").forEach((b) => b.onclick = () => { P.player = b.dataset.player; P.sel.clear(); partiesView(); });
  if (!cur) { $("#party-main").innerHTML = `<div class="empty">${T("Aucun joueur pour l'instant.")}</div>`; return; }
  if (cur.kind === "ps5") return partyGames(cur.acc);
  // Switch / Eden : la comparaison des deux côtés, jeu par jeu (le contenu est vérifié, pas seulement la date).
  const sw = state.remotes.filter((r) => r.type === "switch"), roots = edenRoots();
  if (cur.kind === "switch") { state.syncA = cur.id; if (!(state.syncB || "").startsWith("eden:")) state.syncB = roots.length ? "eden:" + roots[0] : ""; }
  else { state.syncB = cur.id; if (!(state.syncA || "").startsWith("switch:")) state.syncA = sw.length ? "switch:" + sw[0].id : ""; }
  $("#party-main").innerHTML = `<div class="acc-head"><h2>${esc(T("Les parties de %s", cur.name))}</h2><span class="hint">${esc(cur.sub)}</span></div><div id="sync-host"></div>`;
  syncView();
}
function partyGames(acc) {
  const P = state.party, ST = partyStatus();
  const mine = state.overview.entries.filter((e) => e.account === acc.key);
  const keep = (e) => P.view === "all" || (P.view === "todo" ? e.status === "console_newer" || e.status === "not_backed_up" : e.status === P.view);
  const order = { console_newer: 0, not_backed_up: 1, backup_newer: 2, up_to_date: 3, backup_only: 4 };
  const shown = mine.filter(keep).sort((a, b) => (order[a.status] ?? 9) - (order[b.status] ?? 9) || a.title.localeCompare(b.title));
  for (const k of [...P.sel]) if (!mine.some((e) => partyKey(e) === k)) P.sel.delete(k);
  const nOn = shown.filter((e) => P.sel.has(partyKey(e))).length;
  // Jaquette : l'élément de la bibliothèque, sinon l'icône officielle par son numéro (jeu seulement dans un backup).
  const itemOf = (e) => state.items.find((i) => i.key === e.item) || { key: e.item, title: e.title, kind: "game_ps5", platform: "PS5", has_cover: (e.item || "").startsWith("tid:"), apps: [] };
  // Avancée écrite par le jeu (base des sauvegardes de la console) et temps de jeu compté par l'app : comme la télé.
  const adv = (e) => {
    const pg = (e.console && e.console.progress) || {};
    let a = pg.text || "";
    if (pg.play_time_seconds > 0 && !a.includes(":")) a += (a ? " · " : "") + T("%s de jeu", hoursText(pg.play_time_seconds));
    if (e.play_time_tracked >= 60) a += (a ? " · " : "") + T("%s compté par l'app", hoursText(e.play_time_tracked));
    if (a) return a;
    const b = (e.backups || []).slice().sort((x, y) => y.created_unix - x.created_unix)[0];
    return b ? T("Dernière sauvegarde : %s", new Date(b.created_unix * 1000).toLocaleDateString(I18N.code)) + " · " + b.place_label : "";
  };
  $("#party-main").innerHTML = `<div class="acc-head"><h2>${esc(T("Les parties de %s", acc.name))}</h2><span class="hint">${esc(T("%d jeu(x)", mine.length))} · ${esc(acc.platform)} · ${esc(T("utilisateur %s", acc.user_id))}${acc.on_console ? "" : " · " + T("compte d'une autre console")}</span></div>
    <div class="filters"><div class="pills">${partyViews().map(([k, l]) => `<button data-pv="${k}" class="${P.view === k ? "active" : ""}">${l}</button>`).join("")}</div>
      ${allBoxHtml("party-all", nOn, shown.length)}<span class="grow"></span>${acc.on_console ? `<button class="primary" id="save-all">${T("Tout sauvegarder")}</button>` : ""}</div>
    ${shown.length ? `<div class="grid party-grid ${P.sel.size ? "selecting" : ""}">${shown.map((e) => {
      const [label, tone] = ST[e.status] || [e.status, "muted"], k = partyKey(e), on = P.sel.has(k);
      return `<div class="tile selectable party${on ? " marked" : ""}" data-pe="${esc(k)}" tabindex="0" role="button">${coverHtml(itemOf(e)).replace(/<\/div>$/, `<span class="pstate-pill ${tone}">${esc(label)}</span></div>`)}
        <span class="tile-ck">${ckHtml(`data-pk="${esc(k)}"`, on)}</span>${moreBtn(`data-pm="${esc(k)}"`)}
        <div class="name">${esc(cleanTitle(e.title))}</div><div class="meta progress-text">${esc(adv(e))}</div></div>`;
    }).join("")}</div>` : `<div class="empty">${T("Rien dans ce filtre.")}</div>`}`;
  const byKey = (k) => mine.find((e) => partyKey(e) === k);
  $$("[data-pv]").forEach((b) => b.onclick = () => { P.view = b.dataset.pv; partiesView(); });
  $$("[data-pk]").forEach((c) => c.onchange = () => { c.checked ? P.sel.add(c.dataset.pk) : P.sel.delete(c.dataset.pk); partiesView(); });
  const all = $("#party-all");
  setAllBox(all, nOn, shown.length);
  all.onchange = () => { for (const e of shown) all.checked ? P.sel.add(partyKey(e)) : P.sel.delete(partyKey(e)); partiesView(); };
  // Clic sur la jaquette : sauvegarder / restaurer (le menu actuel), comme Croix sur la télé ; « ⋯ » : le même menu.
  $$(".tile.party").forEach((t) => {
    const open = (e) => { if (e.target.closest(".ck, .tile-ck")) return; e.stopPropagation(); const en = byKey(t.dataset.pe); if (en) openMenu(e.target.closest(".more") || $(".cover", t), partyActions(acc, [en])); };
    t.onclick = open;
    t.onkeydown = (e) => { if (e.key === "Enter" && e.target === t) open(e); };
  });
  const sa = $("#save-all");
  if (sa) sa.onclick = () => queueSaves(acc, mine.filter((e) => e.console).map((e) => e.item));
  const picked = mine.filter((e) => P.sel.has(partyKey(e)));
  selBar(picked.length, partyActions(acc, picked, true), () => { P.sel.clear(); partiesView(); });
}
// Fermer l'application ouverte sur une PS5 jumelée, après confirmation nominative (comme sur la télé).
async function confirmCloseRemote(instance, consoleName, fg) {
  if (!fg || !fg.title_id) { toast(T("Aucune application ouverte sur %s", consoleName)); return; }
  const name = fg.title || fg.title_id;
  const note = fg.game ? T("C'est un jeu : la partie en cours, si elle n'est pas sauvegardée, sera perdue.") : fg.ours ? T("Le jeu envoyé apparaîtra sur l'accueil de cette console dès que l'application sera fermée.") : "";
  if (!(await ask(T("Fermer « %s » sur %s ?", name, consoleName), note ? `<p class="hint">${esc(note)}</p>` : "", T("Fermer maintenant")))) return;
  try { await post("/link/close-app", { instance, title_id: fg.title_id }); toast(T("« %s » est fermé sur %s", name, consoleName)); }
  catch (e) { toast(e.message); }
}

// Choisir le compte qui reçoit une sauvegarde : cette console (instance vide) ou une PS5 jumelée, comme sur la télé.
async function pickAccount(anchor, instance, consoleName, then) {
  anchor = anchor && anchor.isConnected ? anchor : document.body;
  let accs = [];
  try { accs = await api(instance ? `/link/peer?instance=${encodeURIComponent(instance)}&what=accounts` : "/accounts"); } catch (e) { toast(e.message); return; }
  if (!accs.length) { toast(T("Aucun compte sur %s", consoleName)); return; }
  setTimeout(() => openMenu(anchor, accs.map((a) => ({ label: a.name || a.user_id, hint: T("Sur quel compte de %s ?", consoleName), run: () => then(a.user_id, a.name || a.user_id) }))), 0);
}

// Sauvegarde actuelle d'un jeu → PS5 jumelée : console, puis compte qui la reçoit (re-signée si besoin), comme sur la télé.
async function sendSaveToPeer(anchor, item, title, uid) {
  anchor = anchor && anchor.isConnected ? anchor : document.body;
  if (!state.linkPeers) await mgLoadPeers();
  const peers = (state.linkPeers || []).filter((p) => p.paired && p.online);
  if (!peers.length) { toast(T("Aucune autre PS5 jumelée et allumée : Appareils › PlayStation 5.")); return; }
  openMenu(anchor, peers.map((p) => ({ label: p.name || p.host, run: async () => {
    let accs = [];
    try { accs = await api(`/link/peer?instance=${encodeURIComponent(p.instance)}&what=accounts`); } catch (e) { toast(e.message); return; }
    if (!accs.length) { toast(T("Aucun compte sur %s", p.name || p.host)); return; }
    setTimeout(() => openMenu(anchor, accs.map((a) => ({ label: a.name || a.user_id, hint: T("Sur quel compte de %s ?", p.name || p.host), run: async () => {
      try { await post("/link/send-current-save", { place: `peer:${p.instance}:internal`, item, account: uid || "", user_id: a.user_id });
        toast(T("La sauvegarde de « %s » part vers %s (%s) : suivie dans Tâches", title, p.name || p.host, a.name || a.user_id)); }
      catch (e) { toast(e.message); }
    } }))), 0);
  } })));
}

// Actions d'une ou plusieurs parties : Sauvegarder · Restaurer… · Toutes les versions · Ouvrir la fiche · Ignorer…
function partyActions(acc, list, bar = false) {
  const one = list.length === 1 && !bar ? list[0] : null, out = [];
  const savable = acc.on_console ? list.filter((e) => e.console) : [];
  if (savable.length) out.push({ label: T("Sauvegarder"), cls: bar ? "primary" : "", run: () => queueSaves(acc, savable.map((e) => e.item)) });
  if (one && (one.backups || []).some((v) => v.complete) && acc.key.startsWith("ps5:")) out.push({ label: T("Restaurer…"), run: () => restoreDialog(one, consoleAccounts()) });
  if (one && (one.backups || []).length) out.push({ label: T("Toutes les versions"), run: () => openVersions({ key: one.item, title_id: one.title_id }) });
  const it = one && state.items.find((i) => i.key === one.item);
  if (it) out.push({ label: T("Ouvrir la fiche"), run: () => openItem(it) });
  if (one && one.console && acc.key.startsWith("ps5:")) out.push({ label: T("Envoyer la sauvegarde à une PS5…"), run: () => sendSaveToPeer(document.activeElement, one.item, one.title, acc.key.slice(4)) });
  if (one && one.console && one.title_id) out.push({ label: T("Supprimer la partie de la console…"), cls: "bad", run: () => deleteConsoleSave(document.body, one.title_id, one.title, () => { state.overview = null; refresh(true); if (state.tab === "saves") partiesView(); }) });
  const ign = list.filter((e) => e.title_id);
  if (ign.length) out.push({ label: T("Ignorer…"), cls: bar ? "danger" : "bad", run: () => ignoreDialog(ign.map((e) => ({ field: "title_id", value: e.title_id, title: e.title })), one ? (one.backups || []).length : -1,
    () => { state.party.sel.clear(); state.overview = null; refresh(true); if (state.tab === "saves") partiesView(); }) });
  return out;
}
async function queueSaves(acc, items) {
  let ok = 0;
  for (const it of items) { try { await post("/tasks", { type: "backup", item: it, parts: ["save"], account: acc.user_id, place: "" }); ok++; } catch (e) { toast(e.message); } }
  if (ok) { toast(ok > 1 ? T("%d sauvegardes ajoutées aux tâches", ok) : T("Sauvegarde ajoutée aux tâches")); state.party.sel.clear(); refresh(true); }
}

// Restaurer une sauvegarde PS5 : quelle version, sur quel compte. On vérifie d'abord (rien n'est modifié),
// puis on restaure ; si le compte diffère, le portage n'a lieu qu'après confirmation.
function restoreDialog(entry, targets) {
  const versions = entry.backups.filter((v) => v && v.complete).sort((a, b) => b.created_unix - a.created_unix);
  if (!versions.length) { toast(T("Aucune version complète à restaurer.")); return; }
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog"><h2>${esc(T("Restaurer « %s »", entry.title))}</h2>
    <label class="field-row">${T("Version")}<select id="rs-version">${versions.map((v, i) => `<option value="${i}">${new Date(v.created_unix * 1000).toLocaleString(I18N.code)} · ${esc(v.place_label)}</option>`).join("")}</select></label>
    <label class="field-row">${T("Sur le compte")}<select id="rs-user">${targets.map((a) => `<option value="${esc(a.user_id)}">${esc(a.name)}</option>`).join("")}</select></label>
    <p class="hint">${T("La sauvegarde actuelle de ce jeu est copiée avant. Le jeu doit être fermé.")}</p>
    <div id="rs-status" class="hint"></div>
    <div class="actions"><button class="primary" id="rs-go">${T("Vérifier puis restaurer")}</button><button class="secondary" id="rs-close">${T("Fermer")}</button></div></div>`;
  document.body.appendChild(box);
  const close = () => box.remove();
  $("#rs-close").onclick = close;
  const status = (t) => { $("#rs-status").textContent = t; };
  const run = async (body) => {
    const r = await post("/tasks", body);
    for (;;) {
      await new Promise((res) => setTimeout(res, 1500));
      const t = (await api("/tasks")).tasks.find((x) => x.id === r.id);
      if (t && ["done", "failed", "cancelled"].includes(t.state)) return t;
      if (t && t.progress) status(body.check_only ? T("Vérification : %d/%d image(s)…", t.progress.files_done, t.progress.files_total) : T("Restauration : %d/%d image(s)…", t.progress.files_done, t.progress.files_total));
    }
  };
  $("#rs-go").onclick = async () => {
    const v = versions[+$("#rs-version").value];
    const base = { type: "restore_save", place: v.place, bundle: v.bundle, user_id: $("#rs-user").value, title_id: entry.title_id };
    $("#rs-go").disabled = true;
    try {
      status(T("Vérification…"));
      const check = await run({ ...base, check_only: true });
      if (check.state !== "done") { status(check.error || T("Vérification refusée")); $("#rs-go").disabled = false; return; }
      const imgs = (check.report && check.report.checked) || [];
      const other = imgs.some((i) => i.match === false);
      if (other && !confirm(T("Cette sauvegarde vient d'un autre compte (%s…). La porter sur ce compte (%s…) ? L'original reste dans la sauvegarde.", (imgs[0].save_account || "").slice(0, 6), (imgs[0].target_account || "").slice(0, 6)))) { status(T("Portage annulé : rien n'a été modifié.")); $("#rs-go").disabled = false; return; }
      const done = await run({ ...base, allow_resign: other });
      status(done.state === "done" ? T("Restaurée : %d image(s) vérifiée(s). Lancez le jeu pour confirmer la progression.", done.report.verified) : (done.error || T("Échec")));
      if (done.state === "done") toast(T("Sauvegarde restaurée"));
    } catch (e) { status(e.message); }
    $("#rs-go").disabled = false;
  };
}

// « Tout reprendre sur ce compte » (même écran que la télé) : pour chaque jeu, la partie de la console contre la
// meilleure copie de l'identifiant ; la plus avancée est cochée ; Valider = une restauration re-signée par jeu.
async function takeover(from) {
  const p0 = ((state.players || {}).players || [])[0];
  if (!p0) return;
  let tk;
  try { tk = await api(`/players/takeover?from=${encodeURIComponent(from)}&user_id=${encodeURIComponent(p0.user_id)}`); } catch (e) { toast(e.message); return; }
  const play = (s) => (s > 0 ? (s < 3600 ? `${Math.max(1, Math.floor(s / 60))} min` : `${Math.floor(s / 3600)} h ${String(Math.floor(s / 60) % 60).padStart(2, "0")}`) : "");
  const txt = (o) => [o.progress_text, play(o.play_time_seconds || 0)].filter(Boolean).join(" · ");
  const rows = tk.games.map((g) => `<tr><td>${ckHtml(`data-tk-tid="${esc(g.title_id)}" ${g.running ? "disabled" : ""}`, g.suggest && !g.running)}</td>
      <td><b>${esc(cleanTitle(g.title))}</b>${g.running ? `<br><span class="hint warn">${T("jeu en cours : laissé de côté")}</span>` : ""}</td>
      <td class="${g.suggest ? "hint" : "ok"}">${esc(g.console ? txt(g.console) || T("partie sans détail") : T("pas de partie"))}</td>
      <td class="${g.suggest ? "ok" : "hint"}">${esc(txt(g.copy) || g.copy.place_label)}</td></tr>`).join("");
  const sel = new Set(tk.games.filter((g) => g.suggest && !g.running).map((g) => g.title_id));
  const onChange = (e) => { const c = e.target; if (c && c.dataset && c.dataset.tkTid) c.checked ? sel.add(c.dataset.tkTid) : sel.delete(c.dataset.tkTid); };
  document.addEventListener("change", onChange);
  const ok = await ask(T("Tout reprendre sur %s", p0.name), tk.games.length
    ? `<p class="hint">${esc(T("Pour chaque jeu, la partie la plus avancée est cochée. Chaque partie actuelle est copiée avant ; rien n'est supprimé."))}</p>
      <div class="tk-wrap"><table class="tk"><thead><tr><th></th><th>${T("Jeu")}</th><th>${T("Sur la console")}</th><th>${T("Copie de cet identifiant")}</th></tr></thead><tbody>${rows}</tbody></table></div>`
    : `<p class="hint">${T("Aucune copie de cet identifiant à reprendre.")}</p>`, T("Reprendre"));
  document.removeEventListener("change", onChange);
  if (!ok) return;
  const ids = [...sel];
  if (!ids.length) { toast(T("Cochez d'abord des jeux (L2)")); return; }
  try { await post("/players/takeover", { from, user_id: p0.user_id, title_ids: ids }); toast(T("%d reprise(s) ajoutée(s) aux tâches", ids.length)); refresh(true); } catch (e) { toast(e.message); }
}

// ---- Synchronisation : deux côtés (Switch, Eden… puis PS5/PS4, cartes mémoire), par jeu et par profil ----
const SYNC_STATUS = { identical: [T("Identiques"), "ok"], a_newer: [T("Plus récent à gauche"), "info"], b_newer: [T("Plus récent à droite"), "info"],
  same_day: [T("Même jour, contenu différent"), "warn"], only_a: [T("Seulement à gauche"), "muted"], only_b: [T("Seulement à droite"), "muted"] };
// Affichée dans « #sync-host » : Parties (une Switch ou Eden) et Transferts › Synchroniser.
async function syncView() {
  await loadEden();
  const remotes = state.remotes, profiles = state.edenProfiles || [];
  const main = $("#sync-host");
  if (!main) return;
  const sides = remotes.filter((r) => r.type === "switch").map((r) => ({ spec: "switch:" + r.id, label: r.name }))
    .concat([...new Set(profiles.map((p) => p.root))].map((root) => ({ spec: "eden:" + root, label: edenName(root) })));
  if (sides.length < 2) { main.innerHTML = `<div class="empty">${T("Ajoutez une Switch (ftpd + JKSV) dans les appareils pour synchroniser ses sauvegardes avec Eden.")}</div>`; return; }
  state.syncA = state.syncA && sides.find((x) => x.spec === state.syncA) ? state.syncA : sides[0].spec;
  state.syncB = state.syncB && sides.find((x) => x.spec === state.syncB) ? state.syncB : sides.find((x) => x.spec.startsWith("eden:")).spec;
  state.syncFilter = state.syncFilter || "diff";
  const opt = (sel) => sides.map((x) => `<option value="${esc(x.spec)}" ${x.spec === sel ? "selected" : ""}>${esc(x.label)}</option>`).join("");
  main.innerHTML = `<div class="sync-sides"><label class="field-row">${T("Côté gauche")}<select id="sync-a">${opt(state.syncA)}</select></label>
    <span class="sync-arrow">⇄</span><label class="field-row">${T("Côté droit")}<select id="sync-b">${opt(state.syncB)}</select></label></div>
    <div class="filters"><div class="pills">${[["diff", T("Différentes")], ["identical", T("Identiques")], ["only", T("D'un seul côté")], ["all", T("Tout")]].map(([k, l]) => `<button data-sf="${k}" class="${state.syncFilter === k ? "active" : ""}">${l}</button>`).join("")}</div>
      <button class="secondary" id="sync-again">${T("Resynchroniser")}</button><span class="hint" id="sync-when"></span></div>
    <div id="sync-list" class="list">${loading(T("Comparaison des sauvegardes… (le contenu est vérifié, pas seulement la date)"))}</div>`;
  $("#sync-again").onclick = () => { if (state.syncCache) delete state.syncCache[state.syncA + "|" + state.syncB]; state.syncData = null; syncView(); };
  $("#sync-a").onchange = (e) => { state.syncA = e.target.value; state.syncData = null; syncView(); };
  $("#sync-b").onchange = (e) => { state.syncB = e.target.value; state.syncData = null; syncView(); };
  document.querySelectorAll("[data-sf]").forEach((b) => b.onclick = () => { state.syncFilter = b.dataset.sf; syncView(); });
  const key = state.syncA + "|" + state.syncB;
  // Une comparaison déjà faite est gardée (par paire de côtés) : changer de filtre ou d'onglet ne la perd pas.
  state.syncCache = state.syncCache || {};
  if (!state.syncData || state.syncData.key !== key) state.syncData = state.syncCache[key] || null;
  if (!state.syncData) {
    $("#sync-list").innerHTML = loading(T("Lecture des sauvegardes des deux côtés… (le contenu est vérifié, pas seulement la date — environ 10 secondes)"));
    state.syncPending = state.syncPending || {};
    const pending = state.syncPending[key] || (state.syncPending[key] = api(`/sync/compare?a=${encodeURIComponent(state.syncA)}&b=${encodeURIComponent(state.syncB)}`).finally(() => { delete state.syncPending[key]; }));
    try { state.syncData = state.syncCache[key] = { key, at: Date.now(), data: await pending }; }
    catch (e) { if ($("#sync-list")) $("#sync-list").innerHTML = `<div class="empty">${esc(e.message)}</div>`; return; }
    if (!$("#sync-list")) return;
    if (state.syncA + "|" + state.syncB !== key) return;
    // Nouvelle comparaison : l'état des joueurs et des synchros habituelles change aussi.
    if (state.tab === "saves") return partiesView();
    if (state.tab === "transfers" && state.tr.screen === "sync") return trSync();
  }
  const d = state.syncData.data, A = d.a.name, Bn = d.b.name;
  if (state.syncData.at && $("#sync-when")) $("#sync-when").textContent = T("comparé à %s", new Date(state.syncData.at).toLocaleTimeString(I18N.code));
  const keep = (e) => state.syncFilter === "all" || (state.syncFilter === "identical" ? e.status === "identical"
    : state.syncFilter === "only" ? e.status.startsWith("only") : ["a_newer", "b_newer", "same_day"].includes(e.status));
  const list = d.entries.filter(keep).sort((x, y) => x.title.localeCompare(y.title) || x.profile.localeCompare(y.profile));
  const date = (s) => s ? new Date(s.time * 1000).toLocaleDateString(I18N.code) : "—";
  const canTo = (spec) => spec.startsWith("eden:") || spec.startsWith("switch:");
  $("#sync-list").innerHTML = list.length ? list.map((e, i) => {
    const [label, tone] = SYNC_STATUS[e.status] || [e.status, "muted"];
    const it = state.items.find((x) => x.platform === "Switch" && x.title_id === e.title_id && x.has_cover)
      || { key: "tid:switch:" + e.title_id, title: e.title, kind: "rom", platform: "Switch", has_cover: /^[0-9A-Fa-f]{16}$/.test(e.title_id || "") };
    const sel = !activeState(t.state) ? `<input type="checkbox" class="tk-sel" data-tk-sel="${esc(t.id)}" ${state.taskSel && state.taskSel.has(t.id) ? "checked" : ""} aria-label="${esc(T("Cocher"))}">` : "";
  return `<div class="row">${sel}<div class="thumb">${coverHtml(it, true)}</div><div><div class="t">${esc(e.title)} <span class="hint">· ${esc(e.profile === "device" ? T("commun à la console") : e.profile)}</span> <span class="tag ${tone}">${label}</span></div>
      <div class="chips"><span>${esc(A)} : ${date(e.a)}</span><span>${esc(Bn)} : ${date(e.b)}</span></div></div>
      <div class="btns">${e.a && canTo(state.syncB) ? `<button data-sync="${i}" data-dir="ab">${esc(A)} → ${esc(Bn)}</button>` : ""}${e.b && canTo(state.syncA) ? `<button data-sync="${i}" data-dir="ba">${esc(Bn)} → ${esc(A)}</button>` : ""}</div></div>`;
  }).join("") : `<div class="empty">${T("Rien dans ce filtre.")}</div>`;
  document.querySelectorAll("[data-sync]").forEach((b) => b.onclick = () => syncTransfer(list[+b.dataset.sync], b.dataset.dir, d));
}

// Une sauvegarde d'un côté vers l'autre : vérification d'abord, confirmation si la cible est plus récente.
async function syncTransfer(e, dir, d) {
  const from = dir === "ab" ? { spec: state.syncA, side: e.a, name: d.a.name } : { spec: state.syncB, side: e.b, name: d.b.name };
  const to = dir === "ab" ? { spec: state.syncB, side: e.b, name: d.b.name } : { spec: state.syncA, side: e.a, name: d.a.name };
  let body;
  if (from.spec.startsWith("switch:") && to.spec.startsWith("eden:")) {
    const prof = to.side ? to.side.where : null;
    const profiles = await api("/eden/profiles");
    const root = to.spec.slice(5);
    const target = prof ? prof.profile : (profiles.find((p) => p.root === root && (e.profile === "device" ? p.profile === "00000000000000000000000000000000" : p.name.toLowerCase() === e.profile)) || {}).profile;
    if (!target) { toast(T("Aucun profil « %s » dans %s : créez-le d'abord", e.profile, to.name)); return; }
    body = { type: "import_switch", remote: from.spec.slice(7), game: from.side.where.game, file: from.side.where.file, title_id: e.title_id, eden_root: root, profile: target };
  } else if (from.spec.startsWith("eden:") && to.spec.startsWith("switch:")) {
    body = { type: "export_switch", remote: to.spec.slice(7), eden_root: from.side.where.eden_root, profile: from.side.where.profile, title_id: e.title_id,
      game_folder: to.side ? to.side.where.game : e.title, profile_name: to.side ? to.side.profile_name : (e.profile === "device" ? "Device" : from.side.profile_name) };
  } else { toast(T("Ce sens n'est pas encore pris en charge")); return; }
  const wait = async (id) => { for (;;) { await new Promise((r) => setTimeout(r, 1200)); const t = (await api("/tasks")).tasks.find((x) => x.id === id); if (t && ["done", "failed", "cancelled"].includes(t.state)) return t; } };
  try {
    toast(T("Vérification…"));
    const check = await wait((await post("/tasks", { ...body, check_only: true })).id);
    if (check.state !== "done") { toast(check.error || T("Vérification refusée")); return; }
    const r = check.report || {};
    const newer = r.eden_newer ? "\n\n" + T("Attention : la sauvegarde dans %s est plus récente.", to.name) : "";
    const what = body.type === "export_switch" ? T("Un nouveau fichier sera déposé sur la Switch (%s). Rien n'y est effacé : restaurez-le ensuite avec JKSV.", r.target) : T("La sauvegarde actuelle de %s est copiée avant d'être remplacée.", to.name);
    if (!confirm(`${e.title} (${e.profile}) : ${from.name} → ${to.name}\n${T("%d fichier(s)", (r.files || []).length)}.\n${what}${newer}`)) return;
    const done = await wait((await post("/tasks", { ...body, force_older: !!r.eden_newer })).id);
    if (done.state === "done" && body.type === "export_switch") jksvGuide((done.report || r).target || r.target);
    else toast(done.state === "done" ? T("Synchronisé") : (done.error || T("Échec")));
    if (state.syncCache) delete state.syncCache[state.syncData.key];  // un transfert a changé un côté : on relit
    state.syncData = null;
    syncView();
  } catch (err) { toast(err.message); }
}

// Guide après un envoi vers la Switch : la restauration se termine dans JKSV.
function jksvGuide(target) {
  const file = target.split("/").pop(), game = target.split("/")[2] || "";
  const steps = [T("Sur la Switch, ouvrez JKSV (menu des homebrews)."), T("À gauche, choisissez votre profil (son avatar)."),
    T("À droite, allez sur « %s » et appuyez sur A.", game), T("Conseil : choisissez d'abord « Nouveau » pour garder une copie de la partie actuelle de la Switch."),
    T("Choisissez le fichier « %s » (il se termine par « PBC ») et appuyez sur Y pour le restaurer.", file), T("Lancez le jeu avec ce profil : vous retrouvez la partie envoyée depuis la PS5.")];
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog"><h2>${T("Dernière étape, sur la Switch")}</h2><ol class="guide">${steps.map((x) => `<li>${esc(x)}</li>`).join("")}</ol>
    <p class="hint">${T("Rien n'a été effacé sur la Switch. Si la partie ne convient pas, restaurez la copie faite à l'étape 4.")}</p><div class="actions"><button class="primary" id="g-ok">${T("Compris")}</button></div></div>`;
  document.body.appendChild(box);
  $("#g-ok").onclick = () => box.remove();
}

// ---- Tâches ---------------------------------------------------------------------------
// Comme la télé : Actives / Terminées / Échecs / Annulées / Sauvegardes automatiques (ex-« Plans programmés »).
// Plans : sauvegardes programmées (calendrier, contenu par règle ou jeux choisis, versions gardées = rotation).
const DAYS = [T("Lun."), T("Mar."), T("Mer."), T("Jeu."), T("Ven."), T("Sam."), T("Dim.")];  // avec un point : « Jeu » seul veut dire un jeu
const RULES = { "": T("Jeux choisis"), ps5_saves: T("Toutes mes sauvegardes PS5"), all_games: T("Tous mes jeux"), roms: T("Toutes mes ROM"), apps: T("Toutes mes applications") };
const PLAN_PARTS = [["all", T("Tout (jeu et sauvegarde)"), []], ["save", T("Sauvegardes seulement"), ["save"]], ["game", T("Jeu seulement"), ["game"]]];
const PLAN_PRESETS = [
  [T("Mes sauvegardes PS5, chaque soir à 21 h"), { name: T("Sauvegardes PS5 du soir"), timing: "daily", minute_of_day: 1260, rule: "ps5_saves", parts: ["save"], keep: 5 }],
  [T("Toutes mes ROM, chaque dimanche à 3 h"), { name: T("ROM du dimanche"), timing: "weekly", weekdays: 64, minute_of_day: 180, rule: "roms", keep: 2 }],
  [T("Mes applications, chaque lundi à 3 h"), { name: T("Applications du lundi"), timing: "weekly", weekdays: 1, minute_of_day: 180, rule: "apps", keep: 2 }],
  [T("Tous mes jeux, à la demande"), { name: T("Tous mes jeux"), timing: "manual", rule: "all_games", keep: 1 }]];
const blankPlan = (extra = {}) => ({ name: "", enabled: true, timing: "daily", minute_of_day: 21 * 60, weekdays: 0, rule: "", rule_arg: "", parts: [], place: "", keep: 5, items: [], ...extra });
const hhmm = (m) => `${String(Math.floor(m / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`;
function planWhen(p) {
  const hm = hhmm(p.minute_of_day || 0);
  if (p.timing === "daily") return T("Tous les jours à %s", hm);
  if (p.timing === "weekly") return T("%s à %s", DAYS.filter((d, i) => p.weekdays & (1 << i)).join(", ") || T("aucun jour"), hm);
  if (p.timing === "once") return T("Le %s", new Date(p.once_at * 1000).toLocaleString(I18N.code));
  return T("À la demande");
}
async function loadPlans() { try { state.plans = await api("/plans"); } catch (e) { state.plans = state.plans || []; } return state.plans; }
const TASK_GROUPS = { active: ["queued", "running", "paused"], done: ["done"], failed: ["failed"], cancelled: ["cancelled"] };
const TASK_LABELS = { active: T("Actives"), done: T("Terminées"), failed: T("Échecs"), cancelled: T("Annulées") };
function taskTabs() {
  const count = (g) => state.tasks.filter((t) => TASK_GROUPS[g].includes(t.state)).length;
  const cur = state.taskTab === "plans" ? "plans" : state.taskFilter;
  return `<div class="filters"><div class="pills">${Object.keys(TASK_GROUPS).map((g) => `<button data-g="${g}" class="${cur === g ? "active" : ""}">${TASK_LABELS[g]} ${count(g)}</button>`).join("")}
    <button data-g="plans" class="${cur === "plans" ? "active" : ""}">${T("Sauvegardes automatiques")}${state.plans ? " " + state.plans.length : ""}</button></div></div>`;
}
function bindTaskTabs() {
  $$(".pills button[data-g]").forEach((b) => b.onclick = () => {
    if (b.dataset.g === "plans") state.taskTab = "plans"; else { state.taskTab = "list"; state.taskFilter = b.dataset.g; }
    tasks();
  });
}
// Détails d'une tâche finie : quand, combien de temps, à quelle vitesse, et ce qui s'est passé.
async function taskDetails(t) {
  const p = t.progress || {}, r = t.report || {}, secs = t.started > 0 && t.finished >= t.started ? t.finished - t.started : 0;
  const when = (x) => (x > 0 ? new Date(x * 1000).toLocaleString(I18N.code) : "?");
  const L = [`${STATES[t.state] || t.state} · ${T("Début :")} ${when(t.started)}${t.finished > 0 ? ` · ${T("Fin :")} ${when(t.finished)}` : ""}`,
    T("%s sur %s", size(p.bytes_done || 0), size(p.bytes_total || 0)) + (p.files_total ? " · " + T("%d/%d fichiers", p.files_done, p.files_total) : "")];
  if (secs > 0) L.push(`${T("Durée :")} ${duration(secs)}${p.bytes_done ? " · " + T("%s/s en moyenne", size(Math.floor(p.bytes_done / secs))) : ""}`);
  if (t.type === "files_copy" || t.type === "files_move") {
    L.push(T("Copiés : %d · déjà là : %d · renommés : %d · erreurs : %d", r.copied || 0, r.skipped || 0, (r.renamed || []).length, (r.errors || []).length));
    if (r.removed > 0) L.push(T("Originaux retirés (déplacement) : %d", r.removed));
    for (const x of (r.renamed || []).slice(0, 20)) L.push(`↪ ${x.from} → ${x.to}`);
    for (const x of (r.errors || []).slice(0, 20)) L.push(`✕ ${x.file} : ${x.error}`);
    if (r.cleaned) L.push(T("Fichiers copiés retirés : %d", r.cleaned.removed || 0));
  }
  if (t.error) L.push(`${T("Erreur :")} ${t.error}`);
  const own = t.type === "backup" && t.state === "done";  // la sauvegarde faite par cette tâche
  const html = `<div class="hint" style="white-space:pre-wrap;user-select:text">${L.map(esc).join("<br>")}</div>`
    + (own ? `<div class="actions" style="margin-top:12px"><button class="secondary" id="tk-view">${T("Voir la sauvegarde (Toutes les versions)")}</button><button class="danger" id="tk-trash">${T("Mettre cette sauvegarde à la corbeille…")}</button></div>` : "");
  const finished = !activeState(t.state);
  if (own) setTimeout(() => {
    const close = () => { const m = $("#tk-view") && $("#tk-view").closest(".modal"); if (m) m.parentNode.removeChild(m); };
    if ($("#tk-view")) $("#tk-view").onclick = async () => {
      try { const j = await post(`/tasks/${t.id}/backup`); close(); openVersions({ key: j.item, title_id: j.title_id }); } catch (e) { toast(e.message); }
    };
    if ($("#tk-trash")) $("#tk-trash").onclick = async () => {
      try {
        const j = await post(`/tasks/${t.id}/backup`);
        close();
        if (!(await holdAsk(T("Mettre « %s » à la corbeille ?", j.title), T("La sauvegarde faite par cette tâche va dans la corbeille de son disque ou de son NAS pendant 30 jours. La tâche reste dans la liste."), T("Maintenir 5 s pour supprimer")))) return;
        await post("/backups/delete", { place: j.place, bundle: j.bundle, confirm: true });
        toast(T("Sauvegarde mise à la corbeille (30 jours)")); refresh(true);
      } catch (e) { toast(e.message); }
    };
  }, 0);
  if (await ask(t.title || TASK_KINDS[t.type] || T("Tâche"), html, finished ? T("Retirer de la liste (la sauvegarde reste)") : T("Fermer"), finished ? T("Fermer") : "") && finished) {
    try { await post(`/tasks/${t.id}/remove`); refresh(true); } catch (e) { toast(e.message); }
  }
}
// « Vider la liste » (Terminées, Échecs, Annulées) : une confirmation ; seulement la liste, aucun fichier.
async function clearTasks(group) {
  const n = state.tasks.filter((t) => TASK_GROUPS[group].includes(t.state)).length;
  const note = group === "done" ? T("Seulement la liste : rien n'est retiré des disques.") : T("Seulement la liste : rien n'est retiré des disques. Ces tâches ne pourront plus être reprises.");
  if (!await ask(T("Vider « %s » (%d) ?", TASK_LABELS[group], n), `<p class="hint">${esc(note)}</p>`, T("Oui, vider la liste"))) return;
  try {
    const j = await post("/tasks/clear", { state: group });
    toast(T("%d tâche(s) retirée(s) de la liste", j.removed || 0) + (j.kept ? " · " + T("%d copie(s) gardée(s) : fichiers copiés encore à retirer", j.kept) : ""));
    refresh(true);
  } catch (e) { toast(e.message); }
}
function tasks() {
  if (state.taskTab === "plans") return plansView();
  if (!state.plans) loadPlans().then(() => { if (state.tab === "tasks") render(); });
  const active = state.tasks.filter((t) => activeState(t.state)).length;
  const list = state.tasks.filter((t) => TASK_GROUPS[state.taskFilter].includes(t.state)).reverse();
  const canClear = state.taskFilter !== "active" && list.length > 0;
  state.taskSel = new Set([...(state.taskSel || [])].filter((id) => list.some((t) => t.id === id)));
  const nsel = state.taskSel.size, nbk = list.filter((t) => state.taskSel.has(t.id) && t.type === "backup" && t.state === "done").length;
  const selBar = canClear ? `<div class="actions" style="margin:0 0 12px">${nsel ? `<b>${T("%d coché(s)", nsel)}</b><button class="secondary" id="tk-sel-remove">${T("Retirer de la liste (%d)", nsel)}</button>${nbk ? `<button class="danger" id="tk-sel-trash">${T("Mettre leurs sauvegardes à la corbeille… (%d)", nbk)}</button>` : ""}<button class="secondary" id="tk-sel-none">${T("Tout décocher")}</button>` : `<button class="secondary" id="tk-sel-all">${T("Tout cocher")}</button>`}</div>` : "";
  $("#view").innerHTML = `<h1>${T("Tâches")}<small>${active > 1 ? T("%d actives", active) : T("%d active", active)}</small></h1>${taskTabs()}
    ${canClear ? `<div class="actions" style="margin:0 0 12px"><button class="secondary" id="tasks-clear">${T("Vider la liste")}</button><button class="secondary" id="tasks-clear-all">${T("Tout vider : Terminées, Échecs et Annulées")}</button></div>` : ""}
    ${selBar}<div class="list">${list.length ? list.map(taskRowHtml).join("") : `<div class="empty"><div class="icon">↻</div>${T("Rien ici. Choisissez un élément dans la bibliothèque pour lancer une sauvegarde.")}</div>`}</div>`;
  bindTaskTabs();
  bindTaskButtons();
  $$("input[data-tk-sel]").forEach((c) => c.onchange = () => { if (c.checked) state.taskSel.add(c.dataset.tkSel); else state.taskSel.delete(c.dataset.tkSel); render(); });
  if ($("#tk-sel-all")) $("#tk-sel-all").onclick = () => { list.forEach((t) => state.taskSel.add(t.id)); render(); };
  if ($("#tk-sel-none")) $("#tk-sel-none").onclick = () => { state.taskSel.clear(); render(); };
  if ($("#tk-sel-remove")) $("#tk-sel-remove").onclick = async () => {
    const ids = [...state.taskSel];
    try { for (const id of ids) await post(`/tasks/${id}/remove`); toast(T("%d tâche(s) retirée(s) de la liste", ids.length)); state.taskSel.clear(); refresh(true); } catch (e) { toast(e.message); }
  };
  if ($("#tk-sel-trash")) $("#tk-sel-trash").onclick = async () => {
    const ids = list.filter((t) => state.taskSel.has(t.id) && t.type === "backup" && t.state === "done").map((t) => t.id);
    if (!(await holdAsk(T("Mettre %d sauvegarde(s) à la corbeille ?", ids.length), T("Les sauvegardes faites par ces tâches vont dans la corbeille de leur disque ou de leur NAS pendant 30 jours. Les tâches restent dans la liste."), T("Maintenir 5 s pour supprimer")))) return;
    let done = 0, failed = 0;
    for (const id of ids) {
      try { const j = await post(`/tasks/${id}/backup`); await post("/backups/delete", { place: j.place, bundle: j.bundle, confirm: true }); done++; } catch (e) { failed++; }
    }
    toast(failed ? T("%d sauvegarde(s) mise(s) à la corbeille · %d introuvable(s) ou refusée(s)", done, failed) : T("%d sauvegarde(s) mise(s) à la corbeille (30 jours)", done));
    state.taskSel.clear(); refresh(true);
  };
  if (canClear) $("#tasks-clear").onclick = () => clearTasks(state.taskFilter);
  if (canClear) $("#tasks-clear-all").onclick = async () => {
    if (!await ask(T("Tout vider : Terminées, Échecs et Annulées"), `<p class="hint">${esc(T("Seulement la liste : rien n'est retiré des disques. Ces tâches ne pourront plus être reprises."))}</p>`, T("Oui, vider la liste"))) return;
    try { for (const st of ["done", "failed", "cancelled"]) await post("/tasks/clear", { state: st }); toast(T("Listes vidées : Terminées, Échecs et Annulées")); refresh(true); } catch (e) { toast(e.message); }
  };
}
async function plansView() {
  if (!state.plans) $("#view").innerHTML = `<h1>${T("Tâches")}</h1>${taskTabs()}${loading(T("Lecture des plans…"))}`;
  const plans = await loadPlans();
  if (state.tab !== "tasks" || state.taskTab !== "plans") return;
  const dests = (id) => (id ? placeLabel(id) : T("Disque par défaut"));
  $("#view").innerHTML = `<h1>${T("Tâches")}</h1>${taskTabs()}
    <div class="actions" style="margin:0 0 12px"><button class="primary" id="plan-new">${T("Nouveau plan")}</button>
      <select id="plan-preset"><option value="">${T("Partir d'un modèle…")}</option>${PLAN_PRESETS.map(([l], i) => `<option value="${i}">${esc(l)}</option>`).join("")}</select></div>
    <p class="hint">${T("Les nouveaux jeux sont pris automatiquement par les règles ; au-delà du nombre de versions gardées, les plus anciennes sont retirées (rotation ; les versions protégées restent).")}</p>
    <div class="list">${plans.length ? plans.map((p) => `<div class="row"><div><div class="t">${esc(p.name || T("Plan"))} ${p.enabled ? "" : `<span class="tag muted">${T("en pause")}</span>`}</div>
      <div class="chips"><span>${esc(planWhen(p))}</span><span>${esc(RULES[p.rule] ?? p.rule)}${p.rule === "" ? ` (${(p.items || []).length})` : ""}${p.rule_arg ? " · " + esc(p.rule_arg) : ""} · ${T("%d élément(s)", p.item_count ?? (p.items || []).length)}</span>
      <span>${esc((PLAN_PARTS.find((x) => JSON.stringify(x[2]) === JSON.stringify(p.parts || [])) || PLAN_PARTS[0])[1])}</span><span>${esc(dests(p.place))}</span>
      <span>${p.keep ? T("%d versions gardées", p.keep) : T("toutes les versions gardées")}</span>
      ${p.next_run ? `<span>${T("Prochaine : %s", new Date(p.next_run * 1000).toLocaleString(I18N.code))}</span>` : ""}${p.last_run ? `<span>${T("Dernière : %s", new Date(p.last_run * 1000).toLocaleString(I18N.code))} · ${esc(p.last_result)}</span>` : ""}</div></div>
      <div class="btns"><button data-run="${p.id}">${T("Lancer maintenant")}</button><button data-toggle="${p.id}">${p.enabled ? T("Mettre en pause") : T("Réactiver")}</button><button data-edit="${p.id}">${T("Modifier")}</button><button data-del="${p.id}">${T("Supprimer")}</button></div></div>`).join("")
      : `<div class="empty">${T("Aucun plan. Créez-en un pour sauvegarder automatiquement, par exemple vos sauvegardes PS5 tous les soirs.")}</div>`}</div>`;
  bindTaskTabs();
  $("#plan-new").onclick = () => planDialog(blankPlan());
  $("#plan-preset").onchange = (e) => { if (e.target.value !== "") planDialog(blankPlan({ parts: [], ...PLAN_PRESETS[+e.target.value][1] })); e.target.value = ""; };
  const act = (sel, fn) => $$(sel).forEach((b) => b.onclick = async () => { b.disabled = true; try { await fn(b); } catch (e) { toast(e.message); } b.disabled = false; });
  act("[data-run]", async (b) => { const r = await post(`/plans/${b.dataset.run}/run`); toast(T("%d sauvegarde(s) ajoutée(s) aux tâches", r.queued)); refresh(true); });
  act("[data-toggle]", async (b) => { const p = plans.find((x) => x.id === b.dataset.toggle); await post("/plans", { ...p, enabled: !p.enabled }); plansView(); });
  $$("[data-edit]").forEach((b) => b.onclick = () => planDialog(plans.find((x) => x.id === b.dataset.edit)));
  act("[data-del]", async (b) => { if (await ask(T("Supprimer ce plan ?"), `<p class="hint">${T("Les sauvegardes déjà faites restent.")}</p>`, T("Supprimer"))) { await post(`/plans/${b.dataset.del}/delete`); toast(T("Plan supprimé")); plansView(); } });
}
// Éditeur complet : nom, quand (à la demande, chaque jour, certains jours, une fois), heure, contenu (règle ou jeux choisis),
// parties, destination (disque ou NAS), versions gardées (rotation), actif ; Lancer maintenant / Supprimer pour un plan existant.
async function planDialog(p) {
  p = { ...blankPlan(), ...p, items: [...(p.items || [])], parts: [...(p.parts || [])] };
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog wide">${loading(T("Chargement…"))}</div>`;
  document.body.appendChild(box);
  let accounts = [];
  try { accounts = (await loadOverview()).accounts.filter((a) => a.on_console); } catch (e) {}
  const platforms = [...new Set(state.items.filter((i) => i.kind === "rom").map((i) => i.platform))].sort();
  const dests = state.places.filter((x) => x.type !== "switch" && x.use !== "sync");
  const partsKey = (PLAN_PARTS.find((x) => JSON.stringify(x[2]) === JSON.stringify(p.parts)) || PLAN_PARTS[0])[0];
  const chosen = new Set(p.items);
  box.innerHTML = `<div class="dialog wide"><h2>${p.id ? T("Modifier le plan") : T("Nouveau plan")}</h2>
    <label class="field-row">${T("Nom")}<input class="field" id="pl-name" value="${esc(p.name)}" placeholder="${esc(T("Sauvegardes du soir"))}"></label>
    <div class="two">
      <label class="field-row">${T("Quand")}<select id="pl-timing">${[["manual", T("À la demande")], ["daily", T("Chaque jour")], ["weekly", T("Certains jours")], ["once", T("Une seule fois")]].map(([k, l]) => `<option value="${k}" ${p.timing === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
      <label class="field-row" id="pl-time-row">${T("Heure")}<input class="field" type="time" id="pl-time" value="${hhmm(p.minute_of_day || 0)}"></label>
      <label class="field-row" id="pl-once-row">${T("Date")}<input class="field" type="datetime-local" id="pl-once" value="${p.once_at ? new Date(p.once_at * 1000 - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 16) : ""}"></label>
    </div>
    <div class="field-row" id="pl-days">${T("Jours")}<div class="pills days">${DAYS.map((d, i) => `<button type="button" data-day="${i}" class="${p.weekdays & (1 << i) ? "active" : ""}">${d}</button>`).join("")}</div></div>
    <div class="two">
      <label class="field-row">${T("Contenu")}<select id="pl-rule">${Object.entries(RULES).map(([k, l]) => `<option value="${k}" ${p.rule === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
      <label class="field-row" id="pl-arg-row">${T("Précision")}<select id="pl-arg"></select></label>
    </div>
    <div class="field-row" id="pl-items-row">${T("Jeux choisis")} <span class="hint" id="pl-items-count"></span>
      <input type="search" class="field" id="pl-items-q" placeholder="${esc(T("Filtrer…"))}"><div class="pick-list" id="pl-items"></div></div>
    <div class="two">
      <label class="field-row">${T("Sauvegarder")}<select id="pl-parts">${PLAN_PARTS.map(([k, l]) => `<option value="${k}" ${partsKey === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
      <label class="field-row">${T("Destination")}<select id="pl-place"><option value="">${T("Par défaut")}</option>${dests.map((d) => `<option value="${esc(d.id)}" ${p.place === d.id ? "selected" : ""}>${esc(d.label)}${d.remote || d.type === "ftp" ? " · " + T("réseau") : ""}</option>`).join("")}</select></label>
      <label class="field-row">${T("Versions gardées (rotation)")}<select id="pl-keep">${[...Array(31).keys()].map((n) => `<option value="${n}" ${(p.keep ?? 5) === n ? "selected" : ""}>${n ? (n > 1 ? T("%d versions", n) : T("1 version")) : T("Toutes")}</option>`).join("")}</select></label>
    </div>
    <label class="radio"><input type="checkbox" id="pl-enabled" ${p.enabled !== false ? "checked" : ""}><span class="grow">${T("Actif")} <span class="hint">${T("(décoché : en pause)")}</span></span></label>
    <div class="actions"><button class="primary" id="pl-save">${T("Enregistrer")}</button>${p.id ? `<button class="secondary" id="pl-run">${T("Lancer maintenant")}</button><button class="danger" id="pl-del">${T("Supprimer")}</button>` : ""}<button class="secondary" id="pl-cancel">${T("Annuler")}</button></div></div>`;
  let days = p.weekdays || 0;
  const drawItems = () => {
    const q = ($("#pl-items-q", box).value || "").toLowerCase();
    const list = state.items.filter((i) => !q || i.title.toLowerCase().includes(q) || chosen.has(i.key)).sort((a, b) => (chosen.has(b.key) - chosen.has(a.key)) || a.title.localeCompare(b.title)).slice(0, 200);
    $("#pl-items", box).innerHTML = list.map((i) => `<label class="pick-item"><input type="checkbox" data-it="${esc(i.key)}" ${chosen.has(i.key) ? "checked" : ""}><span class="grow">${esc(i.title)}</span><span class="hint">${esc(kindLabel(i))}</span></label>`).join("");
    $("#pl-items-count", box).textContent = "· " + T("%d coché(s)", chosen.size);
    $$("[data-it]", box).forEach((c) => c.onchange = () => { if (c.checked) chosen.add(c.dataset.it); else chosen.delete(c.dataset.it); $("#pl-items-count", box).textContent = "· " + T("%d coché(s)", chosen.size); });
  };
  const sync = () => {
    const rule = $("#pl-rule", box).value, timing = $("#pl-timing", box).value;
    const opts = rule === "ps5_saves" ? accounts.map((a) => [a.user_id, a.name]) : rule === "roms" ? [["", T("Toutes les plateformes")], ...platforms.map((x) => [x, x])] : [];
    $("#pl-arg-row", box).style.display = opts.length ? "" : "none";
    $("#pl-arg", box).innerHTML = opts.map(([v, l]) => `<option value="${esc(v)}" ${p.rule_arg === v ? "selected" : ""}>${esc(l)}</option>`).join("");
    $("#pl-items-row", box).style.display = rule === "" ? "" : "none";
    $("#pl-days", box).style.display = timing === "weekly" ? "" : "none";
    $("#pl-time-row", box).style.display = timing === "daily" || timing === "weekly" ? "" : "none";
    $("#pl-once-row", box).style.display = timing === "once" ? "" : "none";
    if (timing === "weekly" && !days) { days = 1 << 6; $$("[data-day]", box).forEach((b) => b.classList.toggle("active", !!(days & (1 << +b.dataset.day)))); }
  };
  $("#pl-rule", box).onchange = sync; $("#pl-timing", box).onchange = sync; sync(); drawItems();
  $("#pl-items-q", box).oninput = drawItems;
  $$("[data-day]", box).forEach((b) => b.onclick = () => { days ^= 1 << +b.dataset.day; b.classList.toggle("active"); });
  $("#pl-cancel", box).onclick = () => box.remove();
  const body = () => {
    const [h, m] = ($("#pl-time", box).value || "21:00").split(":").map(Number);
    const rule = $("#pl-rule", box).value;
    return { ...p, name: $("#pl-name", box).value.trim() || T("Plan"), rule, rule_arg: rule === "ps5_saves" || rule === "roms" ? $("#pl-arg", box).value || "" : "", timing: $("#pl-timing", box).value,
      minute_of_day: h * 60 + m, weekdays: days, once_at: $("#pl-once", box).value ? Math.floor(new Date($("#pl-once", box).value).getTime() / 1000) : 0,
      place: $("#pl-place", box).value, keep: +$("#pl-keep", box).value, enabled: $("#pl-enabled", box).checked,
      parts: PLAN_PARTS.find((x) => x[0] === $("#pl-parts", box).value)[2], items: rule === "" ? [...chosen] : p.items };
  };
  $("#pl-save", box).onclick = async () => {
    const b = body();
    if (b.rule === "" && !b.items.length) { toast(T("Cochez au moins un jeu, ou choisissez une règle.")); return; }
    try { await post("/plans", b); box.remove(); toast(T("Plan enregistré")); await loadPlans(); if (state.tab === "tasks") render(true); } catch (e) { toast(e.message); }
  };
  if (!p.id) return;
  $("#pl-run", box).onclick = async () => { try { const r = await post(`/plans/${p.id}/run`); toast(T("%d sauvegarde(s) ajoutée(s) aux tâches", r.queued)); refresh(true); } catch (e) { toast(e.message); } };
  $("#pl-del", box).onclick = async () => {
    if (!(await ask(T("Supprimer ce plan ?"), `<p class="hint">${T("Les sauvegardes déjà faites restent.")}</p>`, T("Supprimer")))) return;
    try { await post(`/plans/${p.id}/delete`); box.remove(); toast(T("Plan supprimé")); await loadPlans(); if (state.tab === "tasks") render(true); } catch (e) { toast(e.message); }
  };
}
// Depuis la fiche : les plans de ce jeu (cocher = le jeu est sauvegardé à chaque passage du plan).
async function itemPlans(it) {
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog">${loading(T("Lecture des plans…"))}</div>`;
  document.body.appendChild(box);
  const draw = () => {
    const plans = state.plans || [];
    box.innerHTML = `<div class="dialog"><h2>${T("Sauvegardes automatiques de ce jeu")}</h2><p class="hint">${T("Cochez : le jeu est sauvegardé à chaque passage du plan.")}</p>
      <div class="list">${plans.map((p, i) => `<label class="radio"><input type="checkbox" data-pp="${i}" ${(p.items || []).includes(it.key) ? "checked" : ""}><span class="grow"><b>${esc(p.name)}</b><br><span class="hint">${esc(planWhen(p))}</span></span>${p.rule ? `<span class="hint">${T("inclus par sa règle")}</span>` : ""}</label>`).join("") || `<p class="hint">${T("Aucun plan pour l'instant.")}</p>`}</div>
      <div class="actions"><button class="primary" id="pp-new">${T("+ Nouveau plan avec ce jeu")}</button><button class="secondary" id="pp-close">${T("Fermer")}</button></div></div>`;
    $("#pp-close", box).onclick = () => box.remove();
    $("#pp-new", box).onclick = () => { box.remove(); planDialog(blankPlan({ name: T("Plan") + " · " + cleanTitle(it.title).slice(0, 40), items: [it.key] })); };
    $$("[data-pp]", box).forEach((c) => c.onchange = async () => {
      const p = plans[+c.dataset.pp], items = (p.items || []).filter((k) => k !== it.key);
      if (c.checked) items.push(it.key);
      try { await post("/plans", { ...p, items }); p.items = items; toast(c.checked ? T("Ajouté au plan") : T("Retiré du plan")); } catch (e) { toast(e.message); c.checked = !c.checked; }
    });
  };
  await loadPlans();
  draw();
}

// ---- Paramètres -----------------------------------------------------------------------
async function settings() {
  // Même ordre que la télé ; disques, Switch, NAS et téléphones associés se règlent dans l'onglet Appareils seulement.
  const sections = { news: T("Nouveautés"), general: T("Général"), emulators: T("Sources et émulateurs"), ignored: T("Éléments ignorés"), accounts: T("Utilisateurs"),
    updates: T("Mises à jour et démarrage"), diagnostics: T("Diagnostic et à propos"), report: T("Signaler un problème"), goals: T("Projets à financer"), support: T("Soutenir le projet"), thanks: T("Remerciements") };
  if (!sections[state.settingsSection]) state.settingsSection = "general";
  const st = state.status || {};
  const devicesLink = `<div class="card link-card"><span class="dev-ico">${ICONS.usb}</span><span class="grow"><b>${T("Stockage et appareils")}</b><br><span class="hint">${T("Disque par défaut, disques, Switch, NAS et téléphones associés se règlent dans l'onglet Appareils.")}</span></span>
    <button class="secondary small" data-goto="devices">${T("Ouvrir Appareils")}</button></div>`;
  const body = {
    news: () => `<h2>${T("Nouveautés")}</h2><p class="sub">${T("Ce qui est nouveau, amélioré et corrigé dans chaque version.")}</p>
      <div class="card">${state.changelog ? changelogHtml(state.changelog) : `<p class="hint">${T("Chargement…")}</p>`}</div>`,
    general: () => { const autoLang = I18N.languages.find((l) => l.code === I18N.auto) || { name: "English" };
      return `<h2>${T("Langue")}</h2><p class="sub">${T("Langue de l'app télé et du web. Automatique : celle de la console.")}</p>
      <label class="radio"><input type="radio" name="lang" value="auto" ${I18N.setting === "auto" ? "checked" : ""}><span class="grow"><b>${esc(T("Automatique (%s)", langLabel(autoLang)))}</b></span></label>
      ${I18N.languages.map((l) => `<label class="radio"><input type="radio" name="lang" value="${esc(l.code)}" ${I18N.setting === l.code ? "checked" : ""}><span class="grow"><b lang="${esc(l.code)}" dir="${l.rtl ? "rtl" : "ltr"}">${esc(l.name)}</b></span><span class="hint">${esc(langOther(l))}</span></label>`).join("")}
      <h2>${T("Thème")}</h2><p class="sub">${T("Le même sur la télé et ici, appliqué tout de suite.")}</p><div class="themes">${THEMES.map(themeSwatch).join("")}</div>
      <h2>${T("Son sur la télé")}</h2><p class="sub">${T("Les sons sont joués par la console ; cette page ne joue rien, elle règle la télé.")}</p>
      <label class="radio"><input type="checkbox" id="snd-ui" ${state.settings.sound_ui !== false ? "checked" : ""}><span class="grow">${T("Sons de l'interface")} <span class="hint">(${T("déplacement, valider, retour, messages, fin d'une tâche")})</span></span></label>
      <label class="radio"><input type="checkbox" id="snd-music" ${state.settings.music ? "checked" : ""}><span class="grow">${T("Musique d'ambiance")}</span></label>
      <div class="kv">${T("Volume")}<b><input type="range" id="snd-vol" min="0" max="100" step="10" value="${state.settings.volume ?? 70}"> <span id="snd-vol-v">${state.settings.volume ?? 70} %</span></b></div>
      <h2>${T("Stockage et appareils")}</h2>${devicesLink}`; },
    emulators: () => `<h2>${T("Sources et émulateurs")}</h2><p class="sub">${T("Automatique : réglages de chaque émulateur, sinon ses dossiers habituels. « Régler » pour choisir à la main. Un nouvel émulateur apparaît au relevé suivant.")}</p>
      ${state.emulators.filter((e) => e.installed).map((e) => { const app = state.items.find((i) => (e.title_ids || []).includes(i.title_id) && i.has_cover) || { title: e.name, kind: "app", has_cover: false };
        const appInstalled = state.items.some((i) => (e.title_ids || []).includes(i.title_id));
        const m = e.manual || {};
        return `<div class="card"><h4 class="emu-head"><span class="emu-icon">${coverHtml(app, true)}</span><span class="grow">${esc(e.name)}${e.is_manual ? ` <span class="tag info">${T("réglé à la main")}</span>` : ""}${appInstalled ? "" : ` <span class="hint">· ${T("dossier trouvé, aucune application installée")}</span>`}</span><button class="secondary small" data-emu="${esc(e.id)}">${T("Régler…")}</button></h4>
          ${e.roots.length ? e.roots.map((r) => `<div class="kv">${esc(r.platform || "—")}<b>${esc(r.path)} <span class="hint">(${r.source === "config" ? T("réglages de l'émulateur") : r.source === "manual" ? T("choisi") : T("dossier par défaut")})</span></b></div>`).join("") : `<div class="kv">${T("Aucun dossier de jeux trouvé")}<b>—</b></div>`}
          ${(m.config || (e.config_files || [])[0]) ? `<div class="kv">${T("Réglages")}<b>${esc(m.config || e.config_files[0])} <span class="hint">(${m.config ? T("choisi") : T("par défaut")})</span></b></div>` : ""}
          ${m.saves ? `<div class="kv">${T("Sauvegardes")}<b>${esc(m.saves)} <span class="hint">(${T("choisi")})</span></b></div>` : ""}</div>`; }).join("")}`,
    // Comptes : chaque identifiant inscrit dans les sauvegardes (/players), avec son nom ; même écran que la télé.
    // Un formatage ou une autre console donne un nouvel identifiant : on le renomme, on le rattache au même joueur.
    accounts: () => {
      const pl = state.players || { players: [], others: [] };
      const day = (t) => (t > 0 ? new Date(t * 1000).toLocaleDateString(I18N.code) : "");
      const row = (a, mine) => {
        const tag = mine ? (a.current ? [T("Sur cette console"), "ok"] : [T("Absent de cette console"), "info"]) : a.suggest_player ? [T("Probablement vous"), "warn"] : [T("Absent de cette console"), "muted"];
        const bits = [a.account_id, T("%d copie(s)", a.images || 0), T("%d jeu(x)", a.games || 0)];
        if (a.first > 0) bits.push(`${day(a.first)} → ${day(a.last)}`);
        if (!mine && a.identical_games > 0) bits.push(T("même partie : %d jeu(x)", a.identical_games));
        return `<div class="row acc-row"><div class="grow"><div class="t">${esc(a.name || T("Sans nom"))}</div><div class="d">${esc(bits.join(" · "))}</div></div>
          ${tag ? `<span class="tag ${tag[1]}">${esc(tag[0])}</span>` : "<span></span>"}
          <span class="vbtns">${!a.current && pl.players[0] ? `<button class="small" data-acc-take="${esc(a.account_id)}">${esc(T("Tout reprendre sur %s…", pl.players[0].name))}</button>` : ""}<button class="secondary small" data-acc-name="${esc(a.account_id)}" data-cur="${esc(a.name || "")}">${T("Renommer…")}</button>
          ${!mine ? `<button class="secondary small" data-acc-link="${esc(a.account_id)}" data-on="1">${T("C'est moi (même utilisateur)")}</button>` : !a.current ? `<button class="secondary small" data-acc-link="${esc(a.account_id)}" data-on="0">${T("Ce n'est pas moi")}</button>` : ""}</span></div>`;
      };
      return `<h2>${T("Utilisateurs")}</h2><p class="sub">${T("Un formatage ou une autre console donne un nouvel identifiant : renommez-le, rattachez-le au même utilisateur. Ses copies se reprennent ensuite sur votre compte (Toutes les versions).")}</p>
        ${!state.players ? `<div class="loading small">${SPIN}${T("Lecture…")}</div>` : ""}
        ${pl.players.map((p) => `<div class="card"><h4>${esc(p.name)} <span class="hint">· ${T("PS5 · cette console")}</span></h4>${p.accounts.map((a) => row(a, true)).join("")}</div>`).join("")}
        ${pl.others.length ? `<div class="card"><h4>${T("Autres identifiants vus dans vos sauvegardes")}</h4>${pl.others.map((a) => row(a, false)).join("")}</div>` : ""}
        ${devicesLink}`;
    },
    ignored: () => {
      // Identifiants (ou clés) des réglages, plus les éléments signalés « ignored » par le moteur (ROM, homebrews par clé).
      const rows = [];
      for (const id of state.settings.ignored || []) {
        if (rows.some((r) => r.value === id)) continue;
        const it = state.items.find((i) => i.title_id === id) || state.items.find((i) => i.key === id);
        rows.push({ field: it && it.key === id && it.title_id !== id ? "key" : "title_id", value: id, it });
      }
      for (const it of state.items) if (it.ignored) { const v = it.title_id || it.key; if (!rows.some((r) => r.value === v)) rows.push({ field: it.title_id ? "title_id" : "key", value: v, it }); }
      return `<h2>${T("Éléments ignorés")}</h2><p class="sub">${T("Jeux, applications, homebrews et ROM qui ne sont plus sauvegardés ni proposés.")}</p>
      ${rows.length ? `<div class="list">${rows.map((r) => { const it = r.it || { key: "tid:ps5:" + r.value, title: r.value, kind: "game_ps5", platform: "PS5", has_cover: r.field === "title_id", apps: [] };
        return `<div class="row"><div class="thumb">${coverHtml(it, true)}</div><div><div class="t">${esc(it.title)}</div><div class="d">${esc(r.value)}</div></div><div class="btns"><button data-unignore="${esc(r.value)}" data-field="${r.field}" data-name="${esc(it.title)}">${T("Ne plus ignorer")}</button></div></div>`; }).join("")}</div>`
        : `<div class="empty">${T("Aucun élément ignoré")}</div>`}`; },
    updates: () => updatesView(st),
    thanks: () => thanksView(),
    report: () => reportView(),
    goals: () => goalsView(),
    support: () => supportView(),
    diagnostics: () => `<h2>${T("Diagnostic et à propos")}</h2>
      <div class="card"><div class="kv">${T("Moteur")}<b>${esc(st.version)}</b></div><div class="kv">${T("Éléments")}<b>${st.items}</b></div>
      <div class="kv">${T("Dernier relevé")}<b>${st.last_scan ? T("%d dossiers en %d ms", st.last_scan.dirs, st.last_scan.ms) : "—"}</b></div>
      <div class="kv">${T("Démarré")}<b>${st.started_at ? new Date(st.started_at * 1000).toLocaleString(I18N.code) : "—"}</b></div></div>`,
  };
  if (state.settingsSection === "accounts") { try { state.players = await api("/players"); } catch (e) { state.players = { players: [], others: [] }; } }
  if (state.settingsSection === "news" && !state.changelog) { try { state.changelog = await api("/changelog"); } catch (e) { state.changelog = []; } }
  if (state.settingsSection === "report" && !state.statusInfo) { try { state.statusInfo = await api("/status"); } catch (e) { state.statusInfo = {}; } }
  if ((state.settingsSection === "support" || state.settingsSection === "goals") && !state.support) { try { state.support = await api("/support"); } catch (e) { state.support = { links: [], crypto: [], supporters: [], error: e.message }; } }
  if (state.settingsSection === "thanks" && !state.credits) { try { state.credits = await (await fetch("/credits.json")).json(); } catch (e) { state.credits = { used: [], thanks: [] }; } }
  if (state.tab !== "settings") return;
  $("#view").innerHTML = `<h1>${T("Paramètres")}</h1><div class="settings">
    <nav class="menu">${Object.entries(sections).map(([k, l]) => `<button data-s="${k}" class="${state.settingsSection === k ? "active" : ""}">${l}</button>`).join("")}</nav>
    <div>${body[state.settingsSection]()}</div></div>`;
  document.querySelectorAll(".menu button").forEach((b) => b.onclick = () => { state.settingsSection = b.dataset.s; settings(); });
  document.querySelectorAll("[data-copy]").forEach((b) => b.onclick = async () => {
    const el = document.getElementById("sup-a" + b.dataset.copy);
    try { await navigator.clipboard.writeText(el.textContent); toast(T("Adresse copiée")); }
    catch (e) { const r = document.createRange(); r.selectNodeContents(el); const sel = getSelection(); sel.removeAllRanges(); sel.addRange(r); toast(T("Adresse sélectionnée : copiez-la")); }
  });
  document.querySelectorAll("input[name=lang]").forEach((r) => r.onchange = () => setLanguage(r.value));
  document.querySelectorAll("[data-theme-pick]").forEach((b) => b.onclick = async () => {
    applyTheme(b.dataset.themePick); settings();
    try { state.settings = await post("/settings", { theme: b.dataset.themePick }); } catch (e) { toast(e.message); }
  });
  // Son de la télé : enregistré dans le moteur, la télé le suit en quelques secondes.
  const sound = async (patch) => { try { state.settings = await post("/settings", patch); toast(T("Réglage enregistré")); } catch (e) { toast(e.message); } };
  if ($("#snd-ui")) {
    $("#snd-ui").onchange = (e) => sound({ sound_ui: e.target.checked });
    $("#snd-music").onchange = (e) => sound({ music: e.target.checked });
    $("#snd-vol").oninput = (e) => { $("#snd-vol-v").textContent = `${e.target.value} %`; };
    $("#snd-vol").onchange = (e) => sound({ volume: +e.target.value });
  }
  // Comptes : renommer un identifiant, le rattacher au joueur de la console (ou l'en détacher).
  $$("[data-acc-name]").forEach((b) => b.onclick = async () => {
    const ok = await ask(T("Nom de cet identifiant"), `<p class="hint">${esc(b.dataset.accName)}</p><input id="acc-name-in" maxlength="64" value="${esc(b.dataset.cur)}">`, T("Enregistrer"));
    if (!ok) return;
    try { state.players = await post("/players/name", { account_id: b.dataset.accName, name: ($("#acc-name-in") || {}).value || "" }); toast(T("Nom enregistré")); settings(); } catch (e) { toast(e.message); }
  });
  $$("[data-acc-take]").forEach((b) => b.onclick = () => takeover(b.dataset.accTake));
  $$("[data-acc-link]").forEach((b) => b.onclick = async () => {
    try {
      state.players = await post("/players/link", { account_id: b.dataset.accLink, on: b.dataset.on === "1" });
      toast(b.dataset.on === "1" ? T("Identifiant rattaché à cet utilisateur") : T("Identifiant détaché"));
      mg.sum = null; settings();
    } catch (e) { toast(e.message); }
  });
  $$("[data-unignore]").forEach((b) => b.onclick = async () => {
    b.disabled = true; b.innerHTML = SPIN;
    const id = b.dataset.unignore;
    try {
      await post("/ignore", { [b.dataset.field || "title_id"]: id, on: false });
      toast(T("« %s » n'est plus ignoré", b.dataset.name || id));
      refresh(true);
      state.settings = await api("/settings"); state.overview = null;
    } catch (e) { toast(e.message); }
    settings();
  });
  if (state.settingsSection === "updates") bindUpdates();
  $$("[data-emu]").forEach((b) => b.onclick = () => emulatorDialog(state.emulators.find((e) => e.id === b.dataset.emu)));
  $$("[data-goto]").forEach((b) => b.onclick = () => setTab(b.dataset.goto));
}

// Réglages d'un émulateur : dossiers de jeux, fichier de réglages, dossier des sauvegardes (choisis dans l'explorateur de la console),
// ou retour à la détection automatique.
function emulatorDialog(e) {
  const m = e.manual || {};
  const roms = (m.roms && m.roms.length ? m.roms : e.roots.map((r) => r.path)).slice();
  const configDefault = (e.config_files || [])[0] || "";
  let config = m.config || configDefault, saves = m.saves || "";
  const parent = (p) => { const i = (p || "").lastIndexOf("/"); return i <= 0 ? "/" : p.slice(0, i); };
  const box = document.createElement("div");
  box.className = "modal";
  document.body.appendChild(box);
  const draw = () => {
    box.innerHTML = `<div class="dialog wide"><h2>${esc(T("Régler %s", e.name))}</h2><p class="hint">${T("Vos choix remplacent la détection automatique. « Revenir à l'automatique » les efface.")}</p>
      ${roms.map((r, i) => `<div class="pick-row"><span class="lbl">${T("Dossier de jeux %d", i + 1)}</span><span class="mono grow">${esc(r)}</span><button class="secondary small" data-ch="${i}">${T("Changer")}</button><button class="secondary small" data-del-rom="${i}">${T("Retirer")}</button></div>`).join("")}
      <div class="pick-row"><span class="lbl">${T("+ Ajouter un dossier")}</span><span class="hint grow">${T("choisir sur la console")}</span><button class="secondary small" id="em-add">${T("Parcourir…")}</button></div>
      <div class="pick-row"><span class="lbl">${T("Fichier de réglages")}</span><span class="mono grow">${esc(config || "—")}</span><button class="secondary small" id="em-config">${T("Changer")}</button></div>
      <div class="pick-row"><span class="lbl">${T("Sauvegardes")}</span><span class="mono grow">${esc(saves || T("automatique"))}</span><button class="secondary small" id="em-saves">${T("Changer")}</button>${saves ? `<button class="secondary small" id="em-saves-auto">${T("Automatique")}</button>` : ""}</div>
      <div class="actions"><button class="primary" id="em-save">${T("Enregistrer")}</button><button class="secondary" id="em-reset">${T("Revenir à l'automatique")}</button><button class="secondary" id="em-close">${T("Fermer")}</button></div></div>`;
    $$("[data-ch]", box).forEach((b) => b.onclick = () => openBrowser({ title: T("Dossier de jeux"), start: roms[+b.dataset.ch], pick: (p) => { roms[+b.dataset.ch] = p; draw(); } }));
    $$("[data-del-rom]", box).forEach((b) => b.onclick = () => { roms.splice(+b.dataset.delRom, 1); draw(); });
    $("#em-add", box).onclick = () => openBrowser({ title: T("Ajouter un dossier de jeux"), start: roms.length ? parent(roms[roms.length - 1]) : "/mnt", pick: (p) => { roms.push(p); draw(); } });
    $("#em-config", box).onclick = () => openBrowser({ title: T("Fichier de réglages de l'émulateur"), start: config ? parent(config) : "/data", files: true, pick: (p) => { config = p; draw(); } });
    $("#em-saves", box).onclick = () => openBrowser({ title: T("Dossier des sauvegardes"), start: saves || "/data", pick: (p) => { saves = p; draw(); } });
    if ($("#em-saves-auto", box)) $("#em-saves-auto", box).onclick = () => { saves = ""; draw(); };
    $("#em-close", box).onclick = () => box.remove();
    $("#em-save", box).onclick = async () => {
      try { state.emulators = await post(`/emulators/${encodeURIComponent(e.id)}`, { roms, config: config === configDefault ? "" : config, saves }); toast(T("Réglages enregistrés : la collection est relue")); box.remove(); settings(); }
      catch (err) { toast(err.message); }
    };
    $("#em-reset", box).onclick = async () => {
      try { state.emulators = await post(`/emulators/${encodeURIComponent(e.id)}/reset`); toast(T("Retour à l'automatique : la collection est relue")); box.remove(); settings(); }
      catch (err) { toast(err.message); }
    };
  };
  draw();
}


// ---- Soutenir le projet : un code QR par moyen (dessiné à partir de la matrice du moteur), lien et copie ----
function qrSvg(rows) {
  if (!rows || !rows.length) return "";
  const n = rows.length, q = 4;
  let d = "";
  rows.forEach((r, y) => { for (let x = 0; x < r.length; x++) if (r[x] === "1") d += `M${x + q} ${y + q}h1v1h-1z`; });
  return `<svg viewBox="0 0 ${n + 2 * q} ${n + 2 * q}" shape-rendering="crispEdges" role="img"><rect width="100%" height="100%" fill="#fff"/><path d="${d}" fill="#000"/></svg>`;
}
function reportView() {
  const s = state.statusInfo || {}, repo = "https://github.com/consolecenterstudio/ps5-backup-center";
  const card = (url, title, text) => `<div class="card report-card"><b>${esc(title)}</b><p class="hint">${esc(text)}</p><a class="sup-btn" href="${url}" target="_blank" rel="noopener">${esc(T("Ouvrir"))}</a></div>`;
  return `<h2>${T("Signaler un problème")}</h2><p class="sub">${esc(T("Choisissez ce qui correspond : la page s'ouvre sur GitHub."))}</p>
    <div class="report-grid">${card(repo + "/issues/new/choose", T("Un bug"), T("Le formulaire de signalement. Recopiez les versions ci-dessous."))}
      ${card(repo + "/discussions", T("Une question, une idée"), T("Les Discussions : posez votre question ou proposez une idée."))}
      ${card(repo, T("★ Une étoile"), T("Le projet vous plaît : une étoile sur GitHub l'aide à être trouvé."))}</div>
    <div class="card"><div class="kv">${T("Version de l'appli")}<b>${esc(s.version || "—")}</b></div><div class="kv">${T("Système de la console")}<b>${esc(s.firmware || "—")}</b></div></div>`;
}
function goalsView() {
  const goals = (state.support && state.support.goals) || [];
  const card = (g) => { const pct = Math.min(100, Math.round(100 * (g.raised || 0) / Math.max(1, g.target)));
    return `<div class="card goal-card"><div class="goal-h"><b>${esc(g.name)}</b><span>$${(g.raised || 0).toLocaleString("en-US")} / $${g.target.toLocaleString("en-US")} · ${pct} %${g.supporters ? " · " + esc(T("%lld soutien(s)", g.supporters)) : ""}</span></div>
      <p class="hint accent">${esc(g.pitch || "")}</p><ul class="hint">${(g.features || []).map((f) => `<li>${esc(f)}</li>`).join("")}</ul>
      <div class="gauge"><i style="width:${pct}%"></i></div></div>`; };
  return `<h2>${T("Projets à financer")}</h2><p class="sub">${esc(T("Aidez à faire naître les prochaines applis. Pour contribuer : Soutenir le projet, en précisant le produit."))}</p>
    ${goals.length ? goals.map(card).join("") : `<p class="hint">${esc(T("Bientôt : PS5 Center et PS5 Connector, avec leurs objectifs et une jauge pour suivre la cagnotte."))}</p>`}`;
}
function supportView() {
  const s = state.support || { links: [], crypto: [], supporters: [] };
  const link = (l) => `<div class="card support-card"><div class="qr">${l.active && l.qr ? qrSvg(l.qr) : `<span class="soon">${esc(T("bientôt"))}</span>`}</div>
    <div class="grow"><b>${esc(T(l.name))}</b><div class="hint">${esc(l.detail ? T(l.detail) : "")}</div>${l.active && l.url ? `<a class="sup-btn" href="${esc(l.url)}" target="_blank" rel="noopener">${esc(T("Ouvrir"))}</a>` : ""}</div></div>`;
  const coin = (c, i) => `<div class="card support-card"><div class="qr">${qrSvg(c.qr)}</div>
    <div class="grow"><b>${esc(c.coin)}</b><div class="hint accent">${esc(c.network)}</div><code class="addr" id="sup-a${i}">${esc(c.address)}</code><button class="secondary small" data-copy="${i}">${esc(T("Copier l'adresse"))}</button></div></div>`;
  return `<h2>${T("Merci d'utiliser PS5 Backup Center")}</h2>
    <p class="sub">${esc(T("L'appli est gratuite et le restera : rien n'est bloqué, tout le monde a les mêmes fonctions et les mêmes mises à jour. Elle est faite par un développeur indépendant, sur son temps libre et ses propres consoles. Si elle a déjà sauvé une de vos parties, un petit coup de pouce aide à payer les consoles de test, les disques et les outils. Merci, vraiment."))}</p>
    <div class="support-grid">${(s.links || []).map(link).join("")}${(s.crypto || []).map(coin).join("")}</div>
    <p class="hint warn">${esc(T("Crypto : envoyez uniquement sur le réseau indiqué sous chaque code, sinon l'envoi est perdu."))}</p>
    <p><a class="sup-btn" href="https://github.com/consolecenterstudio/ps5-backup-center" target="_blank" rel="noopener">★ ${esc(T("Une étoile sur GitHub"))}</a></p>
    <h2>${T("Messages de la communauté")}</h2>
    <div class="community"><div class="grow">${(s.supporters || []).length ? (s.supporters || []).map((w) => `<div class="kv msg"><span>${esc(w.name)}${w.backer ? ` <span class="tag ok">${esc(T("a soutenu le projet"))}</span>` : ""}</span><b>${esc(w.message ? "« " + w.message + " »" : "")}${w.date ? ` <span class="hint">${esc(w.date)}</span>` : ""}</b></div>`).join("") : `<p class="hint">${esc(T("Les premiers messages apparaîtront ici. Laissez le vôtre : scannez le code."))}</p>`}</div>
    ${s.community ? `<a class="sup-btn" href="${esc(s.community.messages)}" target="_blank" rel="noopener">${esc(T("Laisser un message"))}</a>` : ""}</div>
    ${(s.follow || []).length ? `<h2>${T("Suivez-nous")}</h2><div class="follow">${s.follow.map((f) => f.active
      ? `<a class="follow-chip" href="${esc(f.url)}" target="_blank" rel="noopener" aria-label="${esc(T("Suivez-nous sur %s", f.name))}">${ICONS[f.id]}<span><b>${esc(f.name)}</b><small>${esc(f.handle)}</small></span></a>`
      : `<span class="follow-chip soon">${ICONS[f.id]}<span><b>${esc(f.name)}</b><small>${esc(T("bientôt"))}</small></span></span>`).join("")}</div>` : ""}`;
}

// ---- Remerciements : composants utilisés (licence, source) et projets à qui l'on doit beaucoup ----
function thanksView() {
  const c = state.credits || { used: [], thanks: [] };
  const link = (x) => x.url && x.url.startsWith("http") ? `<a href="${esc(x.url)}" target="_blank" rel="noopener">${esc(x.url)}</a>` : "";
  return `<h2>${T("Remerciements")}</h2><p class="sub">${esc(c.license || "")}</p>
    <h2>${T("Composants utilisés")}</h2>${c.used.map((x) => `<div class="card"><h4>${esc(x.name)}</h4>
      <div class="kv">${T("Auteur")}<b>${esc(x.author || "—")}</b></div><div class="kv">${T("Licence")}<b>${esc(x.license || "—")}</b></div>
      <div class="kv">${T("Rôle")}<b>${esc(x.role || "")}</b></div>${x.url ? `<div class="kv">${T("Source")}<b>${link(x)}</b></div>` : ""}</div>`).join("")}
    <h2>${T("Merci aussi")}</h2>${c.thanks.map((x) => `<div class="card"><h4>${esc(x.name)}${x.author ? ` <span class="hint">· ${esc(x.author)}</span>` : ""}</h4>
      <p class="hint">${esc(x.role || "")}</p>${link(x) ? `<p class="hint">${link(x)}</p>` : ""}</div>`).join("")}`;
}

// ---- Mises à jour (façon Orbit : versions, recherche, installation, redémarrage avec compte à rebours) ----
const UPDATE_STATES = { idle: T("Pas encore recherché"), checking: T("Recherche en cours…"), up_to_date: T("À jour"), available: T("Mise à jour disponible"),
  downloading: T("Téléchargement…"), installing: T("Installation…"), installed: T("Installée"), error: T("Échec") };
function updatesView(st) {
  const u = st.update || {}, s = state.settings || {};
  const pct = u.bytes_total ? Math.round((u.bytes_done / u.bytes_total) * 100) : 0;
  const busy = ["checking", "downloading", "installing"].includes(u.state);
  const countdown = state.restartAt ? Math.max(0, Math.ceil((state.restartAt - Date.now()) / 1000)) : 0;
  return `<h2>${T("Mises à jour")}</h2><p class="sub">${T("Deux parties : l'application télé et le moteur de sauvegarde, qui continue de tourner quand vous la fermez. Une sauvegarde en cours reprend toute seule après le redémarrage du moteur.")}</p>
    <div class="card"><h4>${T("Versions")}</h4>
      <div class="kv">${T("Moteur en cours")}<b>${esc(st.version || "—")}</b></div>
      <div class="kv">${T("Installée sur la console")}<b>${esc(u.installed || "—")}</b></div>
      <div class="kv">${T("Dernière version")}<b>${esc(u.available || "—")}${u.build ? ` <span class="hint">${esc(u.build)}</span>` : ""}</b></div>
      <div class="kv">${T("État")}<b>${esc(u.engine_restart_needed && u.installed ? T("Installée, active au redémarrage du moteur") : UPDATE_STATES[u.state] || u.state || "—")}${u.checked_at ? ` <span class="hint">· ${esc(T("recherché le %s", new Date(u.checked_at * 1000).toLocaleString(I18N.code)))}</span>` : ""}</b></div>
      ${u.message ? `<p class="hint">${esc(u.message)}</p>` : ""}
      ${u.state === "downloading" ? `<div class="bar"><span style="width:${pct}%"></span></div><p class="hint">${esc(updateProgress(u))}</p>` : ""}
      ${u.state === "available" ? `<p class="hint">${T("%d fichier(s) à télécharger, %s.", u.files, size(u.bytes_total))}</p>` : ""}
      ${(u.changelog || []).length && ["available", "installed"].includes(u.state) ? `<h4 style="margin-top:16px">${T("Ce qu'apporte cette mise à jour")}</h4>${changelogHtml(u.changelog)}`
        : (u.notes || []).length && ["available", "installed"].includes(u.state) ? `<ul class="notes">${u.notes.map((n) => `<li>${esc(n)}</li>`).join("")}</ul>` : ""}
      <div class="actions">
        <button class="secondary" data-u="check" ${busy || !s.update_source ? "disabled" : ""}>${T("Rechercher des mises à jour")}</button>
        ${u.state === "available" ? `<button data-u="install">${T("Installer la mise à jour")}</button>` : ""}
        ${u.engine_restart_needed && !state.restartAt ? `<button data-u="restart">${T("Redémarrer le moteur")}</button>` : ""}
        ${state.restartAt ? `<span class="hint">${T("Redémarrage du moteur dans %d s", countdown)}</span><button class="secondary" data-u="cancel">${T("Annuler")}</button>` : ""}
        ${u.previous && !busy ? `<button class="secondary" data-u="rollback">${T("Revenir à la version %s", u.previous)}</button>` : ""}
        <button class="secondary" data-u="relaunch" title="${esc(T("L'application télé se ferme et se rouvre toute seule"))}">${T("Redémarrer l'application télé")}</button>
      </div>
      ${u.state === "installed" && u.app_changed ? `<p class="hint">${T("L'application télé a changé : sur la télé, Paramètres → Mises à jour → « Redémarrer l'application ».")}</p>` : ""}
    </div>
    <div class="card" id="launch-card"><h4>${T("Démarrage du moteur")}</h4><p class="hint">${T("Chargement…")}</p></div>
    <div class="card" id="versions-card"><h4>${T("Versions disponibles")}</h4>
      <p class="hint">${T("choisir une version à installer")}</p>
      <div class="btns"><button class="secondary" id="v-load">${T("Afficher la liste")}</button></div><div id="v-list"></div>
    </div>
    <div class="card"><h4>${T("Alimentation de la console")}</h4>
      <p class="hint">${T("Refusé pendant une partie ou une copie en cours.")}</p>
      <div class="btns"><button class="secondary" data-power="reboot">${T("Redémarrer la console")}</button><button class="secondary" data-power="off">${T("Éteindre la console")}</button></div>
    </div>
    <div class="card"><h4>${T("Historique des versions")}</h4><div id="history">${state.changelog ? changelogHtml(state.changelog) : `<p class="hint">${T("Chargement…")}</p>`}</div></div>
    <div class="card"><h4>${T("Source des mises à jour")}</h4>
      <p class="hint">${T("Une adresse http:// (par exemple http://192.168.1.10:8765/pbc/manifest.json) ou un dossier de la console ou d'une clé USB (par exemple /mnt/usb0/PBC-Update/manifest.json), pour mettre à jour sans réseau.")}</p>
      <div class="actions"><input class="field grow" id="u-source" value="${esc(s.update_source || "")}" placeholder="http://…/pbc/manifest.json" style="flex:1;min-width:260px">
        <button class="secondary" data-u="save">${T("Enregistrer")}</button></div>
      <label class="radio"><input type="checkbox" id="u-auto" ${s.update_auto_check !== false ? "checked" : ""}><span class="grow">${T("Rechercher automatiquement")}<br><span class="hint">${T("Au démarrage, à l'ouverture de l'appli, dans Paramètres et toutes les heures.")}</span></span></label>
      <label class="radio"><input type="checkbox" id="u-autoinst" ${s.update_auto_install ? "checked" : ""}><span class="grow">${T("Installer automatiquement (jamais pendant une partie)")}<br><span class="hint">${T("La mise à jour trouvée s'installe seule, puis le moteur redémarre quand aucune copie n'est en cours.")}</span></span></label>
      <label class="radio"><span class="grow">${T("Canal des mises à jour")}<br><span class="hint">${T("Les bêtas arrivent avant les versions stables, pour les essayer. Elles peuvent contenir des défauts ; « Revenir à la version d'avant » reste possible. Repassez en Stable à tout moment.")}</span></span><select id="u-channel"><option value="stable" ${s.update_channel !== "beta" ? "selected" : ""}>${T("Stable (recommandé)")}</option><option value="beta" ${s.update_channel === "beta" ? "selected" : ""}>${T("Bêta")}</option></select></label>
    </div>`;
}
// Nouveautés à la façon d'Orbit : par version, un titre puis Nouveautés / Améliorations / Corrections.
function changelogHtml(versions) {
  const part = (label, cls, list) => (list || []).length ? `<div class="cl-part"><span class="cl-tag ${cls}">${label}</span><ul>${list.map((x) => `<li>${esc(T(x))}</li>`).join("")}</ul></div>` : "";
  return versions.map((v) => `<div class="cl-version"><div class="cl-head"><b>${esc(T("Version %s", v.version))}</b><span class="hint">${esc(v.date ? new Date(v.date + "T12:00:00").toLocaleDateString(I18N.code) : "")}</span></div>
    ${v.title ? `<p class="cl-title">${esc(T(v.title))}</p>` : ""}${part(T("Nouveautés"), "new", v.new)}${part(T("Améliorations"), "imp", v.improved)}${part(T("Corrections"), "fix", v.fixed)}</div>`).join("");
}
// Une fois après une mise à jour : « Quoi de neuf dans la version X ».
async function whatsNew() {
  let list = [];
  try { list = await api("/changelog"); } catch (e) { return; }
  if (!list.length) return;
  let seen = "";
  try { seen = localStorage.getItem("pbc-seen-version") || ""; } catch (e) {}
  const latest = list[0].version;
  try { localStorage.setItem("pbc-seen-version", latest); } catch (e) {}
  if (!seen || seen === latest) return;  // première visite : pas de fenêtre
  const box = document.createElement("div");
  box.className = "modal";
  box.innerHTML = `<div class="dialog"><h2>${T("Quoi de neuf")}</h2>${changelogHtml(list.filter((v) => v.version !== seen).slice(0, 3))}<p class="hint"><span style="color:#ff5e5b">♥</span> ${esc(T("PS5 Backup Center reste gratuit. Si l'appli vous aide, vous pouvez soutenir son développement : Paramètres › Soutenir le projet."))}</p><div class="actions"><button class="primary" id="wn-ok">${T("Compris")}</button></div></div>`;
  document.body.appendChild(box);
  $("#wn-ok").onclick = () => box.remove();
}

// Démarrage du moteur (comme la télé) : chargeurs présents, dans l'ordre choisi, et « Démarrer avec la console ».
async function loadLaunchCard() {
  const box = $("#launch-card");
  if (!box) return;
  let st;
  try { st = await api("/launch"); } catch (e) { box.innerHTML = `<h4>${T("Démarrage du moteur")}</h4><p class="hint">${esc(e.message)}</p>`; return; }
  const pm = st.pldmgr || {}, ls = st.launchers || [];
  const ld = ls.find((l) => l.id === "elfldr") || {};
  box.innerHTML = `<h4>${T("Démarrage du moteur")}</h4>
    <p class="hint">${esc(T("À l'ouverture : si le moteur ne tourne pas quand vous ouvrez Backup Center, l'appli le démarre toute seule. Avec la console : Payload Manager lance le moteur dès l'allumage, même sans ouvrir l'appli. Une fois lancé, le moteur se relance et se met à jour seul."))}</p>
    <label class="radio"><input type="checkbox" id="l-open" ${ld.enabled ? "checked" : ""}><span class="grow">${esc(T("Démarrer le moteur à l'ouverture de l'appli : %s", ld.enabled ? T("Oui") : T("Non")))}<br><span class="hint">${esc(ld.present ? T("Disponible sur cette console.") : T("Indisponible sur cette console pour le moment."))}</span></span></label>
    ${pm.present ? `<label class="radio"><input type="checkbox" id="l-pm" ${pm.enabled ? "checked" : ""}><span class="grow">${esc(T("Démarrer le moteur avec la console (Payload Manager) : %s", pm.enabled ? T("Oui") : T("Non")))}</span></label>
      ${pm.enabled && !pm.autoload_enabled ? `<p class="hint warn">${esc(T("Ajouté. Activez aussi le démarrage automatique dans Payload Manager."))}</p>` : ""}`
      : `<p class="hint">${esc(T("Démarrer le moteur avec la console : Payload Manager absent"))}</p>`}`;
  const openBox = box.querySelector("#l-open");
  if (openBox) openBox.onchange = async () => {
    try { state.settings = await post("/settings", { launchers: openBox.checked ? ["elfldr"] : [] }); toast(T("Réglage enregistré")); } catch (e) { toast(e.message); }
    loadLaunchCard();
  };
  const pmBox = box.querySelector("#l-pm");
  if (pmBox) pmBox.onchange = async () => {
    try {
      const r = await post("/launch/autostart", { enabled: pmBox.checked });
      toast(!pmBox.checked ? T("Retiré du démarrage de Payload Manager") : r.pldmgr && !r.pldmgr.autoload_enabled ? T("Ajouté. Activez aussi le démarrage automatique dans Payload Manager.") : T("Ajouté au démarrage de Payload Manager"));
    } catch (e) { toast(e.message); }
    loadLaunchCard();
  };
}
function bindUpdates() {
  loadLaunchCard();
  const vload = $("#v-load");
  if (vload) vload.onclick = async () => {
    const box = $("#v-list");
    try {
      const j = await api("/update/versions");
      const inst = j.installed, rec = j.recommended;
      box.innerHTML = `<div class="list">${(j.versions || []).slice(0, 10).map((v) => {
        const tag = v.version === inst ? T("installée") : v.version === rec ? T("recommandée") : v.channel === "beta" ? T("bêta") : !v.tested ? T("test") : "";
        return `<div class="row"><div class="grow"><div class="t">${esc(v.version)}${tag ? ` · <span class="hint">${esc(tag)}</span>` : ""}</div><div class="d">${esc(v.date ? new Date(v.date + "T12:00:00").toLocaleDateString(I18N.code) : "")} · ${esc(T(v.title || ""))}</div></div>
          <div class="btns">${v.version === inst ? "" : `<button data-ver="${esc(v.version)}">${T("Installer")}</button>`}</div></div>`; }).join("") || `<p class="hint">${T("Aucune version dans la liste")}</p>`}</div>`;
      box.querySelectorAll("[data-ver]").forEach((b) => b.onclick = async () => {
        const ver = b.dataset.ver, older = ver.localeCompare(inst, undefined, { numeric: true }) < 0;
        const note = older ? T("Version plus ancienne : la version %s ne sera plus proposée automatiquement ; une recherche à la main la montre toujours.", inst)
                           : T("Fichiers vérifiés, retour arrière en cas d'échec ; le moteur redémarre ensuite.");
        if (!await ask(T("Installer la version %s ?", ver), `<p class="hint">${esc(note)}</p>`, T("Installer"))) return;
        try { await post("/update/version", { version: ver }); toast(T("Installation de la version %s…", ver)); refresh(true); } catch (e) { toast(e.message); }
      });
    } catch (e) { box.innerHTML = `<p class="hint">${esc(e.message)}</p>`; }
  };
  const powerTitles = { reboot: T("Redémarrer la console ?"), rest: T("Mettre la console en mode repos ?"), off: T("Éteindre la console ?") };
  document.querySelectorAll("[data-power]").forEach((b) => b.onclick = async () => {
    if (!await ask(powerTitles[b.dataset.power], `<p class="hint">${esc(T("Refusé pendant une partie ou une copie en cours."))}</p>`, T("Confirmer"))) return;
    try { await post("/power", { action: b.dataset.power }); toast(T("Dans un instant…")); } catch (e) { toast(e.message); }
  });
  if (!state.changelog) api("/changelog").then((c) => { state.changelog = c; const h = $("#history"); if (h) h.innerHTML = changelogHtml(c); }).catch(() => {});
  document.querySelectorAll("[data-u]").forEach((b) => b.onclick = async () => {
    const a = b.dataset.u, u0 = (state.status || {}).update || {};
    try {
      if (a === "check") await post("/update/check");
      else if (a === "install") await post("/update/install");
      else if (a === "restart") { state.restartAt = Date.now() + 10000; tickRestart(); }
      else if (a === "cancel") { state.restartAt = 0; }
      else if (a === "rollback") {
        if (!await ask(T("Revenir à la version %s ?", u0.previous), `<p class="hint">${esc(T("La version d'avant est réinstallée (fichiers vérifiés), puis le moteur redémarre. La version actuelle ne sera plus proposée automatiquement ; une recherche à la main la montre toujours."))}</p>`, T("Revenir"))) return;
        await post("/update/rollback"); toast(T("Retour à la version %s…", u0.previous));
      }
      else if (a === "relaunch") { if (await ask(T("Redémarrer l'application télé ?"), `<p class="hint">${T("Elle se ferme et se rouvre toute seule dans 3 secondes. Les sauvegardes en cours continuent.")}</p>`, T("Redémarrer"))) { await post("/update/relaunch-app", { delay: 3 }); toast(T("L'application télé redémarre…")); } }
      else if (a === "save") { state.settings = await post("/settings", { update_source: $("#u-source").value.trim() }); toast(T("Source enregistrée")); }
      refresh(true); settings();
    } catch (e) { toast(e.message); }
  });
  const auto = $("#u-auto");
  if (auto) auto.onchange = async () => { state.settings = await post("/settings", { update_auto_check: auto.checked }); toast(T("Réglage enregistré")); };
  const autoInst = $("#u-autoinst");
  if (autoInst) autoInst.onchange = async () => { state.settings = await post("/settings", { update_auto_install: autoInst.checked }); toast(T("Réglage enregistré")); };
  const channel = $("#u-channel");
  if (channel) channel.onchange = async () => {
    if (channel.value === "beta" && !(await ask(T("Recevoir les versions bêta ?"), `<p class="hint">${esc(T("Les bêtas arrivent avant les versions stables, pour les essayer. Elles peuvent contenir des défauts ; « Revenir à la version d'avant » reste possible. Repassez en Stable à tout moment."))}</p>`, T("Passer en bêta")))) { channel.value = "stable"; return; }
    state.settings = await post("/settings", { update_channel: channel.value }); toast(T("Réglage enregistré"));
  };
}
function tickRestart() {
  if (!state.restartAt) return;
  if (Date.now() >= state.restartAt) {
    state.restartAt = 0;
    post("/update/restart-engine").then(() => toast(T("Le moteur redémarre…"))).catch((e) => toast(e.message));
    return;
  }
  if (state.tab === "settings" && state.settingsSection === "updates") settings();
  setTimeout(tickRestart, 1000);
}


// ---- Transferts : d'un appareil à l'autre (PS5, disques, Switch, NAS) -----------------------------
// Comme la télé : DEPUIS / VERS / QUE FAIRE ?, puis le choix des éléments et des options, le suivi,
// et la sauvegarde complète de la console. Un jeu en cours d'utilisation n'est jamais touché.
const TR_ACTIONS = [[T("Copier"), T("L'original reste. Vérifié fichier par fichier (empreinte).")], [T("Déplacer"), T("Copie vérifiée, puis l'original est supprimé. Libère de la place.")],
  [T("Compresser"), T("Jeu PS5 en image .ffpfsc : souvent 30 à 40 % de place en moins.")], [T("Synchroniser"), T("Sauvegardes : garder la plus récente des deux côtés.")]];
const TR_CATS = [["game_ps5", T("Jeux PS5")], ["game_ps4", T("Jeux PS4")], ["app", T("Applications")], ["rom", "ROM"]];
const FULL_CATS = [["accounts", T("Comptes"), T("utilisateurs, identifiant de compte, activation hors ligne"), false], ["saves", T("Sauvegardes de jeux"), T("images chiffrées + versions lisibles"), false],
  ["games", T("Jeux"), T("leurs inscriptions (les images restent sur vos disques)"), false], ["images", T("Images des jeux"), T(".ffpfsc · seulement celles qui ne sont pas déjà sur un disque"), false],
  ["homebrew", T("Applications homebrew et leurs données"), T("émulateurs, profils, thèmes, BIOS (/data)"), false], ["settings", T("Démarrage et réglages"), T("payloads au démarrage, shadowmount, kstuff"), false],
  ["captures", T("Captures et vidéos"), T("photos, clips"), false], ["system", T("Système (expert)"), T("registre, bases des applis · restauration guidée seulement"), true]];
const activeState = (s) => ["queued", "running", "paused"].includes(s);
const itemBytes = (it, saves) => it.components.reduce((a, c) => a + (saves || c.part !== "save" ? c.bytes : 0), 0);

function trDevices() {
  const disks = state.places.filter((p) => p.type === "usb" || p.type === "internal");
  const v = [{ id: "ps5", name: T("Cette PS5"), sub: disks.length ? disks.map((p) => p.type === "internal" ? T("Interne") : p.label).join(" · ") : T("tous ses disques"),
    kind: "ps5", icon: "ps5", free: disks.reduce((a, p) => a + p.free, 0), hasFree: false }];
  for (const p of disks) v.push({ id: p.id, name: p.type === "internal" ? T("Stockage interne") : p.label, sub: p.type === "internal" ? "PS5 · " + T("interne") : T("Disque USB") + " · " + p.mount,
    kind: p.type, icon: p.type === "usb" ? "usb" : "ps5", free: p.free, hasFree: true });
  for (const r of state.remotes) v.push({ id: r.id, name: r.name, sub: (r.type === "switch" ? "Switch" : T("Réseau")) + " · " + ((r.config && r.config.host) || ""),
    kind: r.type, icon: r.type === "switch" ? "switch" : "nas", free: 0, hasFree: false });
  // Autres PS5 jumelées : chacun de leurs disques est une destination (sauvegarde vérifiée, comme vers le NAS).
  for (const p of state.places.filter((x) => x.type === "peer")) v.push({ id: p.id, name: p.label, sub: T("Autre PS5 · %s libres", size(p.free)), kind: "peer", icon: "ps5", free: p.free, hasFree: true });
  return v;
}
// Dernière carte des rangées DEPUIS / VERS : ajouter un NAS sans quitter Transferts (il apparaît ensuite ici).
const addNasCard = `<button class="tr-dev tr-add" data-add-nas><span class="dev-ico">${ICONS.nas}</span><span class="grow"><b>${T("Ajouter un NAS")}</b><span class="hint">${T("Synology, QNAP, FTP…")}</span></span></button>`;
function trFromTo() {
  const devs = trDevices(), tr = state.tr;
  let from = devs.find((d) => d.id === tr.from) || devs[0];
  tr.from = from.id;
  let to = devs.find((d) => d.id === tr.to && d.id !== from.id);
  if (!to) { to = devs.find((d) => d.id !== from.id && d.kind !== "switch" && d.id !== "ps5") || devs.find((d) => d.id !== from.id) || from; tr.to = to.id; }
  return { devs, from, to };
}
function trItems(from) {
  const tr = state.tr;
  if (!["ps5", "usb", "internal"].includes(from.kind)) return [];
  const kind = tr.action === 2 ? "game_ps5" : TR_CATS[tr.cat][0];  // Compresser : jeux PS5 seulement
  return state.items.filter((it) => it.kind === kind && (from.kind === "ps5" || it.place === from.id)).sort((a, b) => a.title.localeCompare(b.title));
}
const devCard = (d, { chosen = false, disabled = false, attr = "" } = {}) => `<button class="tr-dev ${chosen ? "chosen" : ""}" ${disabled ? "disabled" : ""} ${attr}>
  <span class="dev-ico">${ICONS[d.icon]}</span><span class="grow"><b>${esc(d.name)}</b><span class="hint">${esc(d.sub)}</span>
  ${d.hasFree || d.free ? `<span class="hint ${chosen ? "accent" : ""}">${T("%s libres", size(d.free))}</span>` : ""}</span></button>`;

function transfers() {
  if (!state.overview) loadOverview();
  if (!state.images || Date.now() - state.imagesAt > 20000) loadImages().then(() => { if (state.tab === "transfers" && state.tr.screen === "choice") render(); });
  ({ main: trMain, choice: trChoice, progress: trProgress, full: trFull, sync: trSync, files: trFiles })[state.tr.screen]();
}
function trBack(to = "main") { state.tr.screen = to; window.scrollTo(0, 0); render(true); }

// ---- Transferts › Synchroniser : synchros habituelles, puis « Nouvelle synchro » (côté A, côté B) ----------
// Côtés possibles : une Switch, Eden / ProsperoEden, un NAS, un compte PS5, un disque USB. Au début, seules les paires
// déjà prises en charge par le moteur sont actives (Switch ⇄ Eden / ProsperoEden) ; les autres sont « bientôt ».
function openSync(a = "") {
  const tr = state.tr;
  if (a) { tr.syncA = a; if (tr.syncB === a) tr.syncB = ""; if (!syncSupported(a, tr.syncB || "")) tr.syncB = ""; }
  tr.screen = "sync"; tr.action = 3;
  if (state.tab === "transfers") { window.scrollTo(0, 0); render(true); } else setTab("transfers");
}
function syncSides() {
  const out = [];
  for (const r of state.remotes.filter((x) => x.type === "switch")) out.push({ spec: "switch:" + r.id, name: r.name, sub: "Switch", icon: ICONS.switch, ok: true });
  for (const root of edenRoots()) out.push({ spec: "eden:" + root, name: edenName(root), sub: T("Émulateur Switch sur la PS5"), icon: ICONS.ps5, ok: true });
  for (const r of state.remotes.filter((x) => x.type === "ftp" || x.type === "smb" || x.type === "webdav")) out.push({ spec: "nas:" + r.id, name: r.name, sub: "NAS", icon: ICONS.nas, ok: false });
  for (const a of consoleAccounts()) out.push({ spec: "ps5:" + a.user_id, name: a.name, sub: T("Compte PS5"), icon: ICONS.ps5, ok: false });
  for (const p of state.places.filter((x) => x.type === "usb")) out.push({ spec: "usb:" + p.id, name: p.label, sub: T("Disque USB"), icon: ICONS.usb, ok: false });
  return out;
}
const syncSupported = (a, b) => (a.startsWith("switch:") && b.startsWith("eden:")) || (a.startsWith("eden:") && b.startsWith("switch:"));
const syncIsDiff = (e) => ["a_newer", "b_newer", "same_day"].includes(e.status);
// Synchros habituelles : chaque Switch ⇄ chaque Eden, plus celles gardées dans ce navigateur.
function syncPairs() {
  let kept = [];
  try { kept = JSON.parse(localStorage.getItem("pbc-sync-pairs") || "[]"); } catch (e) {}
  const keys = [];
  for (const r of state.remotes.filter((x) => x.type === "switch")) for (const root of edenRoots()) keys.push(`switch:${r.id}|eden:${root}`);
  for (const k of Array.isArray(kept) ? kept : []) { const [a, b] = k.split("|"); if (!keys.includes(k) && !keys.includes(b + "|" + a)) keys.push(k); }
  const sides = syncSides();
  return keys.map((k) => k.split("|")).filter(([a, b]) => sides.some((s) => s.spec === a) && sides.some((s) => s.spec === b));
}
function keepPair(a, b) {
  let kept = [];
  try { kept = JSON.parse(localStorage.getItem("pbc-sync-pairs") || "[]"); } catch (e) {}
  if (!Array.isArray(kept)) kept = [];
  if (!kept.includes(a + "|" + b) && !kept.includes(b + "|" + a)) kept.push(a + "|" + b);
  try { localStorage.setItem("pbc-sync-pairs", JSON.stringify(kept)); } catch (e) {}
}
function trSync() {
  const tr = state.tr;
  if (!state.edenProfiles && !loadEden.p) loadEden().then(() => { if (state.tab === "transfers" && tr.screen === "sync") trSync(); });
  const sides = syncSides(), pairs = syncPairs();
  if (tr.syncA && !sides.some((s) => s.spec === tr.syncA)) tr.syncA = "";
  if (tr.syncB && !sides.some((s) => s.spec === tr.syncB)) tr.syncB = "";
  if (tr.syncA && !tr.syncB) { const b = sides.find((s) => s.ok && syncSupported(tr.syncA, s.spec)); if (b) tr.syncB = b.spec; }  // l'autre côté le plus probable
  const A = sides.find((s) => s.spec === tr.syncA), B = sides.find((s) => s.spec === tr.syncB);
  const ready = !!(A && B && A.spec !== B.spec), supported = ready && syncSupported(A.spec, B.spec);
  const cache = state.syncCache || {};
  const pairState = (a, b) => {
    const c = cache[a + "|" + b] || cache[b + "|" + a];
    if (!c) return [T("Jamais comparées"), "muted"];
    const n = c.data.entries.filter(syncIsDiff).length;
    return n ? [T("%d jeu(x) différent(s)", n), "warn"] : [T("Tout est pareil"), "ok"];
  };
  const side = (spec) => sides.find((s) => s.spec === spec);
  const sideCard = (s, which) => {
    const mine = which === "a" ? tr.syncA : tr.syncB, other = which === "a" ? tr.syncB : tr.syncA;
    return `<button class="tr-dev ${mine === s.spec ? "chosen" : ""}" data-side-${which}="${esc(s.spec)}" ${s.ok && s.spec !== other ? "" : "disabled"}>
      <span class="dev-ico">${s.icon}</span><span class="grow"><b>${esc(s.name)}</b><span class="hint">${esc(s.sub)}</span>${s.ok ? "" : `<span class="tag soon">${T("bientôt")}</span>`}</span></button>`;
  };
  const kept = pairs.some(([a, b]) => (a === tr.syncA && b === tr.syncB) || (a === tr.syncB && b === tr.syncA));
  $("#view").innerHTML = `<div class="tr-sync"><div class="eyebrow tr-crumb"><button class="link" id="tr-back">‹ ${T("Retour")}</button> ${T("TRANSFERTS")} · ${T("SYNCHRONISER")}</div>
    <h1>${T("Synchroniser")}</h1>
    <p class="sub">${T("Même partie des deux côtés : jeu par jeu, l'app montre où la partie va le plus loin, puis copie dans le sens choisi, avec aperçu.")}</p>
    <div class="tr-label">${T("SYNCHROS HABITUELLES")}</div>
    ${pairs.length ? `<div class="sync-pairs">${pairs.map(([a, b], i) => { const [txt, tone] = pairState(a, b), sa = side(a), sb = side(b);
      const on = (a === tr.syncA && b === tr.syncB) || (a === tr.syncB && b === tr.syncA);
      return `<button class="sync-pair ${on ? "chosen" : ""}" data-pair="${i}"><span class="pair-imgs"><span class="dev-ico">${sa.icon}</span><span class="sync-arrow">⇄</span><span class="dev-ico">${sb.icon}</span></span>
        <span class="grow"><b>${esc(sa.name)} ⇄ ${esc(sb.name)}</b><span class="pstate ${tone}">${esc(txt)}</span></span></button>`; }).join("")}</div>`
      : `<p class="hint">${T("Aucune pour l'instant : choisissez les deux côtés ci-dessous.")}</p>`}
    <div class="tr-label">${T("NOUVELLE SYNCHRO : CHOISISSEZ LES DEUX CÔTÉS")} <button class="link" id="sync-swap">⇅ ${T("Inverser")}</button></div>
    <div class="tr-label side-label">${T("CÔTÉ A")}</div><div class="tr-devs" data-scroll="sync-a">${sides.map((s) => sideCard(s, "a")).join("")}</div>
    <div class="tr-label side-label">${T("CÔTÉ B")}</div><div class="tr-devs" data-scroll="sync-b">${sides.map((s) => sideCard(s, "b")).join("")}</div>
    <p class="hint">${T("Pour l'instant, seules les paires Switch ⇄ Eden / ProsperoEden sont prises en charge ; NAS, compte PS5 et disque USB arrivent bientôt.")}</p>
    ${!ready ? "" : supported ? `<div class="actions"><b class="grow">${esc(A.name)} ⇄ ${esc(B.name)}</b>${kept ? "" : `<button class="secondary" id="sync-keep">${T("Garder en synchro habituelle")}</button>`}</div><div id="sync-host"></div>`
      : `<div class="empty">${T("Cette paire arrive bientôt.")}</div>`}</div>`;
  $("#tr-back").onclick = () => trBack();
  $$("[data-pair]").forEach((b) => b.onclick = () => { [tr.syncA, tr.syncB] = pairs[+b.dataset.pair]; trSync(); });
  $$("[data-side-a]").forEach((b) => b.onclick = () => { tr.syncA = b.dataset.sideA; if (!syncSupported(tr.syncA, tr.syncB || "")) tr.syncB = ""; trSync(); });
  $$("[data-side-b]").forEach((b) => b.onclick = () => { tr.syncB = b.dataset.sideB; trSync(); });
  $("#sync-swap").onclick = () => { [tr.syncA, tr.syncB] = [tr.syncB, tr.syncA]; trSync(); };
  if ($("#sync-keep")) $("#sync-keep").onclick = () => { keepPair(tr.syncA, tr.syncB); toast(T("Ajoutée aux synchros habituelles")); trSync(); };
  if (supported) { state.syncA = tr.syncA; state.syncB = tr.syncB; syncView(); }
}

function trMain() {
  const tr = state.tr, { devs, from, to } = trFromTo();
  const running = state.tasks.filter((t) => t.state === "running").length, waiting = state.tasks.filter((t) => activeState(t.state) && t.state !== "running").length;
  $("#view").innerHTML = `<h1>${T("D'un appareil à l'autre")}</h1>
    <p class="sub">${T("Jeux, applications, ROM : copier, déplacer ou compresser, entre la PS5, ses disques, les Switch et le NAS.")}</p>
    <div class="tr-label">${T("DEPUIS")} <button class="link" id="tr-swap">⇅ ${T("Inverser")}</button></div>
    <div class="tr-devs" data-scroll="tr-from">${devs.map((d) => devCard(d, { chosen: d.id === from.id, attr: `data-from="${esc(d.id)}"` })).join("")}${addNasCard}</div>
    <div class="tr-label">${T("VERS")}</div>
    <div class="tr-devs" data-scroll="tr-to">${devs.map((d) => devCard(d, { chosen: d.id === to.id, disabled: d.id === from.id, attr: `data-to="${esc(d.id)}"` })).join("")}${addNasCard}</div>
    <div class="tr-label">${T("QUE FAIRE ?")}</div>
    <div class="tr-actions">${TR_ACTIONS.map(([n, t], i) => `<button class="tr-action ${tr.action === i ? "chosen" : ""}" data-action="${i}"><b>${n}</b><span class="hint">${t}</span></button>`).join("")}</div>
    ${tr.sel.size ? `<p class="hint">${T("%d élément(s) déjà coché(s).", tr.sel.size)}</p>` : ""}
    <div class="tr-bottom">
      <button class="tr-big full" id="tr-full"><b>${T("Sauvegarde complète de la PS5")}</b><span class="hint">${T("Comptes, sauvegardes, jeux, homebrews, réglages : tout cocher, tout retrouver après une remise à zéro.")}</span></button>
      <button class="tr-big" id="tr-files"><b>${T("Fichiers")}</b><span class="hint">${T("Copier ou déplacer n'importe quels fichiers ou dossiers, d'un endroit à l'autre.")}</span></button>
      <button class="tr-big" id="tr-progress"><b>${T("Transferts en cours")} ${running ? SPIN : ""}</b><span class="hint ${running ? "accent" : ""}">${T("%d en cours", running)} · ${T("%d en attente", waiting)}</span></button>
    </div>`;
  $$("[data-add-nas]").forEach((b) => b.onclick = () => openEditor("", "ftp"));
  $$("[data-from]").forEach((b) => b.onclick = () => { tr.from = b.dataset.from; if (tr.to === tr.from) tr.to = ""; tr.sel.clear(); trMain(); });
  $$("[data-to]").forEach((b) => b.onclick = () => { tr.to = b.dataset.to; trMain(); });
  $("#tr-swap").onclick = () => { [tr.from, tr.to] = [tr.to, tr.from]; tr.sel.clear(); trMain(); };
  $$("[data-action]").forEach((b) => b.onclick = () => {
    tr.action = +b.dataset.action;
    // Synchroniser (et toute action avec une Switch) : l'écran Synchroniser, la Switch déjà choisie comme côté A.
    if (tr.action === 3 || from.kind === "switch" || to.kind === "switch") {
      const sw = [from, to].find((d) => d.kind === "switch");
      openSync(sw ? "switch:" + sw.id : "");
      return;
    }
    if (from.id === to.id) { toast(T("Choisissez une destination différente de la source.")); return; }
    if (from.kind === "peer") { toast(T("Depuis l'autre PS5 : ouvrez Backup Center sur elle et envoyez vers celle-ci.")); return; }
    if (to.kind === "peer" && tr.action === 2) { toast(T("Compresser vers l'autre PS5 : bientôt. Copiez, puis compressez là-bas.")); return; }
    if (from.kind === "ftp") {  // depuis le NAS : ses sauvegardes, remises sur la PS5
      if (to.kind === "ftp") { toast(T("D'un NAS à l'autre : pas encore. Passez par un disque USB.")); return; }
      if (tr.action !== 0) { toast(T("Depuis le NAS, on copie (restaure) : l'original reste sur le NAS.")); tr.action = 0; }
      tr.sel.clear(); tr.backups = null;
    }
    tr.screen = "choice"; window.scrollTo(0, 0); render(true);
  });
  $("#tr-full").onclick = () => {
    if (["ps5", "internal", "switch"].includes(to.kind)) { const d = devs.find((x) => x.kind === "usb" || x.kind === "ftp"); if (d) tr.to = d.id; }
    tr.fullTo = tr.fullTo || (devs.find((x) => x.kind === "usb" || x.kind === "ftp") || {}).id || "";
    trBack("full");
  };
  $("#tr-progress").onclick = () => trBack("progress");
  $("#tr-files").onclick = () => trBack("files");
}

// ---- Transferts › Fichiers : deux colonnes (DEPUIS / VERS), n'importe quels fichiers ou dossiers --------------------
// Le moteur copie ou déplace (tâche « files ») : jamais d'écrasement (identique sauté, différent gardé « (2) »),
// chaque fichier vérifié, l'original retiré seulement après une copie vérifiée de tout (Déplacer).
const FILE_KINDS = { here: "Cette PS5", peer: "Autre PS5", nas: "NAS", switch: "Switch" };
const parentDir = (p) => { const i = p.lastIndexOf("/"); return i <= 0 ? "/" : p.slice(0, i); };
const childDir = (p, name) => (p === "/" ? "" : p) + "/" + name;
async function filesOpen(side, place, path) {
  const f = state.tr.files, s = f[side];
  s.place = place; s.loading = true; s.error = ""; if (side === "src") s.sel = new Set();
  const p = (f.places || []).find((x) => x.id === place) || {};
  s.path = path || p.root || "/";
  trFiles();
  try { const j = await api(`/files/list?place=${encodeURIComponent(place)}&path=${encodeURIComponent(s.path)}`); s.entries = j.entries || []; s.free = j.free; }
  catch (e) { s.entries = []; s.error = e.message; }
  s.loading = false;
  if (state.tab === "transfers" && state.tr.screen === "files") trFiles();
}
function filesUp(side) {
  const f = state.tr.files, s = f[side], p = (f.places || []).find((x) => x.id === s.place) || {}, root = p.root || "/";
  if (s.path === root || s.path === "/") return;
  const up = parentDir(s.path);
  filesOpen(side, s.place, up.length < root.length ? root : up);
}
function trFiles() {
  const tr = state.tr;
  if (!tr.files) tr.files = { places: null, src: { sel: new Set(), entries: [] }, dst: { entries: [] } };
  const f = tr.files;
  if (!f.places && !f.loadingPlaces) {
    f.loadingPlaces = true;
    api("/files/places").then((p) => {
      f.places = p;
      const here = p.filter((x) => x.kind === "here");
      if (here[0]) filesOpen("src", here[0].id);
      if (here[1] || here[0]) filesOpen("dst", (here[1] || here[0]).id);
    }).catch((e) => toast(e.message)).finally(() => { f.loadingPlaces = false; });
  }
  const placeOpts = (cur) => (f.places || []).map((p) => `<option value="${esc(p.id)}" ${p.id === cur ? "selected" : ""}>${esc(T(FILE_KINDS[p.kind] || p.kind))} · ${esc(p.label)}</option>`).join("");
  const rows = (side) => {
    const s = f[side];
    if (s.loading) return `<p class="hint">${T("Chargement…")}</p>`;
    if (s.error) return `<p class="hint warn">${esc(s.error)}</p>`;
    const up = `<div class="frow"><span></span><button class="link" data-up="${side}">‹ ${T("Dossier parent")}</button><em></em></div>`;
    const list = (s.entries || []).map((e) => `<div class="frow">${side === "src" ? `<input type="checkbox" data-pick="${esc(e.name)}" ${s.sel.has(e.name) ? "checked" : ""}>` : `<span></span>`}
      ${e.dir ? `<button class="link" data-open="${side}" data-name="${esc(e.name)}">📁 ${esc(e.name)}</button>` : `<span>📄 ${esc(e.name)}</span>`}
      <em>${e.dir ? "" : size(e.size)}</em></div>`).join("");
    return up + (list || `<p class="hint">${T("Dossier vide")}</p>`);
  };
  const selSize = (f.src.entries || []).filter((e) => f.src.sel.has(e.name)).reduce((a, e) => a + (e.dir ? 0 : e.size), 0);
  $("#view").innerHTML = `<div class="eyebrow tr-crumb"><button class="link" id="tr-back">‹ ${T("Retour")}</button> ${T("TRANSFERTS")} · ${T("Fichiers")}</div>
    <h1>${T("Fichiers")}</h1>
    <p class="sub">${T("Rien n'est jamais écrasé : un fichier identique est sauté, un fichier différent est gardé à côté « (2) ». Chaque fichier est vérifié.")}</p>
    <div class="fpanes">
      <div class="card fpane"><h4>${T("DEPUIS")}</h4><select id="f-src-place">${placeOpts(f.src.place)}</select>
        <div class="fpath">${esc(f.src.path || "")}</div><div class="flist">${rows("src")}</div></div>
      <div class="fmid"><button id="f-copy" ${f.src.sel.size ? "" : "disabled"}>${T("Copier")} →</button><button class="secondary" id="f-move" ${f.src.sel.size ? "" : "disabled"}>${T("Déplacer")} →</button>
        <button class="danger" id="f-delete" ${f.src.sel.size ? "" : "disabled"}>${T("Supprimer…")}</button>
        <p class="hint">${f.src.sel.size ? T("%d élément(s)", f.src.sel.size) + (selSize ? " · " + size(selSize) : "") : T("Cochez des fichiers ou des dossiers")}</p></div>
      <div class="card fpane"><h4>${T("VERS")}</h4><select id="f-dst-place">${placeOpts(f.dst.place)}</select>
        <div class="fpath">${esc(f.dst.path || "")}${f.dst.free ? ` · <span class="hint">${T("%s libres", size(f.dst.free))}</span>` : ""}</div>
        <div class="flist">${rows("dst")}</div><button class="secondary" id="f-mkdir">＋ ${T("Nouveau dossier")}</button></div>
    </div>`;
  $("#tr-back").onclick = () => trBack("main");
  $("#f-src-place").onchange = (e) => filesOpen("src", e.target.value);
  $("#f-dst-place").onchange = (e) => filesOpen("dst", e.target.value);
  $$("[data-up]").forEach((b) => b.onclick = () => filesUp(b.dataset.up));
  $$("[data-open]").forEach((b) => b.onclick = () => { const s = f[b.dataset.open]; filesOpen(b.dataset.open, s.place, childDir(s.path, b.dataset.name)); });
  $$("[data-pick]").forEach((c) => c.onchange = () => { if (c.checked) f.src.sel.add(c.dataset.pick); else f.src.sel.delete(c.dataset.pick); trFiles(); });
  $("#f-mkdir").onclick = async () => {
    let name = ""; window.__fNew = "";
    const ok = await ask(T("Nouveau dossier"), `<input id="f-newname" class="input" placeholder="${esc(T("Nom du dossier"))}" oninput="window.__fNew=this.value">`, T("Créer"));
    name = (window.__fNew || "").trim();
    if (!ok || !name || /[\/\\]/.test(name) || name === "." || name === "..") { if (ok) toast(T("Nom de dossier invalide")); return; }
    try { await post("/files/mkdir", { place: f.dst.place, path: childDir(f.dst.path, name) }); filesOpen("dst", f.dst.place, childDir(f.dst.path, name)); }
    catch (e) { toast(e.message); }
  };
  const go = async (move) => {
    const src = f.src, dst = f.dst, names = [...src.sel];
    const sp = (f.places || []).find((x) => x.id === src.place) || {}, dp = (f.places || []).find((x) => x.id === dst.place) || {};
    const what = `<p>${esc(T("%d élément(s)", names.length))} · ${esc(T(FILE_KINDS[sp.kind] || ""))} ${esc(sp.label || "")} ${esc(src.path)} → ${esc(T(FILE_KINDS[dp.kind] || ""))} ${esc(dp.label || "")} ${esc(dst.path)}</p>
      <p class="hint">${esc(move ? T("Déplacer : les originaux sont retirés seulement après une copie vérifiée de tout.") : T("Copier : les originaux restent."))}</p>`;
    if (!await ask(move ? T("Déplacer ?") : T("Copier ?"), what, move ? T("Déplacer") : T("Copier"))) return;
    try {
      await post("/files/copy", { src_place: src.place, src_dir: src.path, names, dst_place: dst.place, dst_dir: dst.path, move });
      toast(move ? T("Déplacement lancé") : T("Copie lancée")); src.sel.clear(); refresh(true); trBack("progress");
    } catch (e) { toast(e.message); }
  };
  $("#f-copy").onclick = () => go(false);
  $("#f-move").onclick = () => go(true);
  // « Supprimer » : trois validations — la liste, « définitif », puis taper SUPPRIMER. Le moteur refuse les endroits protégés.
  $("#f-delete").onclick = async () => {
    const src = f.src, names = [...src.sel], sp = (f.places || []).find((x) => x.id === src.place) || {};
    if (sp.kind === "peer") { toast(T("Suppression sur l'autre PS5 : pas encore possible")); return; }
    const picked = (src.entries || []).filter((e) => src.sel.has(e.name)), folders = picked.filter((e) => e.dir).length;
    const bytes = picked.reduce((a, e) => a + (e.dir ? 0 : e.size), 0);
    const list = `<ul>${names.slice(0, 12).map((n) => `<li>${esc(n)}</li>`).join("")}${names.length > 12 ? `<li>${esc(T("… et %d autre(s)", names.length - 12))}</li>` : ""}</ul>`;
    const what = `<p>${esc(T(FILE_KINDS[sp.kind] || ""))} ${esc(sp.label || "")} · ${esc(src.path)}</p>${list}
      <p class="hint">${bytes ? esc(T("Fichiers : %s", size(bytes))) + (folders ? " · " : "") : ""}${folders ? esc(T("%d dossier(s), avec tout leur contenu", folders)) : ""}</p>`;
    if (!await ask(T("Supprimer %d élément(s) ?", names.length), what, T("Continuer"))) return;
    if (!await ask(T("Définitif"), `<p>${esc(T("Définitif : pas de corbeille."))}</p>`, T("Continuer"))) return;
    window.__fDel = "";
    if (!await ask(T("Dernière confirmation"), `<p>${esc(T("Tapez SUPPRIMER pour confirmer."))}</p><input class="input" autocomplete="off" oninput="window.__fDel=this.value">`, T("Supprimer"))) return;
    if ((window.__fDel || "").trim().toUpperCase() !== "SUPPRIMER") { toast(T("Suppression annulée")); return; }
    try {
      await post("/files/delete", { place: src.place, dir: src.path, names, confirm: "SUPPRIMER" });
      toast(T("Suppression lancée : suivez-la dans Transferts en cours")); src.sel.clear(); refresh(true); trBack("progress");
    } catch (e) { toast(e.message); }
  };
}

function trChoice() {
  const tr = state.tr, { from, to } = trFromTo();
  if (from.kind === "ftp") return trRestore(from, to);
  const compress = tr.action === 2;
  if (compress) tr.cat = 0;
  const items = trItems(from);
  const count = (k) => state.items.filter((it) => it.kind === k && (from.kind === "ps5" || it.place === from.id)).length;
  const imgs = state.images;
  const sub = (it) => {
    const g = imgs && imgs.get(it.key);
    if (!g) return [placeLabel(it.place), it.image_type].filter(Boolean).join(" · ");
    let disk = g.disk && g.disk.label ? g.disk.label : placeLabel(it.place);
    let s = `${disk} · ${storageText(g)}`;
    if (g.container === "ffpfsc") s += " · " + T("déjà compressée");
    else if (g.storage === "folder" && it.kind === "game_ps5") s += " · " + T("compressible");
    if (g.running) s += " · " + T("en cours de jeu"); else if (g.mounted) s += " · " + T("monté");
    return s;
  };
  const chosen = state.items.filter((it) => tr.sel.has(it.key));
  const total = chosen.reduce((a, it) => a + itemBytes(it, tr.saves), 0);
  const tight = to.hasFree && total > to.free;
  const opts = [["compress", T("Compresser avant l'envoi"), T("dossiers PS5 → image .ffpfsc"), tr.compress || compress], ["saves", T("Avec les sauvegardes"), T("les sauvegardes du jeu suivent"), tr.saves],
    ["verify", T("Vérifier à l'arrivée"), T("empreinte SHA-256 de chaque fichier"), tr.verify]];
  const allOn = items.length && items.every((it) => tr.sel.has(it.key));
  $("#view").innerHTML = `<div class="eyebrow tr-crumb"><button class="link" id="tr-back">‹ ${T("Retour")}</button> ${T("TRANSFERTS")} · ${esc(from.name)} → ${esc(to.name)} · ${TR_ACTIONS[tr.action][0]}</div>
    <h1>${compress ? T("Quels jeux compresser") : T("Quoi transférer")}</h1>
    <div class="filters"><div class="pills">${TR_CATS.filter((_, i) => !compress || !i).map(([k, n], i) => `<button data-cat="${i}" class="${tr.cat === i ? "active" : ""}">${n} · ${count(k)}</button>`).join("")}</div>
      <button class="secondary" id="tr-all" ${items.length ? "" : "disabled"}>${allOn ? T("Tout décocher") : T("Tout cocher")}</button>
      ${imgs ? "" : `<span class="hint">${SPIN}${T("Lecture des images…")}</span>`}</div>
    <div class="tr-split">
      <div class="list">${items.length ? items.map((it) => `<label class="row pick ${tr.sel.has(it.key) ? "on" : ""}"><input type="checkbox" data-sel="${esc(it.key)}" ${tr.sel.has(it.key) ? "checked" : ""}>
        <div class="thumb">${coverHtml(it, true)}</div><div><div class="t">${esc(it.title)}</div><div class="d">${esc(sub(it))}</div></div><div class="size">${size(itemBytes(it, tr.saves))}</div></label>`).join("")
        : `<div class="empty">${from.kind === "switch" ? T("Le contenu de cet appareil se choisit dans sa synchronisation (Transferts › Synchroniser).") : T("Rien de ce type sur cet appareil.")}</div>`}</div>
      <div><div class="tr-label">${T("OPTIONS")}</div>${opts.map(([k, n, t, on]) => `<label class="toggle-card"><span class="grow"><b>${n}</b><span class="hint">${t}</span></span>
        <input type="checkbox" class="switch" data-opt="${k}" ${on ? "checked" : ""} ${k === "compress" && compress ? "disabled" : ""}></label>`).join("")}</div>
    </div>
    <div class="summary"><div class="grow"><b>${chosen.length > 1 ? T("%d éléments", chosen.length) : T("%d élément", chosen.length)} · ${size(total)} → ${esc(to.name)}</b>
      <div class="hint ${tight ? "bad" : ""}">${tight ? T("Pas assez de place : %s libres", size(to.free)) : to.hasFree ? T("Place libre : %s", size(to.free)) : T("Place libre vérifiée par le moteur avant de commencer")}</div></div>
      <button class="primary" id="tr-go" ${chosen.length && !tight ? "" : "disabled"}>${compress ? T("Compresser") : T("Lancer le transfert")}</button></div>`;
  $("#tr-back").onclick = () => trBack();
  $$("[data-cat]").forEach((b) => b.onclick = () => { tr.cat = +b.dataset.cat; trChoice(); });
  $("#tr-all").onclick = () => { if (allOn) items.forEach((it) => tr.sel.delete(it.key)); else items.forEach((it) => tr.sel.add(it.key)); trChoice(); };
  $$("[data-sel]").forEach((c) => c.onchange = () => { if (c.checked) tr.sel.add(c.dataset.sel); else tr.sel.delete(c.dataset.sel); trChoice(); });
  $$("[data-opt]").forEach((c) => c.onchange = () => { tr[c.dataset.opt] = c.checked; trChoice(); });
  $("#tr-go").onclick = () => trLaunch(to);
}
async function trLaunch(to) {
  const tr = state.tr, keys = [...tr.sel];
  if (!keys.length) { toast(T("Cochez au moins un élément.")); return; }
  $("#tr-go").disabled = true;
  let ok = 0;
  for (const key of keys) {
    let body;
    if ((to.kind === "ftp" || to.kind === "peer") && tr.action !== 2)  // vers le NAS ou une autre PS5 : une sauvegarde vérifiée, rangée comme les autres
      body = { type: "backup", item: key, parts: tr.saves ? [] : ["game", "update", "dlc", "app", "state", "config"], place: to.id };
    else if (tr.action === 2) body = { type: "compress", item: key, target: to.id };
    else body = { type: "transfer", mode: tr.action === 1 ? "move" : "copy", item: key, to: to.id, saves: tr.saves, verify: tr.verify, compress: tr.compress };
    try { await post("/tasks", body); ok++; } catch (e) { toast(e.message); }
  }
  if (!ok) { $("#tr-go").disabled = false; return; }
  tr.sel.clear();
  if (to.kind === "ftp" && tr.action === 1) toast(T("Vers le NAS : copie vérifiée, l'original reste sur la console."));
  if (to.kind === "peer") toast(T("Vers l'autre PS5 : copie vérifiée dans son « PBC Backup », l'original reste ici."));
  else toast(ok > 1 ? T("Transferts ajoutés à la file") : T("Transfert ajouté à la file"));
  refresh(true); trBack("progress");
}

// Depuis un NAS : choisir des sauvegardes et les remettre sur la PS5.
function trRestore(from, to) {
  const tr = state.tr, here = to.kind === "ps5";
  if (!tr.backups || tr.backups.place !== from.id) {
    tr.backups = { place: from.id, loading: true, data: null };
    api("/backups?place=" + encodeURIComponent(from.id)).then((d) => { tr.backups.data = d; }).catch((e) => { tr.backups.data = { error: e.message, backups: [] }; })
      .finally(() => { tr.backups.loading = false; if (state.tab === "transfers" && tr.screen === "choice") render(true); });
  }
  const list = ((tr.backups.data && tr.backups.data.backups) || []).filter((b) => b.complete !== false)
    .sort((a, b) => (a.title || "").localeCompare(b.title || "") || (b.created_unix || 0) - (a.created_unix || 0));
  const err = tr.backups.data && tr.backups.data.error;
  const chosen = list.filter((b) => tr.sel.has("bk:" + b.bundle));
  const total = chosen.reduce((a, b) => a + (b.bytes || 0), 0);
  $("#view").innerHTML = `<div class="eyebrow tr-crumb"><button class="link" id="tr-back">‹ ${T("Retour")}</button> ${T("TRANSFERTS")} · ${esc(from.name)} → ${esc(to.name)} · ${T("Restaurer")}</div>
    <h1>${T("Quoi remettre sur la PS5")}</h1>
    <div class="tr-split">
      <div class="list">${tr.backups.loading ? loading(T("Lecture des sauvegardes du NAS…")) : list.length ? list.map((b) => {
        const it = state.items.find((i) => i.key === b.key);
        return `<label class="row pick ${tr.sel.has("bk:" + b.bundle) ? "on" : ""}"><input type="checkbox" data-bk="${esc(b.bundle)}" ${tr.sel.has("bk:" + b.bundle) ? "checked" : ""}>
          ${it ? `<div class="thumb">${coverHtml(it, true)}</div>` : '<div class="thumb"></div>'}<div><div class="t">${esc(b.title)}</div>
          <div class="d">${esc((b.created || "").slice(0, 16).replace("T", " "))} · ${(b.parts || []).map((p) => PARTS[p] || p).join(", ")}</div></div><div class="size">${b.bytes ? size(b.bytes) : T("taille ?")}</div></label>`;
      }).join("") : `<div class="empty">${err ? `<span class="bad">${esc(T("NAS injoignable : %s", err))}</span><br><span class="hint">${T("Vérifiez qu'il est allumé et que la PS5 peut le joindre (réseau, pare-feu).")}</span>` : T("Aucune sauvegarde sur cet appareil pour l'instant.")}</div>`}</div>
      <div><div class="tr-label">${T("OÙ ÇA VA")}</div><div class="card"><h4>${here ? T("À sa place d'origine") : T("Dans un dossier du disque")}</h4>
        <p class="hint">${here ? T("Jeu, mises à jour, applis : chaque fichier revient où il était, inscription comprise. Rien n'est écrasé.") : esc(T("Copié dans « PBC Restaurés » sur %s : les fichiers, sans inscription du jeu.", to.name))}</p>
        <p class="hint warn">${T("Les sauvegardes de jeu (saves) se remettent sur un compte depuis l'onglet Parties.")}</p></div></div>
    </div>
    <div class="summary"><div class="grow"><b>${chosen.length > 1 ? T("%d sauvegardes", chosen.length) : T("%d sauvegarde", chosen.length)} · ${size(total)} → ${esc(to.name)}</b><div class="hint">${T("Chaque fichier est vérifié à l'arrivée (empreinte).")}</div></div>
      <button class="primary" id="tr-go" ${chosen.length ? "" : "disabled"}>${T("Restaurer")}</button></div>`;
  $("#tr-back").onclick = () => trBack();
  $$("[data-bk]").forEach((c) => c.onchange = () => { const k = "bk:" + c.dataset.bk; if (c.checked) tr.sel.add(k); else tr.sel.delete(k); trRestore(from, to); });
  $("#tr-go").onclick = async () => {
    let queued = 0, savesOnly = 0;
    for (const b of chosen) {
      const parts = (b.parts || []).filter((p) => p !== "save");  // les saves passent par un compte (onglet Parties)
      if (!parts.length) { savesOnly++; continue; }
      let target = "";
      if (!here) { const p = state.places.find((x) => x.id === to.id); target = `${p && p.type !== "internal" ? p.mount : "/data"}/PBC Restaurés/${b.title}`; }
      try { await post("/tasks", { type: "restore", place: from.id, bundle: b.bundle, parts, target, overwrite: false }); queued++; } catch (e) { toast(e.message); }
    }
    if (!queued && !savesOnly) return;
    tr.sel.clear();
    if (savesOnly) toast(queued ? T("Restaurations lancées ; les saves seules se remettent depuis l'onglet Parties.") : T("Ce sont des saves : remettez-les sur un compte depuis l'onglet Parties."));
    else toast(queued > 1 ? T("Restaurations ajoutées à la file") : T("Restauration ajoutée à la file"));
    if (queued) { refresh(true); trBack("progress"); }
  };
}

// « Fichiers » : étape à part (relecture de la copie, ou comparaison avec un fichier déjà là), avec sa propre avancée.
function taskPhase(t) {
  const p = t.progress || {};
  if (t.state !== "running" || !p.phase || !p.phase_total) return null;
  const f = Math.min(1, p.phase_done / p.phase_total), pct = Math.floor(f * 100);
  return { f, text: p.phase === "verify" ? T("Vérification %d %%", pct) : T("Comparaison avec la copie déjà là %d %%", pct), done: p.phase_done, total: p.phase_total };
}
// Suivi : les tâches actives puis les dernières terminées (7 au plus), avec pause / reprise / annulation.
function taskRowHtml(t) {
  const p = t.progress || {}, pct = p.bytes_total ? Math.min(100, Math.round((p.bytes_done / p.bytes_total) * 100)) : t.state === "done" ? 100 : 0;
  const eta = t.state === "running" && t.bytes_per_second > 0 && p.bytes_total ? duration((p.bytes_total - p.bytes_done) / t.bytes_per_second) : "";
  const btns = { running: ["pause", "cancel"], queued: ["pause", "cancel"], paused: ["resume", "cancel"], failed: ["resume", "details", "remove"], done: ["details", "remove"], cancelled: ["details", "remove"] }[t.state] || [];
  if ((t.type === "files_copy" || t.type === "files_move") && (t.state === "cancelled" || t.state === "failed") && !(t.report && (t.report.cleaned || t.report.cleaning))) btns.unshift("cleanup");
  const names = { pause: T("Pause"), resume: T("Reprendre"), cancel: T("Annuler"), remove: T("Retirer de la liste"), cleanup: T("Retirer les fichiers copiés…"), details: T("Détails") };
  const sw = t.type === "import_switch" || t.type === "export_switch";
  const title = (t.title || "").split(" · ").pop();
  const it = state.items.find((i) => i.key === t.item || (t.title_id && i.title_id === t.title_id))
    || state.items.find((i) => i.has_cover && i.title === title)
    || { key: "", title: title || TASK_KINDS[t.type] || T("Tâche"), kind: sw ? "rom" : "game_ps5", platform: sw ? "Switch" : "PS5", has_cover: false };
  const ph = taskPhase(t);
  const what = `${TASK_KINDS[t.type] || T("Tâche")} · ${ph ? `${esc(ph.text)} · ${T("%s sur %s", size(Math.min(ph.done, ph.total)), size(ph.total))}` : t.state === "running" && t.step ? esc(t.step) : STATES[t.state] || t.state}`;
  return `<div class="row"><div class="thumb">${coverHtml(it, true)}</div><div><div class="t">${esc(t.title || TASK_KINDS[t.type] || T("Tâche"))}<span class="state ${t.state}">${t.state === "running" ? SPIN : ""}${STATES[t.state] || t.state}</span></div>
    <div class="d">${what}${p.bytes_total ? ` · ${pct} % · ${T("%s sur %s", size(p.bytes_done), size(p.bytes_total))}` : ""}${p.files_total ? ` · ${T("%d/%d fichiers", p.files_done, p.files_total)}` : ""}${t.state === "running" && !ph && t.bytes_per_second > 0 ? ` · ${size(t.bytes_per_second)}/s${eta ? " · " + T("reste") + " " + eta : ""}` : ""}${t.error ? ` · <span class="bad">${esc(t.error)}</span>` : ""}${t.state === "done" && t.report && t.report.mount_deferred ? ` · ${T("apparaît sur l'accueil une fois l'application fermée")}` : ""}${t.state === "done" && t.report && t.report.bytes_per_second && t.report.seconds ? ` · ${T("lecture %s/s", size(t.report.bytes_per_second))}${t.report.note ? " · " + T("jeu trop petit : mesure peu fiable") : ""}` : ""}</div></div>
    <div class="btns">${t.type === "link_send" && t.state === "done" && t.report && t.report.foreground && t.report.foreground.title_id ? `<button data-close-remote="${esc(t.id)}">${esc(T("Fermer « %s » là-bas", t.report.foreground.title || t.report.foreground.title_id))}</button>` : ""}${btns.map((a) => `<button data-id="${t.id}" data-a="${a}">${names[a]}</button>`).join("")}</div>
    ${ph ? `<div class="bar done"><span style="width:${Math.round(ph.f * 100)}%"></span></div>` : activeState(t.state) || (t.state === "done" && p.bytes_total) ? `<div class="bar ${t.state === "done" ? "done" : ""}"><span style="width:${pct}%"></span></div>` : ""}</div>`;
}
function bindTaskButtons() {
  $$(".btns button[data-close-remote]").forEach((b) => b.onclick = () => {
    const t = (state.tasks || []).find((x) => x.id === b.dataset.closeRemote);
    if (t) confirmCloseRemote(t.report.instance, t.report.peer, t.report.foreground);
  });
  $$(".btns button[data-a]").forEach((b) => b.onclick = async () => {
    if (b.dataset.a === "details") { const t = (state.tasks || []).find((x) => x.id === b.dataset.id); if (t) taskDetails(t); return; }
    if (b.dataset.a === "cleanup") {  // deux confirmations : on retire des fichiers
      if (!await ask(T("Retirer les fichiers que cette copie a créés ?"), `<p class="hint">${esc(T("Seulement ce que cette copie a créé : un fichier qui était déjà là n'est jamais touché, ni rien d'autre."))}</p>`, T("Continuer"))) return;
      if (!await ask(T("Confirmer le retrait"), `<p>${esc(T("Les fichiers créés par cette copie vont être retirés de la destination."))}</p>`, T("Oui, retirer ces fichiers"))) return;
    }
    b.disabled = true;
    try { await post(`/tasks/${b.dataset.id}/${b.dataset.a}`); refresh(true); } catch (e) { toast(e.message); b.disabled = false; }
  });
}
function trProgress() {
  const act = state.tasks.filter((t) => activeState(t.state));
  const rest = state.tasks.filter((t) => !activeState(t.state)).slice().reverse();
  const list = [...act, ...rest].slice(0, Math.max(7, act.length));
  const running = act.filter((t) => t.state === "running").length;
  $("#view").innerHTML = `<div class="eyebrow tr-crumb"><button class="link" id="tr-back">‹ ${T("Retour")}</button> ${T("TRANSFERTS")}</div><h1>${T("Transferts en cours")}</h1>
    <p class="sub">${T("%d en cours", running)} · ${T("%d en attente", act.length - running)} · ${T("ils continuent même si l'app est fermée")}</p>
    <div class="list">${list.length ? list.map(taskRowHtml).join("") : `<div class="empty">${T("Aucun transfert pour l'instant.")}</div>`}</div>
    <p><button class="link" id="tr-all-tasks">${T("Tout l'historique dans Tâches ›")}</button></p>`;
  $("#tr-back").onclick = () => trBack();
  $("#tr-all-tasks").onclick = () => setTab("tasks");
  bindTaskButtons();
}

// Sauvegarde complète : cases à cocher par catégorie, tailles calculées par le moteur.
function trFull() {
  const tr = state.tr;
  if (!tr.fullLoading && (!tr.full || Date.now() - tr.fullAt > (tr.full.computing ? 3000 : 60000))) {
    tr.fullLoading = true;
    api("/fullbackup").then((j) => { tr.full = j; }).catch(() => { tr.full = tr.full || { computing: true }; })
      .finally(() => { tr.fullLoading = false; tr.fullAt = Date.now(); if (state.tab === "transfers" && tr.screen === "full") { render(); if (tr.full.computing) setTimeout(() => { if (state.tab === "transfers" && tr.screen === "full") render(); }, 3000); } });
  }
  const info = (tr.full && tr.full.categories) || {}, computing = !tr.full || tr.full.computing;
  const dests = trDevices().filter((d) => d.kind === "usb" || d.kind === "ftp" || d.kind === "peer");
  if (!dests.some((d) => d.id === tr.fullTo)) tr.fullTo = dests.length ? dests[0].id : "";
  const dest = dests.find((d) => d.id === tr.fullTo);
  let total = 0, checked = 0;
  for (const [id] of FULL_CATS) if (!tr.fullOff.has(id)) { checked++; if (info[id]) total += info[id].bytes || 0; }
  const modes = [T("Sauvegarder"), T("Restaurer"), T("Vers une autre PS5")];
  const tight = dest && dest.hasFree && total > dest.free;
  $("#view").innerHTML = `<div class="eyebrow tr-crumb"><button class="link" id="tr-back">‹ ${T("Retour")}</button> ${T("TRANSFERTS")} · ${T("SAUVEGARDE COMPLÈTE")}</div>
    <h1>${T("Tout ce qui est à vous sur cette PS5")}</h1>
    <p class="sub">${T("Cochez ce que vous voulez garder. Après une remise à zéro, rouvrez l'app : tout revient dans le bon ordre.")}</p>
    <div class="filters"><div class="pills">${modes.map((m, i) => `<button data-mode="${i}" class="${tr.fullMode === i ? "active" : ""}">${m}</button>`).join("")}</div></div>
    <div class="full-grid">${FULL_CATS.map(([id, name, text, expert]) => {
      const c = info[id], on = !tr.fullOff.has(id);
      return `<label class="toggle-card ${on ? "on" : ""}"><input type="checkbox" data-full="${id}" ${on ? "checked" : ""}><span class="grow"><b>${name}</b>
        <span class="hint ${expert ? "warn" : ""}">${esc(c && c.detail ? c.detail : text)}</span></span><span class="size">${c ? size(c.bytes) : ""}${!c || computing ? SPIN : ""}</span></label>`;
    }).join("")}</div>
    <div class="summary"><div class="grow"><b>${checked > 1 ? T("%d catégories", checked) : T("%d catégorie", checked)} · ${total ? size(total) : T("taille en calcul")} → ${esc(dest ? dest.name : "?")} / PBC Backup / ${T("Cette PS5")}</b>
      <div class="hint ${tight ? "bad" : ""}">${tight ? T("Pas assez de place : %s libres", size(dest.free)) : T("Une seule sauvegarde datée, vérifiée fichier par fichier.")}</div></div>
      <select id="full-dest">${dests.length ? dests.map((d) => `<option value="${esc(d.id)}" ${d.id === tr.fullTo ? "selected" : ""}>${esc(d.name)}${d.hasFree ? " · " + T("%s libres", size(d.free)) : " · " + T("réseau")}</option>`).join("") : `<option value="">${T("Aucun disque USB ni NAS")}</option>`}</select>
      <button class="primary" id="full-go" ${checked && dest ? "" : "disabled"}>${[T("Tout sauvegarder"), T("Restaurer"), T("Transférer")][tr.fullMode]}</button></div>`;
  $("#tr-back").onclick = () => trBack();
  $$("[data-mode]").forEach((b) => b.onclick = () => { tr.fullMode = +b.dataset.mode; trFull(); });
  $$("[data-full]").forEach((c) => c.onchange = () => { if (c.checked) tr.fullOff.delete(c.dataset.full); else tr.fullOff.add(c.dataset.full); trFull(); });
  $("#full-dest").onchange = (e) => { tr.fullTo = e.target.value; trFull(); };
  $("#full-go").onclick = async () => {
    if (tr.fullMode !== 0) { toast(tr.fullMode === 1 ? T("Restaurer : choisissez la sauvegarde complète dans l'onglet Sauvegardes (bientôt).") : T("Vers une autre PS5 : jumelez-la (Appareils › PlayStation 5), puis choisissez un de ses disques comme destination.")); return; }
    if (!dest) { toast(T("Choisissez une destination (disque USB ou NAS).")); return; }
    try {
      await post("/tasks", { type: "full_backup", categories: FULL_CATS.map(([id]) => id).filter((id) => !tr.fullOff.has(id)), place: dest.id });
      toast(T("Sauvegarde complète lancée")); refresh(true); trBack("progress");
    } catch (e) { toast(e.message); }
  };
}

// ---- Démarrage ----
if (!linkPage()) refresh(true);
document.getElementById("support-btn").onclick = () => { state.settingsSection = "support"; setTab("settings"); };
setTimeout(() => { if (state.token) whatsNew(); }, 1500);
setTimeout(autoCheckUpdates, 2000);  // ouverture de la page
// Ouverte depuis le code QR de la télé (« …/#cloud ») : directement l'ajout d'un stockage dans le nuage, dès que
// l'appareil est associé (un téléphone qui scanne pour la première fois saisit d'abord le code).
if (location.hash === "#cloud") {
  let tries = 0;
  const wait = setInterval(() => {
    if (++tries > 600) { clearInterval(wait); return; }
    if (!state.token || !state.remotes) return;
    clearInterval(wait);
    history.replaceState(null, "", location.pathname);
    setTab("devices");
    cloudPick();
  }, 1000);
}
