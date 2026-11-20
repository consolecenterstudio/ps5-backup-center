<p align="center">
  <img src="../assets/banner-ps5-backup-center.svg" alt="PS5 Backup Center" width="100%">
</p>

[English](../../README.md) · [Français](README.fr.md) · [Français (Canada)](README.fr-CA.md) · [Español](README.es.md) · [Español (Latinoamérica)](README.es-419.md) · [Português (Brasil)](README.pt-BR.md) · [Português (Portugal)](README.pt-PT.md) · [Deutsch](README.de.md) · [Italiano](README.it.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Türkçe](README.tr.md) · [Русский](README.ru.md) · [简体中文](README.zh-Hans.md) · [繁體中文](README.zh-Hant.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [Tiếng Việt](README.vi.md) · [العربية](README.ar.md)

## 3.2.0

- Nuestro ShadowMount integrado (ShadowMountPlus 1.7, compilado por nosotros): cuando el ShadowMountPlus instalado no está en marcha, las imágenes y carpetas de juegos se montan y registran igualmente — sin ganchos en el sistema ni puertos; el instalado tiene prioridad en cuanto se ejecuta (Ajustes › Montaje de juegos). Su configuración nunca se modifica.
- «Instalar…» de un juego que está en otro sitio (NAS, nube, otra PS5), desde Archivos: eliges la PS5 de destino y su disco; copia verificada y, después, montaje o instalación con la consola. El NAS y la nube son orígenes, nunca destinos.
- Web: «Instalar desde este PC…» (Archivos): un archivo de juego del PC (.pkg o imagen) enviado a la PS5 y al disco elegidos, por partes, con reanudación tras un corte, y después montado o instalado con la consola.
- Ajustes › Herramientas de la consola: los payloads de Payload Manager y los que están en marcha — iniciarlos o detenerlos desde aquí (kstuff y nuestro motor protegidos, ShadowMountPlus detenido limpiamente), y abrir Payload Manager; ya no hace falta reiniciar la consola para relanzar una herramienta.
- Bibliotecas de juegos: una carpeta del NAS, de una nube, de un disco USB o de la otra PS5, declarada desde Archivos; sus juegos no instalados aparecen en Inicio y Biblioteca («Para instalar»), listos para instalar en la PS5 y el disco elegidos. Se lee en segundo plano y no se modifica nada allí. Cada .pkg se reconoce por su encabezado: juego, actualización o DLC (nunca se toma por el juego: se ordena bajo él, instalable con la consola una vez instalado el juego), PS4 o PS5, fake PKG o paquete oficial; Kraken solo se ofrece para un juego de PS5 en fake PKG; si no, se indica el motivo.
- «Instalar…» desde otro lugar: elegir el formato (la imagen, el .pkg del juego iniciado por Kraken o instalado por la consola o la imagen convertida a .pkg y luego instalada), con el tiempo estimado; después una sola tarea con pasos numerados (copia, verificación, conversión, instalación), cada uno con su barra, su velocidad y el tiempo restante. Se puede detener en cualquier paso.
- En Descargas, «+ Añadir un enlace» acepta también un enlace magnet o un archivo .torrent: eliges el disco donde guardarlo (primero el de Ajustes › Red), luego sigues el avance (fuentes, velocidad, tiempo restante) y lo pausas, reanudas o quitas (TV y web).
- Descargas › Mis enlaces: todos los enlaces dados desde la consola (BitTorrent, .zip de la consola, MEGA, OneDrive), con su código QR, su dirección y la opción de detenerlos. Los enlaces MEGA y OneDrive se guardan ahí.
- Planes: una partida o un juego que ya está en el destino (mismo contenido, copiado por cualquier PS5) ya no se vuelve a copiar; el resumen dice «ya al día». No se vuelve a leer nada para saberlo.
- Descargas › «ORBIT Y BLACKBOX»: lo que Orbit y BlackBox descargaron en la consola (estado, disco, tamaño, archivo no encontrado) y sus archivos temporales (archivo comprimido conservado, descarga en curso, resto), leídos en su lado sin cambiar nada.
- «Instalar…» en las herramientas de un juego, Paquetes .pkg y Descargas › Orbit y BlackBox: montarlo donde está (colocado en la carpeta de juegos de su disco, sin copia, montado por ShadowMount), o instalarlo en otro disco u otra PS5 (copia verificada, original conservado o retirado).
- «Instalar… › Montarlo donde está» de un .pkg Kraken: el juego se declara a la consola y se ejecuta desde su paquete, sin descomprimir nada — con Kraken Mount si está instalado; si no, con nuestra versión integrada (el trabajo de Kraken Mount en nuestro motor, con ampr-emu-ng, pgo_stub y dlc_emu compilados por nosotros; requiere kstuff). Un juego ya tomado por ShadowMount o instalado por la consola no se toca. Primera versión, para probar.

La aprobación de producción de Dropbox está pendiente. La sincronización automática entre PS5 y la instalación en otra consola son funciones iniciales con pruebas pendientes.

<p align="center">
  <img src="../assets/screenshots/en-oled-home.png" alt="PS5 Backup Center — Pantalla de inicio" width="88%">
</p>

[Primeros pasos](../guides/01-start.es.md) · [Medios](../media.md)

# PS5 Backup Center

**Descarga, instala y transfiere tus juegos.** Recibe juegos de una PS5 emparejada en tu red local. Guarda copias, compara, sincroniza y restaura partidas entre tus PS5, unidades USB, NAS y la nube, desde el televisor o el navegador. Gestiona aplicaciones y DLC de PS5 y partidas de Switch y emuladores compatibles.

[Novedades 3.2.0](https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.2.0)

**Tu colección. Tus partidas. Tus copias de seguridad.**

Detecta qué necesita una copia de seguridad, compara las copias existentes y organiza los intercambios entre dispositivos. PS5 Backup Center reúne inventario, copias, sincronización y transferencias en el televisor y el navegador.

**3.0.0 · GPL-3.0-or-later · 19 · 8**

**Síguenos** · [X @CCenterStudio](https://x.com/CCenterStudio) · [YouTube @ConsoleCenterStudio](https://www.youtube.com/@ConsoleCenterStudio) · [Reddit @ConsoleCenterStudio](https://www.reddit.com/user/ConsoleCenterStudio/)

Software libre. Sin suscripción.

## Qué puedes hacer

- Explorar juegos, aplicaciones y ROMs; elegir el contenido completo, partidas, estados o ajustes compatibles.
- Comparar copias guardadas, gestionar versiones y programar planes con rotación opcional.
- Seguir tareas en el navegador mientras el motor continúa tras cerrar la aplicación del televisor.

## Biblioteca · Sincro

Las copias almacenadas y las partidas activas son distintas. Elige qué copias mantener entre PS5 o con un NAS. Los identificadores locales pueden cambiar entre consolas: importa la cuenta de destino. Switch ↔ Eden / ProsperoEden usa JKSV y ftpd en Switch. Aplicaciones y ajustes compatibles tienen reglas propias. Los juegos completos pasan por Transferencias.

## Novedades de 3.0

- En dispositivos, añade el NAS o la consola y comprueba la identidad antes de guardar. Para un NAS, elige el protocolo compatible, prueba la conexión y selecciona la carpeta. Para dos PS5, confirma el código de asociación; para Switch prepara ftpd y un exportado JKSV. FTP / FTPS, SMB y WebDAV son compatibles. «Usado para» elige si el NAS guarda partidas, sincro o ambas cosas.
- Para la nube, abre Dispositivos › Nube: elige un servicio S3 (Amazon S3, Backblaze B2, Wasabi, Cloudflare R2, Scaleway, OVHcloud, Synology C2…) e introduce sus claves —el código QR abre el mismo formulario en tu móvil para que las pegues— o pCloud, Koofr, Nextcloud con tu cuenta. Conecta Google Drive y una cuenta personal de OneDrive desde el móvil. El acceso a Dropbox es limitado durante el desarrollo.
- Teclado completo en pantalla.

## Novedades en 2.98

- **La biblioteca de la otra PS5**: explora desde aquí sus juegos, apps y partidas (TV y web) y haz copias de seguridad a distancia.
- **Archivos**: copia, mueve o elimina cualquier archivo entre esta PS5, USB, el NAS, una Switch y la otra PS5; eliminar pide varias confirmaciones.
- **Desinstalar un juego**, aquí o en la otra PS5; las partidas se guardan antes.
- **SMB 2/3** (Windows, Synology, QNAP), además de FTP/FTPS; **Églantine** (Switch 2); reiniciar la consola desde la app.
- **Enlace por Internet** opcional y cifrado con una PS5 lejos de casa (puerto 52373); en la red local, puerto 52370.

## Instalación

1. Descarga `PPSA99950.zip` de la [última versión](https://github.com/consolecenterstudio/ps5-backup-center/releases/latest) y comprueba su SHA-256 (`SHA256SUMS`).
2. Descomprímelo y copia la carpeta `PPSA99950` en `/data/homebrew/` de la consola (FTP o USB).
3. Abre **PS5 Backup Center** desde tu lanzador de homebrew: la app inicia su motor sola. Opcional: el `…-engine.elf` de la versión inicia solo el motor desde un cargador de ELF (puerto 9021).

La app busca sola las nuevas versiones en este repositorio, las anuncia en Inicio y puede instalarlas, con vuelta a la versión anterior.

## TV / Web

- **Inicio**
- **Biblioteca**
- **Progreso**
- **Partidas**
- **Sincro**
- **Transferencias**
- **Dispositivos**
- **Tareas**
- **Ajustes**

## Ajustes

Elige entre 19 idiomas y ocho temas. Sonidos, música de fondo y volumen son controles separados. Revisa fuentes, elementos ignorados, usuarios y dispositivos antes de cambiar una asociación. La página de novedades permite releer los cambios. En créditos se incluyen autores, licencias y fuentes de los componentes.

## Los otros proyectos

PS5 Center es un proyecto futuro más amplio, cuyo alcance y hoja de ruta se definirán juntos; no se limita a gestionar juegos y aplicaciones y aún no está desarrollado. PS5 Connector explora ampliar las capacidades de las consolas mediante conectores y soluciones existentes. NAS, recursos compartidos y USB son ejemplos, no un alcance fijo. No se anuncia prototipo ni fecha.

## Apoyar

PS5 Backup Center es gratuito y lo seguirá siendo. Si la app te ayuda, puedes apoyar su desarrollo (en la app: el ♥ arriba a la derecha). Cripto abajo: envía solo por la red indicada. Regalos, Ko-fi, Patreon y objetivos: [Support the developer](../../README.md#support-the-developer).

| Coin | Network | Address |
|---|---|---|
| BTC | Bitcoin | `16J2KqxzzAwqrbdtsBAXLcfXySH3WEqvas` |
| ETH | Ethereum (ERC20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| BNB | BNB Smart Chain (BEP20) | `0xc8bdccce6d52570e059f58f0f4c5c6199bee298d` |
| SOL | Solana | `EyiGLmxw1b1AZxhVmpfP4cXYGTDuYJaWXonvKww2hDaD` |

## Primeros pasos

- [Primeros pasos](../guides/01-start.es.md)
- [Copias y restauración diaria](../guides/02-everyday.es.md)
- [Destinos y consolas vinculadas](../guides/03-destinations.es.md)
- [Juegos y transferencias](../guides/04-transfers.es.md)
- [Emuladores y ROMs](../guides/05-emulators.es.md)
- [Ajustes y actualizaciones](../guides/06-settings.es.md)
- [Resolver problemas](../guides/07-troubleshooting.es.md)
- [Para desarrolladores](../guides/08-developers.es.md)
- [Proyecto y apoyo](../guides/09-project.es.md)
