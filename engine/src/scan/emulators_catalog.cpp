// Catalogue des émulateurs connus, sous forme de données. Peut être remplacé ou complété sans
// recompiler par /data/pbc-backup-center/emulators.json (mêmes champs).
//
// Champs :
//   id, name, title_ids        identité de l'émulateur
//   data                       dossier de données sur la console (sert aussi à détecter l'installation)
//   config_files               fichiers de réglages (composant « configuration » de l'émulateur)
//   config_roots               où le fichier de réglages dit que sont les jeux :
//                              {file, format: ini|json|xml|tsv, key, column, suffix, relative_to_usb}
//   rom_roots                  dossiers par défaut ; {usb} = racine de chaque disque USB ;
//                              un « * » final = un sous-dossier par système (MegaDrive, SegaCD…)
//   platform                   plateforme affichée (ou nom du sous-dossier si rom_roots se termine par *)
//   rom_ext                    extensions des fichiers de jeu
//   saves, states              dossiers de sauvegardes et d'états (associés par nom de jeu)
//   switch_saves               modèle de chemin des sauvegardes Switch ({tid} = identifiant du titre)
//   covers                     dossiers de jaquettes (associées par nom de jeu, ou par un fichier
//                              « <id>.name » qui donne le nom du jeu de « <id>.tga »)
// « ProsperoEden (/data/eden) » : ancien dossier de données de ProsperoEden, pas une appli à part.
namespace pbc::scan {
extern const char* const emulators_catalog_json;
const char* const emulators_catalog_json = R"json([
 {"id":"prosperoeden","name":"ProsperoEden","title_ids":["PPSA99008"],"platform":"Switch",
  "data":"/data/prosperoeden","config_files":["/data/prosperoeden/config/prosperoeden.json"],
  "config_roots":[{"file":"/data/prosperoeden/config/prosperoeden.json","format":"json","key":"game_files","suffix":"/roms"}],
  "rom_roots":["/data/prosperoeden/roms"],"rom_ext":["nsp","xci","nsz","xcz"],
  "switch_saves":"/data/prosperoeden/user/nand/user/save/0000000000000000/*/{tid}",
  "covers":["/data/prosperoeden/covers"]},
 {"id":"eden","name":"ProsperoEden (/data/eden)","title_ids":[],"platform":"Switch",
  "data":"/data/eden","config_files":["/data/eden/config/prosperoeden.json"],
  "config_roots":[{"file":"/data/eden/config/prosperoeden.json","format":"json","key":"game_files","suffix":"/roms"}],
  "rom_roots":["/data/eden/roms"],"rom_ext":["nsp","xci","nsz","xcz"],
  "switch_saves":"/data/eden/user/nand/user/save/0000000000000000/*/{tid}",
  "covers":["/data/eden/covers"]},
 {"id":"eglantine","name":"Églantine","title_ids":["PPSA98515"],"platform":"Switch 2",
  "data":"/data/eglantineps5","config_files":["/data/eglantineps5/config/eglantineps5.json"],
  "config_roots":[{"file":"/data/eglantineps5/config/eglantineps5.json","format":"json","key":"game_files","suffix":"/roms"}],
  "rom_roots":["/data/eglantineps5/roms"],"rom_ext":["nup","nsp","xci","nsz","xcz"],
  "switch_saves":"/data/eglantineps5/user/nand/user/save/0000000000000000/*/{tid}",
  "covers":["/data/eglantineps5/covers"]},
 {"id":"ps5sx2","name":"PS5SX2","title_ids":["PPSA99203"],"platform":"PlayStation 2",
  "data":"/data/PCSX2","config_files":["/data/PCSX2/gs.ini"],
  "config_roots":[{"file":"/data/PCSX2/gs.ini","format":"ini","key":"GameFolders"}],
  "rom_roots":["/data/PCSX2/games","{usb}/ps2"],"rom_ext":["iso","chd","cso","bin","cue"],
  "saves":["/data/PCSX2/memcards"],"states":["/data/PCSX2/sstates"],"covers":["/data/PCSX2/covers"]},
 {"id":"swanstation","name":"SwanStation","title_ids":["PPSA98510","PPSA99248"],"platform":"PlayStation",
  "data":"/data/SwanStationPS5","config_files":["/data/SwanStationPS5/SwanStationPS5.ini"],
  "config_roots":[{"file":"/data/SwanStationPS5/SwanStationPS5.ini","format":"ini","key":"usb_folder","relative_to_usb":true}],
  "rom_roots":["/data/SwanStationPS5/games"],"rom_ext":["cue","chd","pbp","m3u","iso","bin"],
  "saves":["/data/SwanStationPS5/saves"],"states":["/data/SwanStationPS5/states"],"covers":["/data/SwanStationPS5/covers/default"]},
 {"id":"psxs5","name":"PSXS5","title_ids":["PPSA97510"],"platform":"PlayStation",
  "data":"/data/PSXS5","config_files":["/data/PSXS5/library.txt"],
  "config_roots":[{"file":"/data/PSXS5/library.txt","format":"tsv","column":3}],
  "rom_roots":["/data/PSXS5/games"],"rom_ext":["cue","chd","pbp","m3u","iso","bin"],
  "saves":["/data/PSXS5/saves"],"states":["/data/PSXS5/states"],"covers":["/data/PSXS5/covers"]},
 {"id":"psp5","name":"PSP5","title_ids":["PPSA99131"],"platform":"PSP",
  "data":"/data/PSP","config_files":["/data/PSP/SYSTEM/ppsspp.ini","/data/PSP/SYSTEM/controls.ini"],
  "rom_roots":["/data/PSP/GAME","{usb}/PSP/GAME"],"rom_ext":["iso","cso","pbp","chd"],
  "saves":["/data/PSP/SAVEDATA","{usb}/PSP/SAVEDATA"],"states":["/data/PSP/PPSSPP_STATE","{usb}/PSP/PPSSPP_STATE"]},
 {"id":"mupen64plus","name":"Mupen64Plus","title_ids":["PPSA99064"],"platform":"Nintendo 64",
  "data":"/data/mupen64plus","config_files":["/data/mupen64plus/mupen64plus-ps5.ini","/data/mupen64plus/config/mupen64plus.cfg"],
  "rom_roots":["/data/mupen64plus/roms","{usb}/mupen64plus/roms"],"rom_ext":["z64","n64","v64","zip","7z"],
  "saves":["/data/mupen64plus/saves"],"states":["/data/mupen64plus/states"],"covers":["/data/mupen64plus/covers"]},
 {"id":"genplus","name":"Genesis Plus GX","title_ids":["PPSA99921"],
  "data":"/data/genplus","config_files":["/data/genplus/genplus-ps5.ini","/data/genplus/shares.txt"],
  "rom_roots":["/data/genplus/roms/*","{usb}/genplus/roms/*"],"rom_ext":["md","gen","bin","smd","sms","gg","sg","cue","chd","iso","zip","7z"],
  "saves":["/data/genplus/saves"],"states":["/data/genplus/states"],"covers":["/data/genplus/covers"]},
 {"id":"snes9x","name":"Snes9x","title_ids":["PPSA99922"],"platform":"Super Nintendo",
  "data":"/data/snes9x","config_files":["/data/snes9x/snes9x-ps5.ini"],
  "rom_roots":["/data/snes9x/roms","{usb}/snes9x/roms"],"rom_ext":["sfc","smc","fig","swc","zip","7z"],
  "saves":["/data/snes9x/saves"],"states":["/data/snes9x/states"],"covers":["/data/snes9x/covers"]},
 {"id":"beetlepce","name":"Beetle PCE","title_ids":["PPSA99925"],
  "data":"/data/pce","config_files":[],
  "rom_roots":["/data/pce/roms/*","{usb}/pce/roms/*"],"rom_ext":["pce","sgx","cue","chd","zip","7z"],
  "saves":["/data/pce/saves"],"states":["/data/pce/states"]},
 {"id":"fbneo","name":"FBNeo","title_ids":["PPSA99924"],"platform":"Arcade",
  "data":"/data/fbneo","config_files":[],
  "rom_roots":["/data/fbneo/roms","{usb}/fbneo/roms"],"rom_ext":["zip","7z"],
  "saves":["/data/fbneo/saves"],"states":["/data/fbneo/states"]},
 {"id":"stella","name":"Stella","title_ids":[],
  "data":"/data/stella","config_files":[],
  "rom_roots":["/data/stella/roms/*","{usb}/stella/roms/*"],"rom_ext":["a26","a52","a78","lnx","bin","zip"],
  "saves":["/data/stella/saves"],"states":["/data/stella/states"]},
 {"id":"ps64","name":"PS64","title_ids":["PPSA06464"],"platform":"Commodore 64",
  "data":"/data/ps64","config_files":["/data/ps64/ps64.cfg"],
  "config_roots":[{"file":"/data/ps64/ps64.cfg","format":"ini","key":"games-dir"},{"file":"/data/ps64/ps64.cfg","format":"ini","key":"carts-dir"}],
  "rom_roots":[],"rom_ext":["d64","t64","prg","crt","tap","g64","zip"]},
 {"id":"psflycast","name":"PSFlyCast","title_ids":["PPSA99247"],"platform":"Dreamcast",
  "data":"/data/flycast","config_files":[],
  "rom_roots":["{usb}/Dreamcast"],"rom_ext":["cue","gdi","chd","cdi","m3u"],
  "saves":["/data/flycast/data"],"states":["/data/flycast/data/savestates"]},
 {"id":"porpoise","name":"Porpoise","title_ids":["PPSA99764"],"platform":"GameCube / Wii",
  "data":"/data/porpoise","config_files":["/data/porpoise/settings.ini","/data/porpoise/options.ini"],
  "rom_roots":["/data/porpoise/games"],"rom_ext":["iso","gcm","rvz","wbfs","ciso","gcz"],
  "saves":["/data/porpoise/saves"],"covers":["/data/porpoise/covers"]},
 {"id":"cemu","name":"PS5-ECAM (Wii U)","title_ids":["PPSA99360"],"platform":"Wii U",
  "data":"/data/cemu","config_files":["/data/cemu/settings.xml","/data/ps5cemu/ps5cemu.json"],
  "config_roots":[{"file":"/data/cemu/settings.xml","format":"xml","key":"Entry"},{"file":"/data/ps5cemu/ps5cemu.json","format":"json","key":"gamesFolder"}],
  "rom_roots":["/data/cemu/games"],"rom_ext":["wud","wux","wua","rpx"],
  "saves":["/data/cemu/mlc01/usr/save"]},
 {"id":"azahar","name":"PS5-ECAM (3DS)","title_ids":[],"platform":"Nintendo 3DS",
  "data":"/data/azahar","config_files":["/data/ps5cemu/ps5cemu.json"],
  "config_roots":[{"file":"/data/ps5cemu/ps5cemu.json","format":"json","key":"n3ds.gamesFolder"}],
  "rom_roots":["/data/azahar/games"],"rom_ext":["3ds","cci","cia","cxi"],
  "saves":["/data/azahar/sdmc"]},
 {"id":"melonds","name":"PS5-ECAM (DS)","title_ids":[],"platform":"Nintendo DS",
  "data":"/data/melonds","config_files":[],
  "rom_roots":["/data/melonds/games"],"rom_ext":["nds","zip"],
  "saves":["/data/melonds/saves"],"states":["/data/melonds/states"]},
 {"id":"ps5x360","name":"PS5X360","title_ids":["PPSA50011"],"platform":"Xbox 360",
  "data":"/data/ps5x360","config_files":[],
  "rom_roots":["{usb}/xbox360"],"rom_ext":["iso","xex","zar"]}
])json";
}  // namespace pbc::scan
