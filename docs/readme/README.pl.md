<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Ekran główny" width="88%">
</p>

[Pierwsze kroki](../guides/01-start.pl.md) · [Media](../media.md)

# PS5 Backup Center

**Twoja kolekcja. Twoje postępy. Twoje kopie zapasowe.**

Sprawdź, co wymaga kopii zapasowej, porównaj istniejące kopie i uporządkuj wymianę danych między urządzeniami. PS5 Backup Center łączy katalog, kopie, synchronizację i transfery na telewizorze oraz w przeglądarce.

**2.98.4 · GPL-3.0-or-later · 19 · 8**

Wolne oprogramowanie. Bez abonamentu.

## Co możesz zrobić

- Przeglądać gry, aplikacje i ROM-y; wybierać pełną zawartość, zapisy, stany lub obsługiwane ustawienia.
- Porównywać kopie, zarządzać wersjami i tworzyć harmonogramy z opcjonalną rotacją.
- Śledzić zadania w przeglądarce, gdy silnik działa po zamknięciu aplikacji telewizyjnej.

## Biblioteka · Synchro

Przechowywane kopie i aktywne zapisy to różne rzeczy. Wybierz kopie między PS5 lub z NAS-em. Lokalne identyfikatory mogą się różnić: ważne jest konto docelowe. Switch ↔ Eden / ProsperoEden używa JKSV i ftpd na Switchu. Aplikacje i ustawienia mają własne reguły. Pełne gry obsługuje sekcja Transfery.

## Nowości w 2.98

- **Biblioteka drugiej PS5**: przeglądaj stąd jej gry, aplikacje i zapisy (TV i web) i twórz ich kopie zdalnie.
- **Pliki**: kopiuj, przenoś lub usuwaj dowolne pliki między tą PS5, USB, NAS-em, Switchem i drugą PS5; usuwanie wymaga kilku potwierdzeń.
- **Odinstalowanie gry**, tutaj lub na drugiej PS5; zapisy są najpierw kopiowane.
- **SMB 2/3** (Windows, Synology, QNAP) obok FTP/FTPS; **Églantine** (Switch 2); restart konsoli z aplikacji.
- Opcjonalne, szyfrowane **połączenie przez Internet** z PS5 poza domem (port 52373); w sieci lokalnej port 52370.

## Instalacja

1. Pobierz `PPSA99950.zip` z [najnowszej wersji](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) i sprawdź SHA-256 (`SHA256SUMS`).
2. Rozpakuj i skopiuj folder `PPSA99950` do `/data/homebrew/` na konsoli (FTP lub USB).
3. Uruchom **PS5 Backup Center** z launchera homebrew: aplikacja sama uruchamia swój silnik. Opcjonalnie: `…-engine.elf` z wydania uruchamia sam silnik z loadera ELF (port 9021).

Aplikacja sama sprawdza nowe wersje w tym repozytorium, pokazuje je na ekranie głównym i może je zainstalować, z powrotem do poprzedniej wersji.

## TV / Web

- **Start**
- **Biblioteka**
- **Postępy**
- **Zapisy**
- **Synchro**
- **Transfery**
- **Urządzenia**
- **Zadania**
- **Ustawienia**

## Ustawienia

Wybierz jeden z 19 języków i ośmiu motywów. Dźwięki, muzyka i głośność są oddzielne. Sprawdź źródła, ignorowane elementy, użytkowników i urządzenia przed zmianą powiązań. W nowościach odczytasz zmiany. Podziękowania zawierają autorów, licencje i źródła składników.

## Pozostałe projekty

PS5 Center to szerszy przyszły projekt, którego zakres i plan rozwoju zostaną ustalone wspólnie. Nie ogranicza się do zarządzania grami i aplikacjami i nie został jeszcze opracowany. PS5 Connector bada rozszerzanie możliwości konsol przez złącza i istniejące rozwiązania. NAS, zasoby współdzielone i USB to przykłady, a nie zamknięty zakres. Nie ogłoszono prototypu ani terminu.

## Wesprzyj

Oprogramowanie jest bezpłatne. Zgłoszenia, tłumaczenia, udokumentowane testy i dobrowolne wsparcie mogą finansować rozwój, narzędzia AI, testy i sprzęt. Płatności i publiczny kontakt pozostają do ustalenia.

## Pierwsze kroki

- [Pierwsze kroki](../guides/01-start.pl.md)
- [Codzienne kopie i przywracanie](../guides/02-everyday.pl.md)
- [Cele i sparowane konsole](../guides/03-destinations.pl.md)
- [Gry i transfery](../guides/04-transfers.pl.md)
- [Emulatory i ROM-y](../guides/05-emulators.pl.md)
- [Ustawienia i aktualizacje](../guides/06-settings.pl.md)
- [Rozwiązywanie problemów](../guides/07-troubleshooting.pl.md)
- [Dla programistów](../guides/08-developers.pl.md)
- [Projekt i wsparcie](../guides/09-project.pl.md)
