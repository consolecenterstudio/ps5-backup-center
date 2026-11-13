<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Startscherm" width="88%">
</p>

[Aan de slag](../guides/01-start.nl.md) · [Media](../media.md)

# PS5 Backup Center

**Je collectie. Je voortgang. Je back-ups.**

Ontdek wat een back-up nodig heeft, vergelijk bestaande kopieën en organiseer uitwisselingen tussen apparaten. PS5 Backup Center brengt inventaris, back-ups, synchronisatie en overdrachten samen op je tv en in je browser.

**2.98.4 · GPL-3.0-or-later · 19 · 8**

Vrije software. Geen abonnement.

## Wat je kunt doen

- Games, apps en ROMs bekijken; volledige inhoud, spelvoortgang, opslagstaten of ondersteunde instellingen kiezen.
- Bewaarde kopieën vergelijken, versies beheren en plannen met optionele rotatie instellen.
- Taken in de browser volgen terwijl de engine na het sluiten van de tv-app blijft draaien.

## Bibliotheek · Sync

Bewaarde back-ups en actieve spelvoortgang verschillen. Kies kopieën tussen PS5-consoles of met een NAS. Lokale gebruikers-ID’s kunnen verschillen: de doelaccount is belangrijk. Switch ↔ Eden / ProsperoEden gebruikt JKSV en ftpd op Switch. Apps en ondersteunde instellingen hebben eigen regels. Volledige games gaan via Overdrachten.

## Nieuw in 2.98

- **De bibliotheek van de andere PS5**: blader hiervandaan door zijn games, apps en saves (tv en web) en maak op afstand back-ups.
- **Bestanden**: kopieer, verplaats of verwijder elk bestand tussen deze PS5, USB, de NAS, een Switch en de andere PS5; verwijderen vraagt meerdere bevestigingen.
- **Een game verwijderen**, hier of op de andere PS5; de saves worden eerst geback-upt.
- **SMB 2/3** (Windows, Synology, QNAP) naast FTP/FTPS; **Églantine** (Switch 2); de console herstarten vanuit de app.
- Optionele, versleutelde **internetverbinding** met een PS5 buitenshuis (poort 52373); op het lokale netwerk poort 52370.

## Installatie

1. Download `PPSA99950.zip` van de [nieuwste versie](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) en controleer de SHA-256 (`SHA256SUMS`).
2. Pak uit en kopieer de map `PPSA99950` naar `/data/homebrew/` op de console (FTP of USB).
3. Open **PS5 Backup Center** via je homebrew-launcher: de app start zelf zijn engine. Optioneel: de `…-engine.elf` van de versie start alleen de engine via een ELF-loader (poort 9021).

De app zoekt zelf naar nieuwe versies in deze repository, meldt ze op Start en kan ze installeren, met terugkeer naar de vorige versie.

## TV / Web

- **Start**
- **Bibliotheek**
- **Voortgang**
- **Saves**
- **Sync**
- **Overdrachten**
- **Apparaten**
- **Taken**
- **Instellingen**

## Instellingen

Kies uit 19 talen en acht thema’s. Geluiden, achtergrondmuziek en volume zijn afzonderlijk. Controleer bronnen, genegeerde onderdelen, gebruikers en apparaten voordat je koppelingen wijzigt. Bij nieuwigheden kun je wijzigingen nalezen. Dankbetuigingen tonen auteurs, licenties en bronnen.

## Andere projecten

PS5 Center is een breder toekomstig project waarvan de reikwijdte en routekaart samen worden uitgewerkt; het is niet beperkt tot game- en appbeheer en is nog niet ontwikkeld. PS5 Connector onderzoekt uitbreiding van consoles via connectoren en bestaande oplossingen. NAS, gedeelde mappen en USB zijn voorbeelden, geen vaste afbakening. Er is geen prototype of datum aangekondigd.

## Steunen

PS5 Backup Center is gratis en blijft gratis. Helpt de app je, dan kun je de ontwikkeling steunen (in de app: het ♥ rechtsboven). Crypto hieronder: stuur alleen via het vermelde netwerk. Cadeaus, Ko-fi, Patreon en doelen: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## Aan de slag

- [Aan de slag](../guides/01-start.nl.md)
- [Dagelijks back-uppen en herstellen](../guides/02-everyday.nl.md)
- [Doelen en gekoppelde consoles](../guides/03-destinations.nl.md)
- [Spellen en overdrachten](../guides/04-transfers.nl.md)
- [Emulators en ROMs](../guides/05-emulators.nl.md)
- [Instellingen en updates](../guides/06-settings.nl.md)
- [Problemen oplossen](../guides/07-troubleshooting.nl.md)
- [Voor ontwikkelaars](../guides/08-developers.nl.md)
- [Project en ondersteuning](../guides/09-project.nl.md)
