//! Toast texts in the console's language (System Settings > Language), as ps5-dump-forge's
//! notice.rs: the same 11 languages, English when the language can't be read. "Kraken Mount"
//! stays untranslated; arguments (title names, sizes, paths) are kept as given.

/// A toast language, by the console's `SceSystemParamLang` code.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Lang {
    En,
    Ru,
    Zh,
    De,
    Es,
    Fr,
    Ja,
    Pt,
    It,
    Ko,
    Ar,
}

impl Lang {
    /// `SCE_SYSTEM_PARAM_LANG_*`: both Englishes, Spanishes, Frenches, Portugueses and Chineses
    /// map to the one we have (Chinese: Simplified, Portuguese: Brazil); any other is English.
    fn from_console(code: i32) -> Self {
        match code {
            0 => Self::Ja,
            2 | 22 => Self::Fr,
            3 | 20 => Self::Es,
            4 => Self::De,
            5 => Self::It,
            7 | 17 => Self::Pt,
            8 => Self::Ru,
            9 => Self::Ko,
            10 | 11 => Self::Zh,
            21 => Self::Ar,
            _ => Self::En,
        }
    }
}

extern "C" {
    /// libSceSystemService: a system setting by id.
    fn sceSystemServiceParamGetInt(param_id: i32, value: *mut i32) -> i32;
}

/// The console's language now (read per toast, so a change applies without a restart);
/// English when it can't be read.
pub fn lang() -> Lang {
    console_code().map_or(Lang::En, Lang::from_console)
}

/// `SCE_SYSTEM_PARAM_LANG_*` of the console now, None when it can't be read.
fn console_code() -> Option<i32> {
    /// `SCE_SYSTEM_SERVICE_PARAM_ID_LANG`.
    const LANG: i32 = 1;
    let mut code = -1;
    // SAFETY: one i32 out-parameter that outlives the call.
    (unsafe { sceSystemServiceParamGetInt(LANG, &mut code) } == 0).then_some(code)
}

/// param.json `localizedParameters` tags by `SCE_SYSTEM_PARAM_LANG_*` (SMP sm_l10n.c order, the
/// tags LibProsperoPkg writes).
const TITLE_TAGS: [&str; 31] = [
    "ja-JP", "en-US", "fr-FR", "es-ES", "de-DE", "it-IT", "nl-NL", "pt-PT", "ru-RU", "ko-KR",
    "zh-Hant", "zh-Hans", "fi-FI", "sv-SE", "da-DK", "no-NO", "pl-PL", "pt-BR", "en-GB", "tr-TR",
    "es-419", "ar-AE", "fr-CA", "cs-CZ", "hu-HU", "el-GR", "ro-RO", "th-TH", "vi-VN", "id-ID",
    "uk-UA",
];

/// The console language's `localizedParameters` tag (all 31 console languages, not only the 11
/// toast languages); en-US when it can't be read.
pub fn title_tag() -> &'static str {
    console_code().and_then(|c| TITLE_TAGS.get(usize::try_from(c).ok()?)).copied().unwrap_or("en-US")
}

/// The payload is up and serving the web UI.
pub fn started(l: Lang) -> &'static str {
    match l {
        Lang::En => "Kraken Mount started",
        Lang::Ru => "Kraken Mount запущен",
        Lang::Zh => "Kraken Mount 已启动",
        Lang::De => "Kraken Mount wurde gestartet",
        Lang::Es => "Kraken Mount se ha iniciado",
        Lang::Fr => "Kraken Mount est lancé",
        Lang::Ja => "Kraken Mount を起動しました",
        Lang::Pt => "O Kraken Mount foi iniciado",
        Lang::It => "Kraken Mount è stato avviato",
        Lang::Ko => "Kraken Mount가 실행되었습니다",
        Lang::Ar => "تم تشغيل Kraken Mount",
    }
}

/// Shut down from the web UI.
pub fn stopped(l: Lang) -> &'static str {
    match l {
        Lang::En => "Kraken Mount stopped",
        Lang::Ru => "Kraken Mount остановлен",
        Lang::Zh => "Kraken Mount 已停止",
        Lang::De => "Kraken Mount wurde beendet",
        Lang::Es => "Kraken Mount se ha detenido",
        Lang::Fr => "Kraken Mount est arrêté",
        Lang::Ja => "Kraken Mount は停止しました",
        Lang::Pt => "O Kraken Mount foi parado",
        Lang::It => "Kraken Mount è stato arrestato",
        Lang::Ko => "Kraken Mount가 중지되었습니다",
        Lang::Ar => "توقّف Kraken Mount",
    }
}

/// The web UI port is taken: an older copy with a game running, or another app.
pub fn port_busy(l: Lang, port: u16) -> String {
    match l {
        Lang::En => format!("Kraken Mount: port {port} busy (an older copy that has a game running, or another app?); not started"),
        Lang::Ru => format!("Kraken Mount: порт {port} занят (старая копия с запущенной игрой или другое приложение?); не запущен"),
        Lang::Zh => format!("Kraken Mount：端口 {port} 已被占用（正在运行游戏的旧版本，或其他应用？）；未启动"),
        Lang::De => format!("Kraken Mount: Port {port} belegt (eine ältere Kopie mit laufendem Spiel oder eine andere App?); nicht gestartet"),
        Lang::Es => format!("Kraken Mount: el puerto {port} está ocupado (¿una copia anterior con un juego en curso u otra aplicación?); no se ha iniciado"),
        Lang::Fr => format!("Kraken Mount\u{202f}: le port {port} est occupé (une ancienne copie avec un jeu en cours, ou une autre application\u{202f}?)\u{202f}; non lancé"),
        Lang::Ja => format!("Kraken Mount：ポート {port} は使用中です（ゲーム実行中の古いコピー、または別のアプリ？）。起動しませんでした"),
        Lang::Pt => format!("Kraken Mount: a porta {port} está ocupada (uma cópia antiga com um jogo em execução ou outro aplicativo?); não foi iniciado"),
        Lang::It => format!("Kraken Mount: la porta {port} è occupata (una copia precedente con un gioco in corso o un'altra app?); non avviato"),
        Lang::Ko => format!("Kraken Mount: 포트 {port}이(가) 사용 중입니다(게임이 실행 중인 이전 사본 또는 다른 앱?). 시작하지 않았습니다"),
        Lang::Ar => format!("Kraken Mount: المنفذ {port} مشغول (نسخة أقدم تشغّل لعبة، أو تطبيق آخر؟)؛ لم يتم التشغيل"),
    }
}

/// `/data` free space is below `low_space_gb`.
pub fn low_space(l: Lang, gb: u64) -> String {
    match l {
        Lang::En => format!("Kraken Mount: /data has only {gb} GB free"),
        Lang::Ru => format!("Kraken Mount: на /data свободно всего {gb} ГБ"),
        Lang::Zh => format!("Kraken Mount：/data 仅剩 {gb} GB 可用空间"),
        Lang::De => format!("Kraken Mount: /data hat nur noch {gb} GB frei"),
        Lang::Es => format!("Kraken Mount: /data solo tiene {gb} GB libres"),
        Lang::Fr => format!("Kraken Mount\u{202f}: /data n’a plus que {gb} Go libres"),
        Lang::Ja => format!("Kraken Mount：/data の空き容量は {gb} GB のみです"),
        Lang::Pt => format!("Kraken Mount: /data tem apenas {gb} GB livres"),
        Lang::It => format!("Kraken Mount: /data ha solo {gb} GB liberi"),
        Lang::Ko => format!("Kraken Mount: /data의 여유 공간이 {gb}GB밖에 없습니다"),
        Lang::Ar => format!("Kraken Mount: لا يتوفر في /data سوى {gb} غيغابايت"),
    }
}

/// A data directory can't be created at start-up; `err` is the OS error (English).
pub fn cannot_create(l: Lang, dir: &str, err: &str) -> String {
    match l {
        Lang::En => format!("Kraken Mount: cannot create {dir}: {err}"),
        Lang::Ru => format!("Kraken Mount: не удаётся создать {dir}: {err}"),
        Lang::Zh => format!("Kraken Mount：无法创建 {dir}：{err}"),
        Lang::De => format!("Kraken Mount: {dir} kann nicht erstellt werden: {err}"),
        Lang::Es => format!("Kraken Mount: no se puede crear {dir}: {err}"),
        Lang::Fr => format!("Kraken Mount\u{202f}: impossible de créer {dir}\u{202f}: {err}"),
        Lang::Ja => format!("Kraken Mount：{dir} を作成できません：{err}"),
        Lang::Pt => format!("Kraken Mount: não é possível criar {dir}: {err}"),
        Lang::It => format!("Kraken Mount: impossibile creare {dir}: {err}"),
        Lang::Ko => format!("Kraken Mount: {dir}을(를) 만들 수 없습니다: {err}"),
        Lang::Ar => format!("Kraken Mount: تعذّر إنشاء {dir}: {err}"),
    }
}

/// A game was newly registered on the home screen.
pub fn registered(l: Lang, name: &str) -> String {
    match l {
        Lang::En => format!("{name} registered"),
        Lang::Ru => format!("{name}: игра зарегистрирована"),
        Lang::Zh => format!("{name} 已注册"),
        Lang::De => format!("{name} registriert"),
        Lang::Es => format!("{name} registrado"),
        Lang::Fr => format!("{name} enregistré"),
        Lang::Ja => format!("{name} を登録しました"),
        Lang::Pt => format!("{name} registrado"),
        Lang::It => format!("{name} registrato"),
        Lang::Ko => format!("{name} 등록됨"),
        Lang::Ar => format!("تم تسجيل {name}"),
    }
}

/// A newer ampr-emu-ng release (`version`, its tag) was downloaded and installed.
pub fn ampr_updated(l: Lang, version: &str) -> String {
    match l {
        Lang::En => format!("ampr-emu-ng {version} installed"),
        Lang::Ru => format!("ampr-emu-ng {version} установлен"),
        Lang::Zh => format!("ampr-emu-ng {version} 已安装"),
        Lang::De => format!("ampr-emu-ng {version} installiert"),
        Lang::Es => format!("ampr-emu-ng {version} instalado"),
        Lang::Fr => format!("ampr-emu-ng {version} installé"),
        Lang::Ja => format!("ampr-emu-ng {version} をインストールしました"),
        Lang::Pt => format!("ampr-emu-ng {version} instalado"),
        Lang::It => format!("ampr-emu-ng {version} installato"),
        Lang::Ko => format!("ampr-emu-ng {version} 설치됨"),
        Lang::Ar => format!("تم تثبيت ampr-emu-ng {version}"),
    }
}

/// A game moved to another drive or folder (`path`, the new source) was relinked, no rebuild.
pub fn moved(l: Lang, name: &str, path: &str) -> String {
    match l {
        Lang::En => format!("{name} moved to {path}"),
        Lang::Ru => format!("{name}: игра перемещена в {path}"),
        Lang::Zh => format!("{name} 已移动到 {path}"),
        Lang::De => format!("{name} nach {path} verschoben"),
        Lang::Es => format!("{name} movido a {path}"),
        Lang::Fr => format!("{name} déplacé vers {path}"),
        Lang::Ja => format!("{name} を {path} に移動しました"),
        Lang::Pt => format!("{name} movido para {path}"),
        Lang::It => format!("{name} spostato in {path}"),
        Lang::Ko => format!("{name}이(가) {path}(으)로 이동됨"),
        Lang::Ar => format!("تم نقل {name} إلى {path}"),
    }
}

/// A game's build or registration failed; the error is in kraken.log and the UI.
pub fn failed(l: Lang, name: &str) -> String {
    match l {
        Lang::En => format!("{name}: failed, see the log"),
        Lang::Ru => format!("{name}: ошибка, см. журнал"),
        Lang::Zh => format!("{name}：失败，请查看日志"),
        Lang::De => format!("{name}: fehlgeschlagen, siehe Protokoll"),
        Lang::Es => format!("{name}: error, consulta el registro"),
        Lang::Fr => format!("{name}\u{202f}: échec, voir le journal"),
        Lang::Ja => format!("{name}：失敗しました。ログを確認してください"),
        Lang::Pt => format!("{name}: falhou, veja o log"),
        Lang::It => format!("{name}: non riuscito, vedi il log"),
        Lang::Ko => format!("{name}: 실패했습니다. 로그를 확인하세요"),
        Lang::Ar => format!("{name}: فشل، راجع السجل"),
    }
}

/// A game's source and cache were deleted from the web UI.
pub fn deleted(l: Lang, name: &str) -> String {
    match l {
        Lang::En => format!("{name} deleted"),
        Lang::Ru => format!("{name}: игра удалена"),
        Lang::Zh => format!("{name} 已删除"),
        Lang::De => format!("{name} gelöscht"),
        Lang::Es => format!("{name} eliminado"),
        Lang::Fr => format!("{name} supprimé"),
        Lang::Ja => format!("{name} を削除しました"),
        Lang::Pt => format!("{name} excluído"),
        Lang::It => format!("{name} eliminato"),
        Lang::Ko => format!("{name} 삭제됨"),
        Lang::Ar => format!("تم حذف {name}"),
    }
}

/// kstuff does not answer its readiness probe: start-up waits for it (SMP SM_L10N_KSTUFF_WAITING).
pub fn kstuff_waiting(l: Lang) -> &'static str {
    match l {
        Lang::En => "Kraken Mount: kstuff not found; waiting for it to start",
        Lang::Ru => "Kraken Mount: kstuff не найден; ожидание его запуска",
        Lang::Zh => "Kraken Mount：未找到 kstuff，正在等待其启动",
        Lang::De => "Kraken Mount: kstuff nicht gefunden; warte auf den Start",
        Lang::Es => "Kraken Mount: no se encontró kstuff; esperando a que se inicie",
        Lang::Fr => "Kraken Mount : kstuff introuvable ; en attente de son démarrage",
        Lang::Ja => "Kraken Mount: kstuff が見つかりません。起動を待っています",
        Lang::Pt => "Kraken Mount: kstuff não encontrado; aguardando sua inicialização",
        Lang::It => "Kraken Mount: kstuff non trovato; in attesa del suo avvio",
        Lang::Ko => "Kraken Mount: kstuff를 찾을 수 없습니다. 시작될 때까지 기다리는 중입니다",
        Lang::Ar => "Kraken Mount: لم يتم العثور على kstuff؛ جارٍ انتظار تشغيله",
    }
}

/// ShadowMount+ is running (it mounts the same titles): start-up waits for it to stop.
pub fn smp_running(l: Lang) -> &'static str {
    match l {
        Lang::En => "Kraken Mount: ShadowMount+ is running; waiting for it to stop",
        Lang::Ru => "Kraken Mount: запущен ShadowMount+; ожидание его остановки",
        Lang::Zh => "Kraken Mount：ShadowMount+ 正在运行，正在等待其停止",
        Lang::De => "Kraken Mount: ShadowMount+ läuft; warte, bis es beendet wird",
        Lang::Es => "Kraken Mount: ShadowMount+ está en ejecución; esperando a que se detenga",
        Lang::Fr => "Kraken Mount : ShadowMount+ est en cours d'exécution ; en attente de son arrêt",
        Lang::Ja => "Kraken Mount: ShadowMount+ が実行中です。停止を待っています",
        Lang::Pt => "Kraken Mount: o ShadowMount+ está em execução; aguardando que ele pare",
        Lang::It => "Kraken Mount: ShadowMount+ è in esecuzione; in attesa che si arresti",
        Lang::Ko => "Kraken Mount: ShadowMount+가 실행 중입니다. 종료될 때까지 기다리는 중입니다",
        Lang::Ar => "Kraken Mount: ShadowMount+ قيد التشغيل؛ جارٍ انتظار إيقافه",
    }
}
