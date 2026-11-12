// Son de l'app télé (voir audio.hpp). Sortie : sceAudioOut, mêmes appels et même format que l'ancienne
// app (app/src/pbck_audio_native.cpp, d'après le portage SDL PS5 de ps5-payload-dev) : utilisateur
// système 0xFF, port principal, blocs de 256 trames, 48 kHz, S16 stéréo.
#include "audio.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#if defined(PBCK_PS5)
#include <fcntl.h>
#include <thread>
#include <unistd.h>

extern "C" {
int sceKernelDebugOutText(int, const char*);
std::int32_t sceAudioOutInit(void);
std::int32_t sceAudioOutOpen(std::int32_t, std::int32_t, std::int32_t, std::uint32_t, std::uint32_t, std::uint32_t);
std::int32_t sceAudioOutOutput(std::int32_t, const void*);
}
#include <cstdio>
#endif

namespace app::audio {
namespace {

constexpr int sound_count = static_cast<int>(Sound::count);
std::atomic<std::uint32_t> pending{0};  // un bit par effet demandé depuis le dernier bloc
std::atomic<bool> ui_on{true}, music_on{false}, ok{false};
std::atomic<int> volume{70};

}  // namespace

void configure(bool ui_sounds, bool music, int vol) {
    ui_on.store(ui_sounds, std::memory_order_relaxed);
    music_on.store(music, std::memory_order_relaxed);
    volume.store(vol < 0 ? 0 : vol > 100 ? 100 : vol, std::memory_order_relaxed);
}

void play(Sound s) {
    const int i = static_cast<int>(s);
    if (i < 0 || i >= sound_count || !ok.load(std::memory_order_relaxed) || !ui_on.load(std::memory_order_relaxed)) return;
    pending.fetch_or(1u << i, std::memory_order_relaxed);
}

bool ready() { return ok.load(std::memory_order_relaxed); }

#if !defined(PBCK_PS5)

void start(const char*, void*, std::size_t) {}  // aperçu Mac : muet

#else

namespace {

constexpr std::uint32_t rate = 48000, block = 256;

struct Clip {
    const std::int16_t* pcm = nullptr;
    std::uint32_t frames = 0;
    std::uint32_t rate = 0;
};

std::array<Clip, sound_count> clips{};
Clip music_clip{};

void klog(const char* what, long a, long b = 0) {
    char line[160];
    std::snprintf(line, sizeof line, "PBC2APP audio %s %ld %ld\n", what, a, b);
    sceKernelDebugOutText(0, line);
}

// WAV PCM 16 bits mono (48 kHz pour les effets, 24 kHz ou 48 kHz pour la musique), lu dans [mem, mem+room).
std::size_t load_wav(const char* path, std::uint8_t* mem, std::size_t room, Clip& out) {
    const int f = ::open(path, O_RDONLY);
    if (f < 0) return 0;
    std::size_t used = 0;
    std::uint8_t riff[12];
    std::uint16_t fmt = 0, chans = 0, bits = 0;
    std::uint32_t wav_rate = 0;
    if (::read(f, riff, 12) == 12 && !std::memcmp(riff, "RIFF", 4) && !std::memcmp(riff + 8, "WAVE", 4)) {
        std::uint8_t ch[8];
        while (::read(f, ch, 8) == 8) {
            std::uint32_t size;
            std::memcpy(&size, ch + 4, 4);
            if (!std::memcmp(ch, "fmt ", 4) && size >= 16 && size <= 64) {
                std::uint8_t b[64];
                if (::read(f, b, size) != static_cast<ssize_t>(size)) break;
                std::memcpy(&fmt, b, 2); std::memcpy(&chans, b + 2, 2); std::memcpy(&wav_rate, b + 4, 4); std::memcpy(&bits, b + 14, 2);
            } else if (!std::memcmp(ch, "data", 4)) {
                if (fmt != 1 || chans != 1 || bits != 16 || (wav_rate != 48000 && wav_rate != 24000) || size < 4 || size > room) break;
                std::size_t got = 0;
                while (got < size) { const ssize_t n = ::read(f, mem + got, size - got); if (n <= 0) break; got += static_cast<std::size_t>(n); }
                if (got == size) { out = {reinterpret_cast<const std::int16_t*>(mem), static_cast<std::uint32_t>(size / 2), wav_rate}; used = (size + 15) & ~std::size_t{15}; }
                break;
            } else if (::lseek(f, (size + 1) & ~1u, SEEK_CUR) < 0) break;
        }
    }
    ::close(f);
    return used;
}

struct Voice { const Clip* clip = nullptr; std::uint32_t pos = 0; };

void mixer(int handle) {
    alignas(16) std::array<std::int16_t, block * 2> out{};
    std::array<float, block> acc{};
    std::array<Voice, 8> voices{};
    std::uint32_t music_pos = 0;  // en trames de 48 kHz
    float music_gain = 0;
    for (;;) {
        const std::uint32_t req = pending.exchange(0, std::memory_order_relaxed);
        for (int s = 0; s < sound_count; ++s) {
            if (!(req & (1u << s)) || !clips[static_cast<std::size_t>(s)].pcm) continue;
            const Clip* c = &clips[static_cast<std::size_t>(s)];
            // Le même effet redemandé repart du début (pas d'empilement en défilant vite) ; sinon une voix libre,
            // à défaut la plus avancée.
            Voice* v = nullptr;
            for (auto& x : voices) if (x.clip == c) v = &x;
            for (auto& x : voices) if (!v && !x.clip) v = &x;
            if (!v) { v = &voices[0]; for (auto& x : voices) if (x.pos > v->pos) v = &x; }
            *v = {c, 0};
        }
        const float level = static_cast<float>(volume.load(std::memory_order_relaxed)) / 100.0f;
        const float g = level * level;  // courbe proche de l'oreille
        const float music_target = music_on.load(std::memory_order_relaxed) && music_clip.pcm ? 0.33f * g : 0.0f;
        acc.fill(0);
        for (auto& v : voices) {
            if (!v.clip) continue;
            for (std::uint32_t i = 0; i < block && v.pos < v.clip->frames; ++i, ++v.pos) acc[i] += g * v.clip->pcm[v.pos];
            if (v.pos >= v.clip->frames) v = {};
        }
        if (music_target > 0 || music_gain > 0.0005f) {
            const Clip& m = music_clip;
            const std::uint32_t len48 = m.rate == 24000 ? m.frames * 2 : m.frames;
            for (std::uint32_t i = 0; i < block; ++i) {
                music_gain += (music_target - music_gain) * (1.0f / 24000.0f);  // fondu d'environ une demi-seconde
                float s;
                if (m.rate == 24000) {  // doublement linéaire 24 → 48 kHz, en boucle
                    const std::uint32_t a = music_pos / 2, b = a + 1 < m.frames ? a + 1 : 0;
                    s = (music_pos & 1) ? 0.5f * (static_cast<float>(m.pcm[a]) + m.pcm[b]) : static_cast<float>(m.pcm[a]);
                } else {
                    s = m.pcm[music_pos];
                }
                acc[i] += music_gain * s;
                if (++music_pos >= len48) music_pos = 0;
            }
        } else {
            music_gain = 0;
        }
        for (std::uint32_t i = 0; i < block; ++i) {
            const float x = acc[i];
            const std::int16_t v = static_cast<std::int16_t>(x > 32767.f ? 32767 : x < -32767.f ? -32767 : x);
            out[2 * i] = v;
            out[2 * i + 1] = v;
        }
        // Bloque jusqu'à ce que le bloc précédent soit joué (≈ 5,3 ms) : c'est l'horloge du fil.
        static int blocks = 0;
        if (blocks == 0) klog("first-output", handle, 0);
        const int r = sceAudioOutOutput(handle, out.data());
        if (blocks++ == 0) klog("first-output-done", r, 0);
        if (r < 0) { klog("output-failed", r); ok.store(false); return; }
    }
}

}  // namespace

void start(const char* dir, void* mem, std::size_t bytes) {
    static bool started = false;
    if (started) return;
    started = true;
    auto* p = static_cast<std::uint8_t*>(mem);
    std::size_t used = 0;
    static constexpr const char* names[sound_count] = {"move", "select", "back", "notify", "success", "error"};
    char path[256];
    for (int i = 0; p && i < sound_count; ++i) {
        std::snprintf(path, sizeof path, "%s/sound-%s.wav", dir, names[i]);
        used += load_wav(path, p + used, bytes - used, clips[static_cast<std::size_t>(i)]);
    }
    std::snprintf(path, sizeof path, "%s/music.wav", dir);
    if (p) used += load_wav(path, p + used, bytes - used, music_clip);
    klog("loaded bytes/music", static_cast<long>(used), static_cast<long>(music_clip.frames));
    const int init = sceAudioOutInit();  // « déjà initialisé » n'empêche pas d'ouvrir : on essaie quand même
    const int handle = sceAudioOutOpen(0xFF, 0, 0, block, rate, 1);
    klog("init/open", init, handle);
    if (handle < 1) return;
    ok.store(true);
    std::thread([handle] { mixer(handle); }).detach();
}

#endif

}  // namespace app::audio
