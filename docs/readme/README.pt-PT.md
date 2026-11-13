<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Ecrã inicial" width="88%">
</p>

[Primeiros passos](../guides/01-start.pt-PT.md) · [Multimédia](../media.md)

# PS5 Backup Center

**A sua coleção. As suas partidas. As suas cópias de segurança.**

Identifique o que precisa de uma cópia de segurança, compare as cópias existentes e organize as trocas entre dispositivos. PS5 Backup Center reúne inventário, cópias, sincronização e transferências na televisão e no navegador.

**2.98.4 · GPL-3.0-or-later · 19 · 8**

Software livre. Sem subscrição.

## O que pode fazer

- Explorar jogos, aplicações e ROMs; escolher conteúdo completo, partidas, estados ou definições compatíveis.
- Comparar cópias guardadas, gerir versões e agendar planos com rotação opcional.
- Acompanhar tarefas no navegador enquanto o motor continua após fechar a aplicação da televisão.

## Biblioteca · Sincro

As cópias guardadas e as partidas ativas são distintas. Escolha as cópias a manter entre PS5 ou com um NAS. Os identificadores locais podem variar entre consolas: a conta de destino importa. Switch ↔ Eden / ProsperoEden utiliza JKSV e ftpd na Switch. Aplicações e definições compatíveis têm regras próprias. Jogos completos usam Transferências.

## Novidades da 2.98

- **A biblioteca da outra PS5**: percorra daqui os jogos, apps e gravações dela (TV e web) e faça cópias de segurança à distância.
- **Ficheiros**: copie, mova ou elimine qualquer ficheiro entre esta PS5, USB, o NAS, uma Switch e a outra PS5; eliminar pede várias confirmações.
- **Desinstalar um jogo**, aqui ou na outra PS5; as gravações são copiadas antes.
- **SMB 2/3** (Windows, Synology, QNAP), além de FTP/FTPS; **Églantine** (Switch 2); reiniciar a consola a partir da app.
- **Ligação pela Internet** opcional e cifrada com uma PS5 longe de casa (porta 52373); na rede local, porta 52370.

## Instalação

1. Transfira `PPSA99950.zip` da [versão mais recente](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) e verifique o SHA-256 (`SHA256SUMS`).
2. Descomprima e copie a pasta `PPSA99950` para `/data/homebrew/` na consola (FTP ou USB).
3. Abra o **PS5 Backup Center** a partir do seu lançador de homebrew: a app inicia o motor sozinha. Opcional: o `…-engine.elf` da versão inicia só o motor a partir de um carregador de ELF (porta 9021).

A app procura sozinha novas versões neste repositório, anuncia-as no Início e pode instalá-las, com regresso à versão anterior.

## TV / Web

- **Início**
- **Biblioteca**
- **Progresso**
- **Dados guardados**
- **Sincro**
- **Transferências**
- **Dispositivos**
- **Tarefas**
- **Definições**

## Definições

Escolha entre 19 idiomas e oito temas. Sons, música de fundo e volume são controlos separados. Reveja fontes, elementos ignorados, utilizadores e dispositivos antes de mudar associações. As novidades permitem reler as alterações. Os créditos incluem autores, licenças e fontes dos componentes.

## Outros projetos

PS5 Center é um projeto futuro mais amplo, com âmbito e roteiro a definir em conjunto; não se limita à gestão de jogos e aplicações e ainda não foi desenvolvido. PS5 Connector explora alargar as capacidades das consolas através de conectores e soluções existentes. NAS, partilhas e USB são exemplos, não um âmbito fechado. Não há protótipo nem data anunciados.

## Apoiar

O PS5 Backup Center é gratuito e continuará a sê-lo. Se a app o ajuda, pode apoiar o desenvolvimento (na app: o ♥ no canto superior direito). Cripto abaixo: envie só pela rede indicada. Presentes, Ko-fi, Patreon e objetivos: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## Primeiros passos

- [Primeiros passos](../guides/01-start.pt-PT.md)
- [Cópias e restauro no dia a dia](../guides/02-everyday.pt-PT.md)
- [Destinos e consolas associadas](../guides/03-destinations.pt-PT.md)
- [Jogos e transferências](../guides/04-transfers.pt-PT.md)
- [Emuladores e ROMs](../guides/05-emulators.pt-PT.md)
- [Definições e atualizações](../guides/06-settings.pt-PT.md)
- [Resolver problemas](../guides/07-troubleshooting.pt-PT.md)
- [Para programadores](../guides/08-developers.pt-PT.md)
- [Projeto e apoio](../guides/09-project.pt-PT.md)
