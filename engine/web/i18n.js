// Traductions du web : même catalogue que l'app télé (i18n/<code>.json, clé = texte français d'origine).
//   T("Sauvegarder")          → texte traduit, ou le français s'il manque
//   T("%s jeux", n)           → %s / %d remplacés dans l'ordre (%1$s, %2$s… pour changer l'ordre)
// Langue : réglage « language » du moteur (le même que la télé) ; « auto » → langue de la console (signalée par l'app
// télé au moteur : « system_language »), sinon celle du navigateur.
// Le catalogue est chargé avant app.js : tout le code peut appeler T() dès le départ, même hors des fonctions.
"use strict";
const I18N = { code: "fr", setting: "auto", auto: "en", languages: [], decimal: ",", rtl: false, text: {} };

function T(key, ...args) {
  let s = Object.prototype.hasOwnProperty.call(I18N.text, key) ? I18N.text[key] : key;
  if (!args.length) return s;
  let i = 0;
  return s.replace(/%(?:(\d+)\$)?([sd%])/g, (m, pos, type) => {
    if (type === "%") return "%";
    const v = args[pos ? +pos - 1 : i++];
    return v === undefined ? "" : String(v);
  });
}

// Nom d'une langue dans la langue affichée (« russe » en français) ; vide s'il répète le nom natif.
function langOther(l) {
  if (!l.fr || l.code === I18N.code) return "";
  const here = T(l.fr);
  return here.toLowerCase() === String(l.name).toLowerCase() ? "" : here;
}
// « Русский · russe » : nom natif, puis son nom dans la langue affichée.
function langLabel(l) { const o = langOther(l); return o ? `${l.name} · ${o}` : l.name; }

// Étiquette du navigateur ou de la console (« pt-BR », « es-MX », « zh-TW »…) → langue proposée, anglais à défaut.
function i18nResolve(tag) {
  const codes = I18N.languages.map((l) => l.code), lower = String(tag || "").replace("_", "-").toLowerCase();
  const exact = codes.find((c) => c.toLowerCase() === lower);
  if (exact) return exact;
  const [base, region = ""] = lower.split("-");
  if (base === "es" && region && region !== "es") return "es-419";
  if (base === "pt") return region === "br" ? "pt-BR" : "pt-PT";
  if (base === "zh") return ["tw", "hk", "mo", "hant"].includes(region) ? "zh-Hant" : "zh-Hans";
  return codes.find((c) => c.toLowerCase() === base) || "en";
}
function i18nBrowser() {
  for (const tag of navigator.languages || [navigator.language]) {
    const c = i18nResolve(tag);
    if (c !== "en" || /^en/i.test(tag)) return c;
  }
  return "en";
}
// Change la langue (réglage partagé avec la télé) puis recharge la page dans cette langue.
async function setLanguage(code) {
  try { localStorage.setItem("pbc-lang", code); } catch (e) {}
  await fetch("/api/v2/settings", { method: "POST", headers: { "X-PBC-Token": localStorage.getItem("pbc-token") || "", "Content-Type": "application/json" },
    body: JSON.stringify({ language: code }) }).catch(() => {});
  location.reload();
}

// Textes de index.html : data-i18n (contenu), data-i18n-title / -placeholder / -aria-label (attributs).
function i18nPage(root = document) {
  root.querySelectorAll("[data-i18n]").forEach((el) => { el.textContent = T(el.textContent.trim()); });
  for (const a of ["title", "placeholder", "aria-label"])
    root.querySelectorAll(`[data-i18n-${a}]`).forEach((el) => el.setAttribute(a, T(el.getAttribute(a))));
}

(async () => {
  const get = async (url, opts) => { const r = await fetch(url, opts); if (!r.ok) throw new Error(r.status); return r.json(); };
  try { I18N.languages = await get("/i18n/languages.json"); } catch (e) { I18N.languages = [{ code: "fr", name: "Français", decimal: "," }]; }
  let setting = "", system = "";
  try { setting = localStorage.getItem("pbc-lang") || ""; } catch (e) {}
  try {
    const token = localStorage.getItem("pbc-token") || "";
    if (token) { const s = await get("/api/v2/settings", { headers: { "X-PBC-Token": token } }); setting = s.language || setting; system = s.system_language || ""; }
  } catch (e) {}
  I18N.setting = setting || "auto";
  I18N.auto = system ? i18nResolve(system) : i18nBrowser();
  const code = I18N.setting === "auto" ? I18N.auto : i18nResolve(I18N.setting);
  const lang = I18N.languages.find((l) => l.code === code) || I18N.languages[0];
  I18N.code = lang.code; I18N.decimal = lang.decimal || "."; I18N.rtl = !!lang.rtl;
  if (I18N.code !== "fr") { try { I18N.text = await get(`/i18n/${I18N.code}.json`); } catch (e) { I18N.text = {}; } }
  document.documentElement.lang = I18N.code;
  document.documentElement.dir = I18N.rtl ? "rtl" : "ltr";
  i18nPage();
  const s = document.createElement("script");
  s.src = "/app.js";
  document.body.appendChild(s);
})();
