<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Tela inicial" width="88%">
</p>

[Primeiros passos](../guides/01-start.pt-BR.md) · [Mídia](../media.md)

# PS5 Backup Center

**Sua coleção. Suas partidas. Seus backups.**

Identifique o que precisa de backup, compare as cópias existentes e organize as trocas entre dispositivos. PS5 Backup Center reúne inventário, backups, sincronização e transferências na TV e no navegador.

**2.99.5 · GPL-3.0-or-later · 19 · 8**

Software livre. Sem assinatura.

## O que você pode fazer

- Explorar jogos, aplicativos e ROMs; escolher conteúdo completo, partidas, estados ou configurações compatíveis.
- Comparar backups, gerenciar versões e agendar planos com rotação opcional.
- Acompanhar tarefas no navegador enquanto o motor continua após fechar o aplicativo da TV.

## Biblioteca · Sincro

Backups armazenados e partidas ativas são diferentes. Escolha as cópias a manter entre PS5 ou com um NAS. Os IDs locais podem mudar entre consoles: a conta de destino importa. Switch ↔ Eden / ProsperoEden usa JKSV e ftpd no Switch. Aplicativos e configurações compatíveis têm regras próprias. Jogos completos usam Transferências.

## Novidades da 2.98

- **A biblioteca do outro PS5**: navegue daqui pelos jogos, apps e saves dele (TV e web) e faça backup a distância.
- **Arquivos**: copie, mova ou exclua qualquer arquivo entre este PS5, USB, o NAS, um Switch e o outro PS5; excluir pede várias confirmações.
- **Desinstalar um jogo**, aqui ou no outro PS5; os saves são copiados antes.
- **SMB 2/3** (Windows, Synology, QNAP), além de FTP/FTPS; **Églantine** (Switch 2); reiniciar o console pelo app.
- **Conexão pela Internet** opcional e criptografada com um PS5 longe de casa (porta 52373); na rede local, porta 52370.

## Instalação

1. Baixe `PPSA99950.zip` da [versão mais recente](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) e confira o SHA-256 (`SHA256SUMS`).
2. Descompacte e copie a pasta `PPSA99950` para `/data/homebrew/` no console (FTP ou USB).
3. Abra o **PS5 Backup Center** pelo seu inicializador de homebrew: o app inicia o motor sozinho. Opcional: o `…-engine.elf` da versão inicia só o motor por um carregador de ELF (porta 9021).

O app procura sozinho novas versões neste repositório, avisa no Início e pode instalá-las, com volta à versão anterior.

## TV / Web

- **Início**
- **Biblioteca**
- **Progresso**
- **Saves**
- **Sincro**
- **Transferências**
- **Dispositivos**
- **Tarefas**
- **Configurações**

## Configurações

Escolha entre 19 idiomas e oito temas. Sons, música de fundo e volume são controles separados. Revise fontes, itens ignorados, usuários e dispositivos antes de mudar associações. As novidades permitem reler as alterações. Os créditos incluem autores, licenças e fontes dos componentes.

## Outros projetos

PS5 Center é um projeto futuro mais amplo, com escopo e roteiro a definir em conjunto; não se limita à gestão de jogos e aplicativos e ainda não foi desenvolvido. PS5 Connector explora ampliar os recursos dos consoles com conectores e soluções existentes. NAS, compartilhamentos e USB são exemplos, não um escopo fechado. Não há protótipo ou data anunciados.

## Apoiar

O PS5 Backup Center é gratuito e continuará sendo. Se o app ajuda você, você pode apoiar o desenvolvimento (no app: o ♥ no canto superior direito). Cripto abaixo: envie só pela rede indicada. Presentes, Ko-fi, Patreon e metas: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## Primeiros passos

- [Primeiros passos](../guides/01-start.pt-BR.md)
- [Backups e restauração no dia a dia](../guides/02-everyday.pt-BR.md)
- [Destinos e consoles associados](../guides/03-destinations.pt-BR.md)
- [Jogos e transferências](../guides/04-transfers.pt-BR.md)
- [Emuladores e ROMs](../guides/05-emulators.pt-BR.md)
- [Configurações e atualizações](../guides/06-settings.pt-BR.md)
- [Resolver problemas](../guides/07-troubleshooting.pt-BR.md)
- [Para desenvolvedores](../guides/08-developers.pt-BR.md)
- [Projeto e apoio](../guides/09-project.pt-BR.md)
