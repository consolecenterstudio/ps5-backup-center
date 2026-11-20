# fpkg — provenance (paquets FPKG : créer, compresser, vérifier)

- Code : `ps5upload-fpkg` et `ps5upload-pkg`, tels que livrés par **Dump Forge v0.5.6** (quer3q),
  https://github.com/quer3q/ps5-dump-forge, commit figé `2dc0a29071fd909e30b00bce34e2cfe28ac05767`, dossier `vendor/`.
- Origine de ces deux crates : **PS5Upload v7.0.22** (phantomptr), https://github.com/phantomptr/ps5upload, commit `01afdf8`,
  plus la série de correctifs de Dump Forge (`vendor/patches`, détaillée dans `README-dump-forge-vendor.md`).
- Licence : GPL-3.0-or-later (`LICENSE-ps5upload`, `LICENSE-ps5-dump-forge`). Clés de paquet publiques reprises de
  LibProsperoPkg (voir `ps5upload-fpkg/src/keys.rs`). Aucune clé privée.
- Rôle chez nous : créer un `.pkg` (compression Kraken, encodeur libre en Rust) depuis un jeu en dossier ou une image
  (.exfat, .ffpkg, .ffpfsc) et le vérifier. Appelé par notre façade `engine/fpkg/pbc-fpkg` (bibliothèque statique liée au
  moteur) — aucune appli, aucun port. Plan : docs/PLAN-paquets-fpkg.md.

## Règles
- Fichiers copiés **sans modification**. Nos adaptations vivent dans `engine/fpkg/` (façade, espace de travail Cargo).
- Compilation PS5 : chaîne Rust 1.100.0 (celle que Dump Forge épingle), `-Z build-std`, la std corrigée pour FreeBSD 11
  (`prepare-rust-std.py` de ps5-ai-cli, une ligne), cible `engine/fpkg/x86_64-ps5-freebsd.json` (de ps5-ai-cli).
- Mise à jour volontaire uniquement : diff relu, tests (Mac et console), puis mise à jour de ce fichier.

## Empreintes (SHA-256)
- `LICENSE-ps5-dump-forge` 3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986
- `LICENSE-ps5upload` 3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986
- `README-dump-forge-vendor.md` c6aec943989ff34f2cf5ad27d06c2f9639cb6e2789efebd569a61fcd40f015db
- `ps5upload-fpkg/Cargo.toml` b36e988d35c49ec5e9d822f36b5d717d791d46e9eacc2a2b37e160f8fe1a7117
- `ps5upload-fpkg/examples/cblock_dump.rs` 6bb2ec60f067f3841e909ed323568a3e6aa3fcbb563e326fb8deac6bcf66266c
- `ps5upload-fpkg/examples/ffpfsc_wrap.rs` da61a6e7ab786d67be748c11b814d9c47ea477f7013374cb6d661d9d2cbb81e7
- `ps5upload-fpkg/examples/fpkg_build.rs` a440d69047e8512104ec51a35349b094f18d315d111e089128aea34ece518c53
- `ps5upload-fpkg/examples/fpkg_dump_inner.rs` c5f55e9976249e026fbe7c83c5985f4710b5d46f92645fe50354d7ee5627b577
- `ps5upload-fpkg/examples/fpkg_verify.rs` d8b369f29122dd3aa1994861f17f71380896b5444688b66a555b9a8191902178
- `ps5upload-fpkg/examples/naps_dump.rs` 257d15aa97b30ec62729dde2467b732a40923c528fd79b2aeab9d221e941fc53
- `ps5upload-fpkg/examples/naps_probe.rs` 96e62621c79f181a8316a7fc883a30a522dddd52c710b32a5bdbbd012acf2457
- `ps5upload-fpkg/examples/outer_blocks.rs` 4453811bce17a0d63c3a8ae2994a023b099b8c0be9bb33a951bc8d5b5e6d1b9b
- `ps5upload-fpkg/src/ampr_index.rs` bb20726388694811a75f70eee9e996ee83e33e0f356c25afd995012a120b74dd
- `ps5upload-fpkg/src/ampr_pack/config.rs` eef4f1fd9466748057cae4b9ea398a5e05494b1a53f473e1ec65a91f0b21e516
- `ps5upload-fpkg/src/ampr_pack/format.rs` 764469e47c0cc1c39c78a6f25a3ccfa00d577e4dfd0a4f7aa96006d2e481b555
- `ps5upload-fpkg/src/ampr_pack/glob.rs` 3aea9327c3297d1ed88b435e38e056e8681ad918c8169d8d1faa3251a0d73e91
- `ps5upload-fpkg/src/ampr_pack/image.rs` a258c892fd68abaa9376d09500aa32427e9820c1cde70a6c43f670d0adeb627e
- `ps5upload-fpkg/src/ampr_pack/lz4.rs` ade3fc479334a5d8fa3f1e731f913fb2ece44e271e8ea9f651db0b9e824b543e
- `ps5upload-fpkg/src/ampr_pack/mod.rs` de8829a028dec393b078544b5edc28157f8d9317487317a38349df5731eebdb6
- `ps5upload-fpkg/src/ampr_pack/pack.rs` 0a124c0d383fed6d47055a7286b8ed2bf502cfbbc41d5a6990a35c43992a2bf7
- `ps5upload-fpkg/src/ampr_pack/profile_gen.rs` e1a8e951c3e700fe3388027027a29dc78c95cbe48df0ddc127ad38c33b8e1f13
- `ps5upload-fpkg/src/ampr_pack/read.rs` d2aa00efbe48330ec00062c5ac641975e72d770b5e19a83892db64314d13fc84
- `ps5upload-fpkg/src/build.rs` 235ed4af8fe17832cf6d0a07063cf76868e64efcfc594d73ea46df5a918e2c3e
- `ps5upload-fpkg/src/cnt.rs` e773bacd4b2a02745c26406f11b4e868a1ee34186e12dc13105d0517caa45987
- `ps5upload-fpkg/src/cnt_write.rs` 43191f8cbac53fb9990f0a131b19ba197372f105503341850259d19f85e49aac
- `ps5upload-fpkg/src/crypto.rs` a75ea30736e94b131fce1d2b95150dd7711de6361aa0c92a56684b71d17e23ab
- `ps5upload-fpkg/src/exfat.rs` 94ed9a3e7b1e803be1ebbd2533e6e9bbca3cfc04d7ab111c70708f239ee5428b
- `ps5upload-fpkg/src/exfat_upcase.bin` 8344f27a410a16df14ad98decde32b48c4db0b8e7fa8b9dc4394b58ced972f11
- `ps5upload-fpkg/src/exfat_write.rs` 5ff50a170187df76f7117ee3aa9b573631ae57bd5d03a7d25dcf566c53e40fc0
- `ps5upload-fpkg/src/ffpfsc.rs` c17094e488f9dff146ce03b682d66d202e1428c554b774e890199638e8d3eb4c
- `ps5upload-fpkg/src/fih.rs` 0c4e2f36d6350a692f63bb8ad1c146cd076f2b4acd87fb9f794bff54b7fedcfe
- `ps5upload-fpkg/src/fih_write.rs` d6508f014a33a0ab3f752318ebd6b1a88b1a2ba611c400ca46d54992bc669de4
- `ps5upload-fpkg/src/flt.rs` ec4d0e547cc723edbdf8bd97663116c9fd8082d28add990e87ef230b7ee2d60a
- `ps5upload-fpkg/src/inner.rs` b52b9594f3bb0d952af4ee802dd0eb2c9b0b35549d3ba454b5a54fbd8573c602
- `ps5upload-fpkg/src/keys.rs` b3ffb286f65969f97b914bb85a218505bf6f5f649f422a4562565704372e2213
- `ps5upload-fpkg/src/kraken.rs` 9b9dc3edc2c429d468854b303b0a2cb7785be30ce13983e79ea167db1b7bf45e
- `ps5upload-fpkg/src/kraken/huff.rs` 3a314f06df38c74c87518084652ccc5c1f0f6d028a92219da2373d17c9e36637
- `ps5upload-fpkg/src/kraken_image.rs` ddb19b3b715e6a62a0da7659fbd6a49af773027924eb40892b30e8fd985de03a
- `ps5upload-fpkg/src/lib.rs` 625a7d7d990d17ab799622b2214e62e568ccd7629d6ef57cbfdf3ede2ae740c0
- `ps5upload-fpkg/src/license.rs` d5a5b35bd3a730ed303895bb06ce80b158e4e67a89e778aad420cd3b0aec486c
- `ps5upload-fpkg/src/naps.rs` d194a48c8e0e1d092c1ee655227f26e9ca6f89a5731130eafa33db76797a5f6d
- `ps5upload-fpkg/src/outer.rs` 3fbdbcce344e891fdc6b2fc7bcb2c9b123737b02300825cc8cca234478603e79
- `ps5upload-fpkg/src/outer_write.rs` 9616e21c5cb22e83aaeed0c313875769659b74de53515b2233cbc35bde38d8f4
- `ps5upload-fpkg/src/pfs_source.rs` d9fc5e00608e8709a59b153d3b3d3803356db78e6fa16203aecbc32ae9ffd05b
- `ps5upload-fpkg/src/pfsc.rs` df7387384d0038762af6efb39e4a1b21c793a9a106c14014ee6ab163543a7a71
- `ps5upload-fpkg/src/pfsc_reader.rs` f83fab7eae014f636836f62fef0e7478bd57f64828f5dc10f13de3c04817fcd5
- `ps5upload-fpkg/src/pfsimage.rs` 4eecf47fc129a2e0e1b7f93c4c061197f88f8a66af5ee53f4b35220676f62f9e
- `ps5upload-fpkg/src/plan.rs` dfc0f9b588974524671e323498f2c95e793af808b4769a1e22fa0fecaae60026
- `ps5upload-fpkg/src/playgo.rs` 9a0ba58da2e4bb67692fb86612bad1f42ee33fdd1b9e3fe04607075bc1d23960
- `ps5upload-fpkg/src/remote_source.rs` f73303ee4493f282428d351e2a9332b06f583d5841c38a5b6ff8782b5a9d0245
- `ps5upload-fpkg/src/rsa.rs` 707b21982d1e4f2f9202136b7405ebbe2ad95b567c09d60d7298ed5e023aca8c
- `ps5upload-fpkg/src/sdk_rules.rs` 150461326e69582c658e588481d64c2e3af7522e78fc6b5575b58c74f377de0d
- `ps5upload-fpkg/src/self_repair.rs` 7d90f93d4e48a7efb691e3f09f1f0a27c879e9ef863126890928120dd68eeb15
- `ps5upload-fpkg/src/si.rs` 328c99964c3f1d619ecf4912e4a32a1098fcd2d0fd70805ffd1675cd1453406a
- `ps5upload-fpkg/src/si_write.rs` 5f2f2c6c0a2b8a5cd513077a4b70ba16caa7385bdbf2a111e5ff102666c31ae2
- `ps5upload-fpkg/src/source.rs` fe8b0073d37a32363a89fe57d3cf10877b106b27aa0ba0e8f78b0af9802addeb
- `ps5upload-fpkg/src/stream.rs` e9cdcfe7ea3931f06169cabd593af9f093903a6fccf5f0439f31c6fc727ea453
- `ps5upload-fpkg/src/ufs2_source.rs` 7606409be8d61621e47912320084fa552c01ec24f630ed6c624f47764a507e13
- `ps5upload-fpkg/src/verify.rs` 5a6c70d2cc1032c1c4f5856d6fe4cfb4f96f0291429374996ddf1c348492659f
- `ps5upload-fpkg/src/xts.rs` 0f65c858a2463ca2084be5f8bc6821abd788d500551347791159e9da0e462b3f
- `ps5upload-fpkg/tests/fixtures/mini.exfat` 9b16140e66a911ba0c73f6f714d11acc1f07f9b410fead6c06e02a33bccb6f1e
- `ps5upload-fpkg/tests/mounts.rs` 903f168cbc0dc58bacb1c4741cd747ac9548dcbc0000db708ab1ebfaff5f3c21
- `ps5upload-fpkg/tests/prepared.rs` b6f6b53e43ee210a0cd246305d1f50e8662dbee5b37571d227481509baf72284
- `ps5upload-fpkg/tests/production.rs` c312cc4051e579035680b37ccb51031759b654a224930cb06e6ea07ed2efe14a
- `ps5upload-fpkg/tests/samples.rs` 837f77faa13525b27945428e33a5d1481adefcb84eac47bd176d93b679040624
- `ps5upload-fpkg/tests/scale.rs` ae715306854e5fe6952e110d97f7db141e2f0410f23da57e9a3d7d6525c2f126
- `ps5upload-fpkg/tests/writer.rs` 68f3621143a173d7eb3389d27984e2eff314b4523770aeddd68095d5fdc4aee0
- `ps5upload-pkg/Cargo.toml` 3b2c5ad9a96a6aa3864328c0ca6c96eb5279b64b57982a0469ef45f60f8b32e7
- `ps5upload-pkg/examples/probe.rs` b235b6f2860f725f7f4b74c05fc59be2b60b713c6aa14ec9e798e8e89ae9bf6f
- `ps5upload-pkg/src/kind.rs` 996cee7eac051ce7904037e3902308e3ccb8995a1f8601b5d47eac963e857404
- `ps5upload-pkg/src/lib.rs` e178875d76367b2a1aa4fb40d16bb8d3bc49210c4c763bfe863263b88083f187
- `ps5upload-pkg/src/ps3.rs` 4c10f8ae436f18f881d9d99365999e60ecc356b273530a4c3823d0b65e701292
- `ps5upload-pkg/src/ufs2.rs` 3042fb1859ba3d2bbe605af51b75a71397da179c1cd57735bc7a4ebf71539259
