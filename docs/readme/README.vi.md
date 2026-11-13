<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Màn hình chính" width="88%">
</p>

[Bắt đầu](../guides/01-start.vi.md) · [Nội dung](../media.md)

# PS5 Backup Center

**Bộ sưu tập của bạn. Tiến trình của bạn. Bản sao lưu của bạn.**

Xác định nội dung cần sao lưu, so sánh các bản sao hiện có và tổ chức trao đổi giữa thiết bị. PS5 Backup Center kết hợp danh mục, sao lưu, đồng bộ và truyền dữ liệu trên TV và trình duyệt.

**2.98.4 · GPL-3.0-or-later · 19 · 8**

Phần mềm tự do. Không cần thuê bao.

## Bạn có thể làm gì

- Duyệt trò chơi, ứng dụng và ROM; chọn toàn bộ nội dung, dữ liệu lưu, trạng thái hoặc thiết lập được hỗ trợ.
- So sánh bản sao, quản lý phiên bản và lập lịch với tùy chọn luân phiên lưu giữ.
- Theo dõi tác vụ trong trình duyệt khi bộ máy vẫn chạy sau khi đóng ứng dụng TV.

## Thư viện · Đồng bộ

Bản sao lưu được cất giữ khác với dữ liệu lưu đang dùng để chơi. Chọn bản sao cần duy trì giữa các PS5 hoặc với NAS. ID người dùng cục bộ có thể khác nhau nên tài khoản nhận rất quan trọng. Switch ↔ Eden / ProsperoEden sử dụng JKSV và ftpd trên Switch. Ứng dụng và thiết lập có quy tắc so sánh riêng. Toàn bộ trò chơi dùng mục Truyền dữ liệu.

## Có gì mới ở 2.98

- **Thư viện của PS5 còn lại**: xem từ đây game, ứng dụng và save của nó (TV và web) và sao lưu từ xa.
- **Tệp**: sao chép, di chuyển hoặc xóa bất kỳ tệp nào giữa PS5 này, USB, NAS, Switch và PS5 còn lại; xóa cần nhiều lần xác nhận.
- **Gỡ cài đặt game**, tại đây hoặc trên PS5 còn lại; save được sao lưu trước.
- **SMB 2/3** (Windows, Synology, QNAP) bên cạnh FTP/FTPS; **Églantine** (Switch 2); khởi động lại máy từ ứng dụng.
- **Kết nối Internet** tùy chọn, được mã hóa, tới một PS5 ở xa (cổng 52373); trong mạng nội bộ dùng cổng 52370.

## Cài đặt

1. Tải `PPSA99950.zip` từ [phiên bản mới nhất](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) và kiểm tra SHA-256 (`SHA256SUMS`).
2. Giải nén và chép thư mục `PPSA99950` vào `/data/homebrew/` trên máy (FTP hoặc USB).
3. Mở **PS5 Backup Center** từ trình khởi chạy homebrew: ứng dụng tự khởi động engine. Tùy chọn: tệp `…-engine.elf` trong bản phát hành chỉ khởi động engine qua trình nạp ELF (cổng 9021).

Ứng dụng tự tìm phiên bản mới trong kho này, báo trên màn hình chính và có thể cài đặt, có thể quay lại phiên bản trước.

## TV / Web

- **Trang chủ**
- **Thư viện**
- **Tiến độ chơi**
- **Bản lưu**
- **Đồng bộ**
- **Chuyển dữ liệu**
- **Thiết bị**
- **Tác vụ**
- **Cài đặt**

## Cài đặt

Chọn trong 19 ngôn ngữ và tám giao diện. Hiệu ứng âm thanh, nhạc nền và âm lượng được điều khiển riêng. Kiểm tra nguồn, mục bỏ qua, người dùng và thiết bị trước khi đổi liên kết. Có thể đọc lại thay đổi trong mục tin mới. Lời cảm ơn gồm tác giả, giấy phép và nguồn thành phần.

## Các dự án khác

PS5 Center là dự án tương lai rộng hơn, có phạm vi và lộ trình sẽ được cùng xây dựng; không chỉ quản lý trò chơi và ứng dụng, và hiện chưa được phát triển. PS5 Connector nghiên cứu mở rộng khả năng của máy chơi game qua các trình kết nối và giải pháp hiện có. NAS, chia sẻ và USB chỉ là ví dụ, không phải phạm vi cố định. Chưa công bố nguyên mẫu hay ngày ra mắt.

## Ủng hộ

PS5 Backup Center miễn phí và sẽ luôn miễn phí. Nếu ứng dụng giúp ích cho bạn, bạn có thể ủng hộ việc phát triển (trong ứng dụng: biểu tượng ♥ ở góc trên bên phải). Crypto ở bên dưới: chỉ gửi trên mạng được ghi. Quà tặng, Ko-fi, Patreon và mục tiêu: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## Bắt đầu

- [Bắt đầu](../guides/01-start.vi.md)
- [Sao lưu và khôi phục hằng ngày](../guides/02-everyday.vi.md)
- [Đích và máy đã ghép đôi](../guides/03-destinations.vi.md)
- [Trò chơi và truyền dữ liệu](../guides/04-transfers.vi.md)
- [Trình giả lập và ROM](../guides/05-emulators.vi.md)
- [Cài đặt và cập nhật](../guides/06-settings.vi.md)
- [Khắc phục sự cố](../guides/07-troubleshooting.vi.md)
- [Dành cho nhà phát triển](../guides/08-developers.vi.md)
- [Dự án và hỗ trợ](../guides/09-project.vi.md)
