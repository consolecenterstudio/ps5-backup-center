<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

## 3.2.0

- Nasz wbudowany ShadowMount (ShadowMountPlus 1.7, zbudowany przez nas): gdy zainstalowany ShadowMountPlus nie działa, obrazy i foldery gier i tak są montowane i rejestrowane — bez ingerencji w system i bez portu; zainstalowany ma pierwszeństwo, gdy tylko działa (Ustawienia › Montowanie gier). Jego ustawienia nigdy nie są zmieniane.
- „Zainstaluj…” dla gry, która jest gdzie indziej (NAS, chmura, inna PS5), z Plików: wybierasz docelową PS5 i jej dysk; sprawdzona kopia, potem montowanie lub instalacja przez konsolę. NAS i chmura to źródła, nigdy cele.
- Web: „Zainstaluj z tego PC…” (Pliki) — plik gry z PC (.pkg lub obraz) wysyłany w częściach na wybraną PS5 i dysk, ze wznawianiem po przerwie, potem montowany lub instalowany przez konsolę.
- Ustawienia › Narzędzia konsoli: payloady Payload Manager i te działające — uruchamiaj je lub zatrzymuj stąd (kstuff i nasz silnik chronione, ShadowMountPlus zatrzymywany porządnie) oraz otwieraj Payload Manager; koniec z restartem konsoli, by ponownie uruchomić narzędzie.
- Biblioteki gier: folder na NAS, w chmurze, na dysku USB lub na drugiej PS5, wskazany w Plikach; jego niezainstalowane gry pojawiają się na ekranie głównym i w Bibliotece („Do zainstalowania”), gotowe do instalacji na wybranej PS5 i dysku. Odczytywany w tle, nic tam nie jest zmieniane. Każdy .pkg jest rozpoznawany po nagłówku: gra, aktualizacja lub DLC (nigdy nie mylone z grą: umieszczane pod nią, do zainstalowania przez konsolę po zainstalowaniu gry), PS4 lub PS5, fake PKG lub oficjalny pakiet; Kraken jest proponowany tylko dla gry PS5 w fake PKG, w innym razie podawany jest powód.
- „Zainstaluj…” z innego miejsca: wybór formatu (obraz, .pkg gry uruchamiany przez Kraken lub instalowany przez konsolę albo obraz przekonwertowany do .pkg i zainstalowany) z szacowanym czasem; potem jedno zadanie z numerowanymi krokami (kopia, sprawdzanie, konwersja, instalacja), każdy z paskiem, prędkością i pozostałym czasem. Można je przerwać na każdym kroku.
- W Pobranych „+ Dodaj link” przyjmuje też link magnet lub plik .torrent: wybierasz dysk (najpierw ten z Ustawienia › Sieć), śledzisz postęp (źródła, prędkość, pozostały czas) i wstrzymujesz, wznawiasz lub usuwasz (TV i web).
- Pobieranie › Moje linki: wszystkie linki udostępnione z konsoli (BitTorrent, .zip konsoli, MEGA, OneDrive), z kodem QR, adresem i możliwością zatrzymania. Linki MEGA i OneDrive są tam zachowane.
- Plany: zapis lub gra, które już są w miejscu docelowym (ta sama zawartość, skopiowana przez dowolną PS5), nie są kopiowane ponownie; podsumowanie mówi „już aktualne”. Nic nie jest ponownie odczytywane.
- Pobieranie › „ORBIT I BLACKBOX”: co Orbit i BlackBox pobrały na konsolę (stan, dysk, rozmiar, brak pliku) i ich pliki tymczasowe (zachowane archiwum, trwające pobieranie, pozostałość), odczytane u nich bez żadnych zmian.
- „Zainstaluj…” w narzędziach gry, Pakietach .pkg i Pobranych › Orbit i BlackBox: zamontuj ją tam, gdzie jest (przeniesiona do folderu gier swojego dysku, bez kopiowania, montowana przez ShadowMount), albo zainstaluj na innym dysku lub innej PS5 (sprawdzona kopia, oryginał zostaje lub jest usuwany).
- „Zainstaluj… › Zamontuj tam, gdzie jest” dla .pkg Kraken: gra jest zgłaszana konsoli i uruchamia się z pakietu, bez rozpakowywania — przez Kraken Mount, jeśli jest zainstalowany, w przeciwnym razie przez naszą wbudowaną wersję (praca Kraken Mount w naszym silniku, z ampr-emu-ng, pgo_stub i dlc_emu zbudowanymi przez nas; wymaga kstuff). Gra przejęta już przez ShadowMount lub zainstalowana przez konsolę nie jest ruszana. Pierwsza wersja, do wypróbowania.

Zatwierdzenie produkcyjne Dropbox jest w toku. Automatyczna synchronizacja między PS5 i instalacja na innej konsoli to pierwsze wersje wymagające dalszych testów.

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Ekran główny" width="88%">
</p>

[Pierwsze kroki](../guides/01-start.pl.md) · [Media](../media.md)

# PS5 Backup Center

**Pobieraj, instaluj i przenoś gry.** Odbieraj gry ze sparowanej PS5 w sieci lokalnej. Twórz kopie, porównuj, synchronizuj i przywracaj zapisy między PS5, dyskami USB, NAS i chmurą z telewizora lub przeglądarki. Zarządzaj aplikacjami i DLC PS5 oraz zapisami Switch i zgodnych emulatorów.

[Nowości 3.2.0](https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.2.0)

**Twoja kolekcja. Twoje postępy. Twoje kopie zapasowe.**

Sprawdź, co wymaga kopii zapasowej, porównaj istniejące kopie i uporządkuj wymianę danych między urządzeniami. PS5 Backup Center łączy katalog, kopie, synchronizację i transfery na telewizorze oraz w przeglądarce.

**3.0.0 · GPL-3.0-or-later · 19 · 8**

**Obserwuj nas** · [X @CCenterStudio](https://x.com/CCenterStudio) · [YouTube @ConsoleCenterStudio](https://www.youtube.com/@ConsoleCenterStudio) · [Reddit @ConsoleCenterStudio](https://www.reddit.com/user/ConsoleCenterStudio/)

Wolne oprogramowanie. Bez abonamentu.

## Co możesz zrobić

- Przeglądać gry, aplikacje i ROM-y; wybierać pełną zawartość, zapisy, stany lub obsługiwane ustawienia.
- Porównywać kopie, zarządzać wersjami i tworzyć harmonogramy z opcjonalną rotacją.
- Śledzić zadania w przeglądarce, gdy silnik działa po zamknięciu aplikacji telewizyjnej.

## Biblioteka · Synchro

Przechowywane kopie i aktywne zapisy to różne rzeczy. Wybierz kopie między PS5 lub z NAS-em. Lokalne identyfikatory mogą się różnić: ważne jest konto docelowe. Switch ↔ Eden / ProsperoEden używa JKSV i ftpd na Switchu. Aplikacje i ustawienia mają własne reguły. Pełne gry obsługuje sekcja Transfery.

## Nowości w 3.0

- Dodaj NAS lub konsolę w urządzeniach i sprawdź tożsamość przed zapisaniem. Dla NAS wybierz obsługiwany protokół, przetestuj połączenie i wybierz folder. Między PS5 potwierdź kod parowania; dla Switch przygotuj ftpd i eksport JKSV. Obsługiwane są FTP / FTPS, SMB i WebDAV. „Używany do” określa, czy NAS przechowuje zapisy, synchro, czy oba.
- Aby użyć chmury, otwórz Urządzenia › Chmura: wybierz usługę S3 (Amazon S3, Backblaze B2, Wasabi, Cloudflare R2, Scaleway, OVHcloud, Synology C2…) i wpisz jej klucze — kod QR otwiera ten sam formularz na telefonie, aby je wkleić — albo pCloud, Koofr, Nextcloud z własnym kontem. Połącz Google Drive i osobiste konto OneDrive z telefonu. Rejestracja w Dropbox jest ograniczona na etapie rozwoju.
- Pełna klawiatura ekranowa.

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

PS5 Backup Center jest darmowy i taki zostanie. Jeśli aplikacja ci pomaga, możesz wesprzeć jej rozwój (w aplikacji: ♥ w prawym górnym rogu). Krypto poniżej: wysyłaj tylko w podanej sieci. Prezenty, Ko-fi, Patreon i cele: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

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
