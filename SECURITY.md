# Security policy

PS5 Backup Center runs on your console with access to your saves, accounts and disks, and it serves a web UI on your local network. We take reports seriously.

## Supported versions

Include the app and engine versions in your report. Check the [Releases page](https://github.com/consolecenterstudio/ps5-backup-center/releases) for available updates.

## Reporting a vulnerability

**Please do not open a public issue.**

Use **Security → Report a vulnerability** when the private reporting form is available. Otherwise, ask a maintainer for a private contact method before sharing details.

Include the version, what an attacker can do, and the steps to reproduce. We will credit you in the release notes unless you prefer otherwise.

## In scope

- The engine's web server and API (port 52370): authentication, pairing code, token handling, path traversal in the file browser.
- Anything that could delete, overwrite or leak saves, backups or account data without the user's confirmation.
- Credentials of destinations (NAS / FTP passwords) and how they are stored or sent; FTPS / HTTPS certificate handling.
- The update mechanism (manifest, file verification, rollback).

## Out of scope

- Ways to run homebrew on a PS5 in the first place, and the security of the console's firmware.
- Third-party tools we credit (Garlic, Game Compressor, payload loaders…) — please report those upstream; tell us too if it affects us.

## Design notes

- The web UI is meant for your **local network only**. Do not expose port 52370 to the internet.
- A device must be paired with a 6-digit code shown on the TV before it can use the API.
- FTPS accepts self-signed LAN certificates and records their SHA-256 fingerprint; pinning with a warning on change is planned.
