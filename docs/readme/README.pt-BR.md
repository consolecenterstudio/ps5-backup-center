<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

## 3.2.0

- Nosso ShadowMount integrado (ShadowMountPlus 1.7, compilado por nós): quando o ShadowMountPlus instalado não está rodando, imagens e pastas de jogos são montadas e registradas mesmo assim — sem ganchos no sistema nem portas; o instalado tem prioridade assim que roda (Configurações › Montagem dos jogos). As configurações dele nunca são alteradas.
- “Instalar…” para um jogo que está em outro lugar (NAS, nuvem, outro PS5), a partir de Arquivos: escolha o PS5 de destino e o disco; cópia verificada, depois montagem ou instalação pelo console. O NAS e a nuvem são origens, nunca destinos.
- Web: “Instalar a partir deste PC…” (Arquivos) — um arquivo de jogo do PC (.pkg ou imagem) enviado ao PS5 e ao disco escolhidos, em partes, retomando após uma queda, depois montado ou instalado pelo console.
- Configurações › Ferramentas do console: os payloads do Payload Manager e os que estão rodando — iniciá-los ou pará-los por aqui (kstuff e nosso motor protegidos, ShadowMountPlus parado com segurança), e abrir o Payload Manager; não é mais preciso reiniciar o console para relançar uma ferramenta.
- Bibliotecas de jogos: uma pasta do NAS, de uma nuvem, de um disco USB ou do outro PS5, declarada em Arquivos; os jogos não instalados aparecem no Início e na Biblioteca («Para instalar»), prontos para instalar no PS5 e no disco escolhidos. Lida em segundo plano, nada ali é alterado. Cada .pkg é reconhecido pelo cabeçalho: jogo, atualização ou DLC (nunca confundido com o jogo: fica sob ele, instalável pelo console depois que o jogo estiver instalado), PS4 ou PS5, fake PKG ou pacote oficial; o Kraken só é oferecido para um jogo de PS5 em fake PKG; caso contrário, o motivo é informado.
- «Instalar…» de outro lugar: escolher o formato (a imagem, o .pkg do jogo iniciado pelo Kraken ou instalado pelo console ou a imagem convertida em .pkg e depois instalada), com o tempo estimado; depois uma única tarefa com etapas numeradas (cópia, verificação, conversão, instalação), cada uma com sua barra, velocidade e tempo restante. Pode ser interrompida em qualquer etapa.
- Em Downloads, “+ Adicionar um link” também aceita um link magnet ou um arquivo .torrent: você escolhe o disco onde guardar (o de Configurações › Rede vem primeiro), depois acompanha o andamento (fontes, velocidade, tempo restante) e pausa, retoma ou tira (TV e web).
- Downloads › Meus links: todos os links dados pelo console (BitTorrent, .zip do console, MEGA, OneDrive), com código QR, endereço e como pará-los. Os links MEGA e OneDrive ficam guardados ali.
- Planos: um save ou jogo que já está no destino (mesmo conteúdo, copiado por qualquer PS5) não é mais copiado de novo; o resumo diz “já em dia”. Nada é relido para saber isso.
- Downloads › “ORBIT E BLACKBOX”: o que o Orbit e o BlackBox baixaram no console (estado, disco, tamanho, arquivo não encontrado) e os arquivos temporários deles (arquivo compactado mantido, download em andamento, resto), lidos do lado deles sem alterar nada.
- “Instalar…” nas ferramentas de um jogo, em Pacotes .pkg e em Downloads › Orbit e BlackBox: montar onde está (colocado na pasta de jogos do disco, sem cópia, montado pelo ShadowMount), ou instalar em outro disco ou outro PS5 (cópia verificada, original mantido ou removido).
- “Instalar… › Montar onde está” de um .pkg Kraken: o jogo é declarado ao console e roda a partir do pacote, sem descompactar nada — pelo Kraken Mount se estiver instalado; senão, pela nossa versão integrada (o trabalho do Kraken Mount no nosso motor, com ampr-emu-ng, pgo_stub e dlc_emu compilados por nós; requer kstuff). Um jogo já assumido pelo ShadowMount ou instalado pelo console não é tocado. Primeira versão, para testar.

A aprovação de produção do Dropbox está pendente. A sincronização automática entre PS5 e a instalação em outro console são recursos iniciais com testes pendentes.

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Tela inicial" width="88%">
</p>

[Primeiros passos](../guides/01-start.pt-BR.md) · [Mídia](../media.md)

# PS5 Backup Center

**Baixe, instale e transfira seus jogos.** Receba jogos de um PS5 pareado na rede local. Faça backup, compare, sincronize e restaure seus saves entre consoles PS5, unidades USB, NAS e nuvem pela TV ou navegador. Gerencie aplicativos e DLCs de PS5 e saves de Switch e emuladores compatíveis.

[Novidades 3.2.0](https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.2.0)

**Sua coleção. Suas partidas. Seus backups.**

Identifique o que precisa de backup, compare as cópias existentes e organize as trocas entre dispositivos. PS5 Backup Center reúne inventário, backups, sincronização e transferências na TV e no navegador.

**3.0.0 · GPL-3.0-or-later · 19 · 8**

**Siga a gente** · [X @CCenterStudio](https://x.com/CCenterStudio) · [YouTube @ConsoleCenterStudio](https://www.youtube.com/@ConsoleCenterStudio) · [Reddit @ConsoleCenterStudio](https://www.reddit.com/user/ConsoleCenterStudio/)

Software livre. Sem assinatura.

## O que você pode fazer

- Explorar jogos, aplicativos e ROMs; escolher conteúdo completo, partidas, estados ou configurações compatíveis.
- Comparar backups, gerenciar versões e agendar planos com rotação opcional.
- Acompanhar tarefas no navegador enquanto o motor continua após fechar o aplicativo da TV.

## Biblioteca · Sincro

Backups armazenados e partidas ativas são diferentes. Escolha as cópias a manter entre PS5 ou com um NAS. Os IDs locais podem mudar entre consoles: a conta de destino importa. Switch ↔ Eden / ProsperoEden usa JKSV e ftpd no Switch. Aplicativos e configurações compatíveis têm regras próprias. Jogos completos usam Transferências.

## Novidades da 3.0

- Em dispositivos, adicione o NAS ou console e confira a identidade antes de salvar. No NAS, escolha o protocolo compatível, teste a conexão e selecione a pasta. Entre PS5, confirme o código de associação; no Switch prepare ftpd e uma exportação JKSV. FTP / FTPS, SMB e WebDAV são compatíveis. “Usado para” escolhe se o NAS serve para saves, sincro ou ambos.
- Para a nuvem, abra Dispositivos › Nuvem: escolha um serviço S3 (Amazon S3, Backblaze B2, Wasabi, Cloudflare R2, Scaleway, OVHcloud, Synology C2…) e digite as chaves — o QR code abre o mesmo formulário no seu celular para colá-las — ou pCloud, Koofr, Nextcloud com sua conta. Conecte o Google Drive e o OneDrive pessoal pelo celular. O acesso ao Dropbox é limitado durante o desenvolvimento.
- Teclado completo na tela.

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
