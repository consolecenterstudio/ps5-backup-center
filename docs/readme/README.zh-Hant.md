<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — 主畫面" width="88%">
</p>

[開始使用](../guides/01-start.zh-Hant.md) · [媒體](../media.md)

# PS5 Backup Center

**你的遊戲收藏。你的遊戲進度。你的備份。**

找出需要備份的內容，比較現有副本，並安排裝置之間的資料交換。PS5 Backup Center 在電視與瀏覽器中整合內容清單、備份、同步和傳輸。

**2.98.4 · GPL-3.0-or-later · 19 · 8**

自由軟體，無需訂閱。

## 你可以做什麼

- 瀏覽遊戲、應用程式和 ROM；選擇完整內容、遊戲存檔、即時存檔或支援的設定。
- 比較保存的副本、管理版本，並建立可選輪替的排程計畫。
- 電視應用程式關閉後，引擎持續運作，仍可在瀏覽器中追蹤工作。

## 遊戲庫 · 同步

保存的備份副本與目前使用的遊戲存檔不同。選擇在兩台 PS5 之間或與 NAS 保持一致的副本。本機使用者 ID 可能不同，因此必須確認目標帳號。Switch ↔ Eden / ProsperoEden 在 Switch 端使用 JKSV 和 ftpd。應用程式與支援的設定有各自的比較規則。完整遊戲透過「傳輸」處理。

## 2.98 新功能

- **另一台 PS5 的遊戲庫**：在這裡瀏覽它的遊戲、應用程式和存檔（電視與網頁），並遠端備份。
- **檔案**：在本機 PS5、USB、NAS、Switch 與另一台 PS5 之間複製、移動或刪除任何檔案；刪除需多次確認。
- **解除安裝遊戲**，在本機或另一台 PS5 上；會先備份存檔。
- 除 FTP/FTPS 外支援 **SMB 2/3**（Windows、Synology、QNAP）；**Églantine**（Switch 2）；從應用程式重新啟動主機。
- 可選的加密**網際網路連線**，連到不在家中的 PS5（連接埠 52373）；區域網路使用連接埠 52370。

## 安裝

1. 從[最新版本](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest)下載 `PPSA99950.zip`，並核對 SHA-256（`SHA256SUMS`）。
2. 解壓縮後將 `PPSA99950` 資料夾複製到主機的 `/data/homebrew/`（FTP 或 USB）。
3. 從你的自製軟體啟動器開啟 **PS5 Backup Center**：應用程式會自行啟動引擎。可選：版本附帶的 `…-engine.elf` 可透過 ELF 載入器（連接埠 9021）單獨啟動引擎。

應用程式會自動在本儲存庫尋找新版本，在首頁提示並可安裝，也可退回上一版本。

## TV / Web

- **首頁**
- **遊戲庫**
- **遊戲進度**
- **存檔**
- **同步**
- **傳輸**
- **裝置**
- **工作**
- **設定**

## 設定

可選擇19種語言與八種主題。介面音效、背景音樂和音量分別控制。變更關聯前檢查來源、忽略項目、使用者與裝置。更新說明可重新閱讀各版本變更，致謝包含元件作者、授權與來源。

## 其他專案

PS5 Center 是範圍更廣的未來專案，具體方向和路線圖將共同完善；它不局限於遊戲與應用程式管理，目前尚未開發。PS5 Connector 探索透過連接器及現有方案擴展主機能力。NAS、共享和 USB 只是範例，並非固定範圍。目前未公布原型或日期。

## 支持專案

軟體免費提供。問題回報、翻譯、有紀錄的測試與自願支持可協助支付開發、AI 工具、測試與硬體費用。付款方式與公開聯絡資訊尚未確定。

## 開始使用

- [開始使用](../guides/01-start.zh-Hant.md)
- [日常備份與還原](../guides/02-everyday.zh-Hant.md)
- [目標位置與已配對主機](../guides/03-destinations.zh-Hant.md)
- [遊戲與傳輸](../guides/04-transfers.zh-Hant.md)
- [模擬器與ROM](../guides/05-emulators.zh-Hant.md)
- [設定與更新](../guides/06-settings.zh-Hant.md)
- [疑難排解](../guides/07-troubleshooting.zh-Hant.md)
- [開發者指南](../guides/08-developers.zh-Hant.md)
- [專案與支持](../guides/09-project.zh-Hant.md)
