<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Startbildschirm" width="88%">
</p>

[Erste Schritte](../guides/01-start.de.md) · [Medien](../media.md)

# PS5 Backup Center

**Deine Sammlung. Dein Spielfortschritt. Deine Backups.**

Erkenne, was gesichert werden sollte, vergleiche vorhandene Kopien und organisiere den Austausch zwischen Geräten. PS5 Backup Center verbindet Inventar, Backups, Synchronisierung und Übertragungen am Fernseher und im Browser.

**3.0.0 · GPL-3.0-or-later · 19 · 8**

**Folge uns** · [X @CCenterStudio](https://x.com/CCenterStudio) · [YouTube @ConsoleCenterStudio](https://www.youtube.com/@ConsoleCenterStudio) · [Reddit @ConsoleCenterStudio](https://www.reddit.com/user/ConsoleCenterStudio/)

Freie Software. Ohne Abonnement.

## Das kannst du tun

- Spiele, Apps und ROMs durchsuchen; vollständige Inhalte, Spielstände, Speicherzustände oder unterstützte Einstellungen auswählen.
- Gesicherte Kopien vergleichen, Versionen verwalten und Pläne mit optionaler Aufbewahrungsrotation erstellen.
- Aufgaben im Browser verfolgen, während die Engine nach dem Schließen der TV-App weiterläuft.

## Bibliothek · Sync

Gesicherte Kopien und aktive Spielstände sind verschieden. Wähle Kopien zwischen PS5-Konsolen oder einem NAS. Lokale Benutzer-IDs können sich unterscheiden: Das Zielkonto ist wichtig. Switch ↔ Eden / ProsperoEden verwendet JKSV und ftpd auf der Switch. Apps und unterstützte Einstellungen haben eigene Regeln. Ganze Spiele gehören zu Übertragungen.

## Neu in 3.0

- Füge NAS oder Konsole bei den Geräten hinzu und prüfe die Identität vor dem Speichern. Wähle beim NAS ein unterstütztes Protokoll, teste die Verbindung und den Zielordner. Bestätige zwischen PS5 den Kopplungscode; bereite bei Switch ftpd und einen JKSV-Export vor. FTP / FTPS, SMB und WebDAV werden unterstützt. „Verwendet für“ legt fest, ob das NAS Spielstände, Sync oder beides aufnimmt.
- Für die Cloud öffne Geräte › Cloud: Wähle einen S3-Dienst (Amazon S3, Backblaze B2, Wasabi, Cloudflare R2, Scaleway, OVHcloud, Synology C2…) und gib seine Schlüssel ein – der QR-Code öffnet dasselbe Formular auf deinem Handy, damit du sie dort einfügen kannst – oder pCloud, Koofr, Nextcloud mit deinem Konto. Google Drive, OneDrive und Dropbox verbindest du mit deinem eigenen Konto vom Handy aus (Aktivierung in einem kommenden Update).
- Vollständige Bildschirmtastatur.

## Neu in 2.98

- **Die Bibliothek der anderen PS5**: ihre Spiele, Apps und Spielstände von hier aus durchsuchen (TV und Web) und aus der Ferne sichern.
- **Dateien**: beliebige Dateien zwischen dieser PS5, USB, dem NAS, einer Switch und der anderen PS5 kopieren, verschieben oder löschen; Löschen verlangt mehrere Bestätigungen.
- **Spiel deinstallieren**, hier oder auf der anderen PS5; die Spielstände werden vorher gesichert.
- **SMB 2/3** (Windows, Synology, QNAP) zusätzlich zu FTP/FTPS; **Églantine** (Switch 2); Konsole aus der App neu starten.
- Optionale, verschlüsselte **Internetverbindung** zu einer PS5 außer Haus (Port 52373); im lokalen Netz Port 52370.

## Installation

1. `PPSA99950.zip` aus der [neuesten Version](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) herunterladen und die SHA-256 prüfen (`SHA256SUMS`).
2. Entpacken und den Ordner `PPSA99950` nach `/data/homebrew/` auf der Konsole kopieren (FTP oder USB).
3. **PS5 Backup Center** über deinen Homebrew-Launcher öffnen: Die App startet ihre Engine selbst. Optional: die `…-engine.elf` der Version startet nur die Engine über einen ELF-Loader (Port 9021).

Die App sucht selbst nach neuen Versionen in diesem Repository, zeigt sie auf dem Start an und kann sie installieren, mit Rückkehr zur vorherigen Version.

## TV / Web

- **Start**
- **Bibliothek**
- **Spielfortschritt**
- **Spielstände**
- **Sync**
- **Übertragungen**
- **Geräte**
- **Aufgaben**
- **Einstellungen**

## Einstellungen

Wähle aus 19 Sprachen und acht Designs. Töne, Hintergrundmusik und Lautstärke sind getrennte Einstellungen. Prüfe Quellen, ignorierte Elemente, Benutzer und Geräte vor geänderten Zuordnungen. Unter Neuigkeiten kannst du Änderungen nachlesen. Die Danksagungen nennen Autoren, Lizenzen und Quellen der Komponenten.

## Weitere Projekte

PS5 Center ist ein umfassenderes Zukunftsprojekt, dessen Umfang und Roadmap gemeinsam erarbeitet werden. Es ist nicht auf die Verwaltung von Spielen und Apps beschränkt und noch nicht entwickelt. PS5 Connector untersucht die Erweiterung von Konsolen durch Konnektoren und bestehende Lösungen. NAS, Freigaben und USB sind Beispiele, kein festgelegter Umfang. Prototyp und Termin sind nicht angekündigt.

## Unterstützen

PS5 Backup Center ist kostenlos und bleibt es. Wenn dir die App hilft, kannst du die Entwicklung unterstützen (in der App: das ♥ oben rechts). Krypto unten — sende nur über das angegebene Netzwerk. Geschenke, Ko-fi, Patreon und Projektziele: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## Erste Schritte

- [Erste Schritte](../guides/01-start.de.md)
- [Sichern und Wiederherstellen im Alltag](../guides/02-everyday.de.md)
- [Ziele und gekoppelte Konsolen](../guides/03-destinations.de.md)
- [Spiele und Übertragungen](../guides/04-transfers.de.md)
- [Emulatoren und ROMs](../guides/05-emulators.de.md)
- [Einstellungen und Updates](../guides/06-settings.de.md)
- [Probleme lösen](../guides/07-troubleshooting.de.md)
- [Für Entwickler](../guides/08-developers.de.md)
- [Projekt und Unterstützung](../guides/09-project.de.md)
