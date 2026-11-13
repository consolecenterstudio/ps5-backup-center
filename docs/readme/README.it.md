<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Schermata iniziale" width="88%">
</p>

[Primi passi](../guides/01-start.it.md) · [Media](../media.md)

# PS5 Backup Center

**La tua collezione. Le tue partite. I tuoi backup.**

Individua cosa richiede un backup, confronta le copie esistenti e organizza gli scambi tra dispositivi. PS5 Backup Center riunisce inventario, backup, sincronizzazione e trasferimenti sulla TV e nel browser.

**2.98.4 · GPL-3.0-or-later · 19 · 8**

Software libero. Senza abbonamento.

## Cosa puoi fare

- Esplorare giochi, app e ROM; scegliere contenuto completo, salvataggi, stati o impostazioni supportate.
- Confrontare copie archiviate, gestire versioni e programmare piani con rotazione facoltativa.
- Seguire le attività nel browser mentre il motore continua dopo la chiusura dell’app TV.

## Libreria · Sincro

Le copie archiviate e i salvataggi attivi sono distinti. Scegli le copie da mantenere tra PS5 o con un NAS. Gli ID locali possono differire: conta l’account di destinazione. Switch ↔ Eden / ProsperoEden usa JKSV e ftpd sulla Switch. App e impostazioni supportate hanno regole proprie. I giochi completi passano da Trasferimenti.

## Novità della 2.98

- **La libreria dell'altra PS5**: sfoglia da qui i suoi giochi, app e salvataggi (TV e web) e fai il backup a distanza.
- **File**: copia, sposta o elimina qualsiasi file tra questa PS5, USB, il NAS, una Switch e l'altra PS5; l'eliminazione chiede più conferme.
- **Disinstallare un gioco**, qui o sull'altra PS5; i salvataggi vengono copiati prima.
- **SMB 2/3** (Windows, Synology, QNAP) oltre a FTP/FTPS; **Églantine** (Switch 2); riavviare la console dall'app.
- **Collegamento via Internet** facoltativo e cifrato con una PS5 lontana da casa (porta 52373); in rete locale, porta 52370.

## Installazione

1. Scarica `PPSA99950.zip` dall'[ultima versione](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) e verifica lo SHA-256 (`SHA256SUMS`).
2. Estrailo e copia la cartella `PPSA99950` in `/data/homebrew/` sulla console (FTP o USB).
3. Apri **PS5 Backup Center** dal tuo launcher di homebrew: l'app avvia da sola il suo motore. Facoltativo: il `…-engine.elf` della versione avvia solo il motore da un loader ELF (porta 9021).

L'app cerca da sola le nuove versioni in questo repository, le segnala nella Home e può installarle, con ritorno alla versione precedente.

## TV / Web

- **Home**
- **Libreria**
- **Progressi**
- **Salvataggi**
- **Sincro**
- **Trasferimenti**
- **Dispositivi**
- **Attività**
- **Impostazioni**

## Impostazioni

Scegli tra 19 lingue e otto temi. Suoni, musica di sottofondo e volume sono separati. Controlla fonti, elementi ignorati, utenti e dispositivi prima di cambiare associazioni. Le novità permettono di rileggere i cambiamenti. I riconoscimenti includono autori, licenze e fonti dei componenti.

## Gli altri progetti

PS5 Center è un progetto futuro più ampio, con ambito e roadmap da definire insieme; non si limita alla gestione di giochi e app e non è ancora sviluppato. PS5 Connector esplora come estendere le capacità delle console con connettori e soluzioni esistenti. NAS, condivisioni e USB sono esempi, non un ambito fisso. Nessun prototipo o data annunciati.

## Sostieni

Il software è gratuito. Segnalazioni, traduzioni, test documentati e sostegno volontario possono finanziare sviluppo, strumenti IA, prove e hardware. Pagamenti e contatto pubblico sono da definire.

## Primi passi

- [Primi passi](../guides/01-start.it.md)
- [Backup e ripristino quotidiani](../guides/02-everyday.it.md)
- [Destinazioni e console associate](../guides/03-destinations.it.md)
- [Giochi e trasferimenti](../guides/04-transfers.it.md)
- [Emulatori e ROM](../guides/05-emulators.it.md)
- [Impostazioni e aggiornamenti](../guides/06-settings.it.md)
- [Risolvere problemi](../guides/07-troubleshooting.it.md)
- [Per sviluppatori](../guides/08-developers.it.md)
- [Progetto e sostegno](../guides/09-project.it.md)
