<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — 主屏幕" width="88%">
</p>

[开始使用](../guides/01-start.zh-Hans.md) · [媒体](../media.md)

# PS5 Backup Center

**你的游戏收藏。你的游戏进度。你的备份。**

找出需要备份的内容，比较已有副本，并安排设备之间的数据交换。PS5 Backup Center 在电视与浏览器中整合内容清单、备份、同步和传输。

**2.99.5 · GPL-3.0-or-later · 19 · 8**

自由软件，无需订阅。

## 你可以做什么

- 浏览游戏、应用和 ROM；选择完整内容、游戏存档、即时存档或受支持的设置。
- 比较保存的副本、管理版本，并创建可选轮换的定时计划。
- 电视应用关闭后，引擎继续运行，你仍可在浏览器中跟踪任务。

## 游戏库 · 同步

保存的备份副本与当前使用的游戏存档不同。选择在两台 PS5 之间或与 NAS 保持一致的副本。本地用户 ID 可能不同，因此必须确认目标账户。Switch ↔ Eden / ProsperoEden 在 Switch 端使用 JKSV 和 ftpd。应用与受支持的设置有各自的比较规则。完整游戏通过“传输”处理。

## 2.98 新功能

- **另一台 PS5 的游戏库**：在这里浏览它的游戏、应用和存档（电视和网页），并远程备份。
- **文件**：在本机 PS5、USB、NAS、Switch 和另一台 PS5 之间复制、移动或删除任意文件；删除需要多次确认。
- **卸载游戏**，在本机或另一台 PS5 上；会先备份存档。
- 除 FTP/FTPS 外支持 **SMB 2/3**（Windows、Synology、QNAP）；**Églantine**（Switch 2）；在应用内重启主机。
- 可选的加密**互联网连接**，连接不在家中的 PS5（端口 52373）；局域网使用端口 52370。

## 安装

1. 从[最新版本](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest)下载 `PPSA99950.zip`，并核对 SHA-256（`SHA256SUMS`）。
2. 解压后将 `PPSA99950` 文件夹复制到主机的 `/data/homebrew/`（FTP 或 USB）。
3. 从你的自制软件启动器打开 **PS5 Backup Center**：应用会自行启动引擎。可选：版本附带的 `…-engine.elf` 可通过 ELF 加载器（端口 9021）单独启动引擎。

应用会自动在本仓库查找新版本，在主页提示并可安装，也可退回上一版本。

## TV / Web

- **首页**
- **游戏库**
- **游戏进度**
- **存档**
- **同步**
- **传输**
- **设备**
- **任务**
- **设置**

## 设置

可选择19种语言与八种主题。界面音效、背景音乐和音量分别控制。更改关联前检查来源、忽略项目、用户与设备。更新说明可重新阅读各版本变更，致谢包含组件作者、许可证和来源。

## 其他项目

PS5 Center 是范围更广的未来项目，具体方向和路线图将共同完善；它不局限于游戏与应用管理，目前尚未开发。PS5 Connector 探索通过连接器及现有方案扩展主机能力。NAS、共享和 USB 只是示例，并非固定范围。目前未公布原型或日期。

## 支持项目

PS5 Backup Center 免费，并将一直免费。如果它帮到了你，可以支持它的开发（应用内：右上角的 ♥）。加密货币见下表：只在标明的网络上发送。礼物、Ko-fi、Patreon 和项目目标：[Support the developer](../../README.md#support-the-developer)。

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## 开始使用

- [开始使用](../guides/01-start.zh-Hans.md)
- [日常备份与恢复](../guides/02-everyday.zh-Hans.md)
- [目标位置与已配对主机](../guides/03-destinations.zh-Hans.md)
- [游戏与传输](../guides/04-transfers.zh-Hans.md)
- [模拟器与ROM](../guides/05-emulators.zh-Hans.md)
- [设置与更新](../guides/06-settings.zh-Hans.md)
- [故障排查](../guides/07-troubleshooting.zh-Hans.md)
- [开发者指南](../guides/08-developers.zh-Hans.md)
- [项目与支持](../guides/09-project.zh-Hans.md)
