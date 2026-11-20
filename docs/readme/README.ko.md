<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

## 3.2.0

- 다운로드의 '+ 링크 추가'에서 마그넷 링크나 .torrent 파일도 받습니다. 저장할 디스크를 고르고(설정 › 네트워크의 디스크가 먼저), 진행 상황(소스, 속도, 남은 시간)을 보며 일시 정지, 재개, 제거할 수 있습니다(TV와 웹).
- 다운로드 › 내 링크: 본체에서 준 모든 링크(BitTorrent, 본체 .zip, MEGA, OneDrive)를 QR 코드, 주소, 중지 방법과 함께 보여 줍니다. MEGA와 OneDrive 링크도 여기에 보관됩니다.
- 계획: 대상에 이미 있는 세이브나 게임(어느 PS5가 복사했든 내용이 같으면)은 다시 복사하지 않으며, 결과에 '이미 최신'이라고 표시됩니다. 이를 위해 다시 읽지 않습니다.
- 다운로드 › 'ORBIT 및 BLACKBOX': Orbit과 BlackBox가 콘솔에 다운로드한 항목(상태, 드라이브, 크기, 파일 없음)과 임시 파일(남은 압축 파일, 다운로드 중, 잔여 파일)을 앱 쪽을 바꾸지 않고 읽어 표시합니다.
- 게임 도구, .pkg 패키지, 다운로드 › Orbit 및 BlackBox의 “설치…”: 그 자리에서 마운트(드라이브의 게임 폴더로 옮김, 복사 없음, ShadowMount가 마운트)하거나 다른 드라이브나 다른 PS5에 설치(검증된 복사, 원본 유지 또는 삭제).
- Kraken .pkg의 “설치… › 그 자리에서 마운트”: 게임을 콘솔에 등록하고 패키지에서 바로 실행하며 아무것도 압축 해제하지 않습니다. Kraken Mount가 설치되어 있으면 그것으로, 아니면 우리 내장 버전으로 합니다(Kraken Mount가 하던 일을 우리 엔진에서, ampr-emu-ng, pgo_stub, dlc_emu는 우리가 빌드; kstuff 필요). ShadowMount가 이미 맡은 게임이나 콘솔에 설치된 게임은 건드리지 않습니다. 첫 버전이니 시험해 보세요.

Dropbox 프로덕션 승인은 심사 중입니다. PS5 간 자동 동기화와 다른 콘솔로의 설치는 초기 기능이며 추가 실기 테스트가 필요합니다.

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — 홈 화면" width="88%">
</p>

[시작하기](../guides/01-start.ko.md) · [미디어](../media.md)

# PS5 Backup Center

**게임을 다운로드하고 설치하고 전송하세요.** 로컬 네트워크에서 페어링한 PS5의 게임을 받으세요. TV나 브라우저에서 PS5, USB 드라이브, NAS, 클라우드 간 저장 데이터를 백업하고 비교하고 동기화하고 복원하세요. PS5 앱과 DLC, Switch 및 호환 에뮬레이터 저장 데이터도 관리할 수 있습니다.

[새로운 기능 3.2.0](https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.2.0)

**나의 컬렉션. 나의 진행 상황. 나의 백업.**

백업이 필요한 항목을 찾고 기존 사본을 비교하며 기기 간 교환을 정리하세요. PS5 Backup Center는 목록, 백업, 동기화, 전송을 TV와 브라우저에 모읍니다.

**3.0.0 · GPL-3.0-or-later · 19 · 8**

**팔로우** · [X @CCenterStudio](https://x.com/CCenterStudio) · [YouTube @ConsoleCenterStudio](https://www.youtube.com/@ConsoleCenterStudio) · [Reddit @ConsoleCenterStudio](https://www.reddit.com/user/ConsoleCenterStudio/)

자유 소프트웨어. 구독 없음.

## 할 수 있는 일

- 게임, 앱, ROM을 살펴보고 전체 콘텐츠, 저장 데이터, 상태 또는 지원 설정을 선택합니다.
- 보관된 사본을 비교하고 버전을 관리하며 선택적 순환 보관이 있는 예약 계획을 만듭니다.
- TV 앱을 닫아도 엔진이 계속 실행되어 브라우저에서 작업을 확인할 수 있습니다.

## 라이브러리 · 동기화

보관용 백업과 현재 플레이에 사용하는 저장 데이터는 다릅니다. PS5 사이 또는 NAS와 유지할 사본을 선택하세요. 본체마다 로컬 사용자 ID가 다를 수 있어 대상 계정이 중요합니다. Switch ↔ Eden / ProsperoEden은 Switch에서 JKSV와 ftpd를 사용합니다. 앱과 지원 설정에는 별도 비교 규칙이 있습니다. 전체 게임은 전송에서 처리합니다.

## 3.0의 새로운 기능

- 기기 화면에서 NAS나 콘솔을 추가하고 저장 전에 대상을 확인합니다. NAS는 지원 프로토콜을 선택해 연결을 시험하고 폴더를 고릅니다. PS5끼리는 페어링 코드를 확인하며 Switch는 ftpd와 JKSV 내보내기를 준비합니다. FTP / FTPS, SMB, WebDAV를 지원합니다. ‘용도’에서 NAS를 세이브 데이터, 동기화 또는 둘 다에 쓸지 선택합니다.
- 클라우드는 기기 › 클라우드를 엽니다. S3 서비스(Amazon S3, Backblaze B2, Wasabi, Cloudflare R2, Scaleway, OVHcloud, Synology C2…)를 고르고 키를 입력합니다. QR 코드를 쓰면 같은 양식이 휴대폰에서 열려 키를 붙여 넣을 수 있습니다. 또는 pCloud, Koofr, Nextcloud를 내 계정으로 연결합니다. 휴대폰에서 Google Drive와 개인 OneDrive를 연결하세요. 개발 기간에는 Dropbox 등록이 제한됩니다.
- 전체 화면 키보드.

## 2.98 새 기능

- **다른 PS5의 라이브러리**: 그 PS5의 게임, 앱, 세이브를 여기서 둘러보고(TV와 웹) 원격으로 백업합니다.
- **파일**: 이 PS5, USB, NAS, Switch, 다른 PS5 사이에서 모든 파일을 복사·이동·삭제합니다. 삭제는 여러 번 확인합니다.
- **게임 삭제**: 여기 또는 다른 PS5에서. 세이브는 먼저 백업됩니다.
- FTP/FTPS와 함께 **SMB 2/3**(Windows, Synology, QNAP), **Églantine**(Switch 2), 앱에서 본체 재시작.
- 집 밖의 PS5와 연결하는 선택형 암호화 **인터넷 연결**(포트 52373). 로컬 네트워크는 포트 52370.

## 설치

1. [최신 버전](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest)에서 `PPSA99950.zip`을 내려받고 SHA-256을 확인합니다(`SHA256SUMS`).
2. 압축을 풀고 `PPSA99950` 폴더를 본체의 `/data/homebrew/`에 복사합니다(FTP 또는 USB).
3. 홈브루 런처에서 **PS5 Backup Center**를 엽니다. 앱이 엔진을 직접 시작합니다. 선택: 릴리스의 `…-engine.elf`는 ELF 로더(포트 9021)로 엔진만 시작합니다.

앱이 이 저장소에서 새 버전을 스스로 확인하고 홈에서 알려 주며 설치할 수 있고, 이전 버전으로 되돌릴 수도 있습니다.

## TV / Web

- **홈**
- **라이브러리**
- **플레이 데이터**
- **세이브 데이터**
- **동기화**
- **전송**
- **기기**
- **작업**
- **설정**

## 설정

19개 언어와 8개 테마를 선택할 수 있습니다. 효과음, 배경 음악, 음량은 따로 설정합니다. 연결을 바꾸기 전에 소스, 무시한 항목, 사용자와 기기를 확인합니다. 새 소식에서 변경 사항을 다시 읽을 수 있습니다. 감사의 말에는 제작자, 라이선스와 소스가 있습니다.

## 다른 프로젝트

PS5 Center는 범위와 로드맵을 함께 구체화할 더 폭넓은 미래 프로젝트입니다. 게임과 앱 관리에 국한되지 않으며 아직 개발되지 않았습니다. PS5 Connector는 커넥터와 기존 솔루션으로 콘솔 기능을 확장하는 방안을 연구합니다. NAS, 공유, USB는 예시이며 확정된 범위가 아닙니다. 시제품이나 출시일은 발표되지 않았습니다.

## 후원

PS5 Backup Center는 무료이며 앞으로도 무료입니다. 도움이 되었다면 개발을 후원할 수 있습니다(앱에서는 오른쪽 위 ♥). 암호화폐는 아래 표에 있으며 표시된 네트워크로만 보내 주세요. 선물, Ko-fi, Patreon, 프로젝트 목표: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## 시작하기

- [시작하기](../guides/01-start.ko.md)
- [일상적인 백업과 복원](../guides/02-everyday.ko.md)
- [대상 및 페어링된 콘솔](../guides/03-destinations.ko.md)
- [게임과 전송](../guides/04-transfers.ko.md)
- [에뮬레이터와 ROM](../guides/05-emulators.ko.md)
- [설정과 업데이트](../guides/06-settings.ko.md)
- [문제 해결](../guides/07-troubleshooting.ko.md)
- [개발자 안내](../guides/08-developers.ko.md)
- [프로젝트와 후원](../guides/09-project.ko.md)
