// Реализация AudioSystem на miniaudio. Компилируется только если miniaudio найден (см. CMakeLists.txt модуля).
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <memory>

#include "han/audio/audio_system.h"

namespace han::audio {
namespace {
using contracts::Bus;
using contracts::Vec3;

constexpr int kBusCount = 6;
}  // namespace

bool hasAudio() { return true; }

struct AudioSystem::Impl {
    struct Voice {
        ma_sound sound{};
        bool music = false;
        bool loop = false;
        std::string id;
        // Плавное изменение громкости музыки.
        float fadeFrom = 1.f, fadeTo = 1.f, fadeTime = 0.f, fadeDuration = 0.f;
        bool stopWhenFaded = false;
        float baseVolume = 1.f;
    };

    AudioSettings settings;
    ma_engine engine{};
    ma_sound_group groups[kBusCount]{};
    bool engineReady = false;
    int groupsReady = 0;
    std::map<std::string, SoundEvent, std::less<>> events;
    std::list<std::unique_ptr<Voice>> voices;
    float busVolumes[kBusCount] = {1, 1, 1, 1, 1, 1};
    std::string music;
    std::uint32_t failed = 0;
    std::string problem;
    std::uint32_t rng = 1;

    float rand01() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<float>(rng & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }
    float jitter(float base, float amount) { return base * (1.f + amount * (rand01() * 2.f - 1.f)); }

    void fail(const std::string& why) {
        ++failed;
        problem = why;
    }

    void dropVoice(std::list<std::unique_ptr<Voice>>::iterator it) {
        ma_sound_uninit(&(*it)->sound);
        voices.erase(it);
    }

    // Освобождает место: вытесняется самый старый немузыкальный звук.
    void makeRoom() {
        while (voices.size() >= settings.maxVoices) {
            auto victim = std::find_if(voices.begin(), voices.end(), [](const auto& v) { return !v->music; });
            if (victim == voices.end()) return;
            dropVoice(victim);
        }
    }

    Voice* start(const SoundEvent& e, bool spatialOverride, bool allowSpatial, Vec3 position, float extraVolume = 1.f) {
        if (e.files.empty()) {
            fail("у события «" + e.id + "» нет файлов");
            return nullptr;
        }
        makeRoom();
        const auto& file = e.files[static_cast<std::size_t>(rand01() * static_cast<float>(e.files.size())) % e.files.size()];
        auto voice = std::make_unique<Voice>();
        const bool spatial = allowSpatial && e.spatial && !spatialOverride;
        ma_uint32 flags = e.bus == Bus::Music ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
        if (!spatial) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
        const ma_result r = ma_sound_init_from_file_w(&engine, file.c_str(), flags, &groups[static_cast<int>(e.bus)], nullptr, &voice->sound);
#else
        const ma_result r = ma_sound_init_from_file(&engine, file.string().c_str(), flags, &groups[static_cast<int>(e.bus)], nullptr, &voice->sound);
#endif
        if (r != MA_SUCCESS) {
            fail("не удалось открыть звук " + file.string() + " (код miniaudio " + std::to_string(static_cast<int>(r)) + ")");
            return nullptr;
        }
        voice->id = e.id;
        voice->music = e.bus == Bus::Music;
        voice->loop = e.loop;
        voice->baseVolume = std::clamp(jitter(e.volume, e.volumeJitter), 0.f, 4.f) * extraVolume;
        ma_sound_set_volume(&voice->sound, voice->baseVolume);
        ma_sound_set_pitch(&voice->sound, std::max(0.05f, jitter(e.pitch, e.pitchJitter)));
        ma_sound_set_looping(&voice->sound, e.loop ? MA_TRUE : MA_FALSE);
        if (spatial) {
            ma_sound_set_position(&voice->sound, position.x, position.y, position.z);
            ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_linear);  // тишина на максимальной дистанции
            ma_sound_set_min_distance(&voice->sound, e.minDistance);
            ma_sound_set_max_distance(&voice->sound, e.maxDistance);
            ma_sound_set_rolloff(&voice->sound, 1.f);
        }
        if (ma_sound_start(&voice->sound) != MA_SUCCESS) {
            ma_sound_uninit(&voice->sound);
            fail("не удалось запустить звук " + e.id);
            return nullptr;
        }
        voices.push_back(std::move(voice));
        return voices.back().get();
    }

    void fadeMusic(Voice& v, float from, float to, float seconds, bool stopAtEnd) {
        v.fadeFrom = from;
        v.fadeTo = to;
        v.fadeTime = 0.f;
        v.fadeDuration = std::max(seconds, 0.f);
        v.stopWhenFaded = stopAtEnd;
        ma_sound_set_volume(&v.sound, v.baseVolume * (v.fadeDuration > 0.f ? from : to));
    }
};

AudioSystem::AudioSystem() : impl_(std::make_unique<Impl>()) {}

AudioSystem::~AudioSystem() {
    if (!impl_) return;
    for (auto& v : impl_->voices) ma_sound_uninit(&v->sound);
    impl_->voices.clear();
    for (int i = impl_->groupsReady - 1; i >= 0; --i) ma_sound_group_uninit(&impl_->groups[i]);
    if (impl_->engineReady) ma_engine_uninit(&impl_->engine);
}

std::unique_ptr<AudioSystem> AudioSystem::create(const AudioSettings& settings, std::string& error) {
    std::unique_ptr<AudioSystem> sys(new AudioSystem());
    Impl& im = *sys->impl_;
    im.settings = settings;
    im.settings.maxVoices = std::max<std::uint32_t>(settings.maxVoices, 1);
    im.rng = settings.seed ? settings.seed : 1u;
    ma_engine_config config = ma_engine_config_init();
    if (settings.headless) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = settings.sampleRate;
    }
    if (const ma_result r = ma_engine_init(&config, &im.engine); r != MA_SUCCESS) {
        error = "не удалось запустить звуковой движок (код miniaudio " + std::to_string(static_cast<int>(r)) + ")";
        return nullptr;
    }
    im.engineReady = true;
    // Шина Master — корень, остальные шины — её дети: громкость Master действует на всё.
    for (int i = 0; i < kBusCount; ++i) {
        if (ma_sound_group_init(&im.engine, 0, i == 0 ? nullptr : &im.groups[0], &im.groups[i]) != MA_SUCCESS) {
            error = "не удалось создать шину звука " + std::string(busName(static_cast<Bus>(i)));
            return nullptr;
        }
        ++im.groupsReady;
    }
    return sys;
}

void AudioSystem::addEvent(SoundEvent event) {
    const std::string id = event.id;
    impl_->events[id] = std::move(event);
}

bool AudioSystem::hasEvent(std::string_view id) const { return impl_->events.find(id) != impl_->events.end(); }

std::size_t AudioSystem::eventCount() const { return impl_->events.size(); }

void AudioSystem::setListener(Vec3 position, Vec3 forward, Vec3 up) {
    ma_engine_listener_set_position(&impl_->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
}

void AudioSystem::playEvent(std::string_view eventId, Vec3 position) {
    const auto it = impl_->events.find(eventId);
    if (it == impl_->events.end()) {
        impl_->fail("нет события «" + std::string(eventId) + "»");
        return;
    }
    impl_->start(it->second, false, true, position);
}

void AudioSystem::playUi(std::string_view eventId) {
    const auto it = impl_->events.find(eventId);
    if (it == impl_->events.end()) {
        impl_->fail("нет события «" + std::string(eventId) + "»");
        return;
    }
    impl_->start(it->second, true, false, {});
}

void AudioSystem::setBusVolume(Bus bus, float volume01) {
    const int i = static_cast<int>(bus);
    if (i < 0 || i >= kBusCount) return;
    const float v = std::clamp(volume01, 0.f, 1.f);
    impl_->busVolumes[i] = v;
    ma_sound_group_set_volume(&impl_->groups[i], v);
}

float AudioSystem::busVolume(Bus bus) const {
    const int i = static_cast<int>(bus);
    return i >= 0 && i < kBusCount ? impl_->busVolumes[i] : 0.f;
}

void AudioSystem::playMusic(std::string_view trackId, float fadeSeconds) {
    Impl& im = *impl_;
    if (im.music == trackId && !trackId.empty()) return;  // уже играет
    const auto it = im.events.find(trackId);
    if (it == im.events.end()) {
        im.fail("нет музыкального трека «" + std::string(trackId) + "»");
        return;
    }
    for (auto& v : im.voices)
        if (v->music && !(v->stopWhenFaded)) im.fadeMusic(*v, v->fadeDuration > 0.f ? v->fadeTo : 1.f, 0.f, fadeSeconds, true);
    SoundEvent track = it->second;
    track.bus = Bus::Music;
    track.spatial = false;
    track.loop = true;
    if (auto* v = im.start(track, true, false, {})) {
        im.fadeMusic(*v, 0.f, 1.f, fadeSeconds, false);
        im.music = std::string(trackId);
    }
}

void AudioSystem::stopMusic(float fadeSeconds) {
    for (auto& v : impl_->voices)
        if (v->music && !v->stopWhenFaded) impl_->fadeMusic(*v, v->fadeDuration > 0.f ? v->fadeTo : 1.f, 0.f, fadeSeconds, true);
    impl_->music.clear();
}

std::string AudioSystem::currentMusic() const { return impl_->music; }

void AudioSystem::update(float dt) {
    Impl& im = *impl_;
    for (auto it = im.voices.begin(); it != im.voices.end();) {
        Impl::Voice& v = **it;
        bool remove = false;
        if (v.music && v.fadeDuration > 0.f && v.fadeTime < v.fadeDuration) {
            v.fadeTime = std::min(v.fadeTime + dt, v.fadeDuration);
            const float k = v.fadeTime / v.fadeDuration;
            ma_sound_set_volume(&v.sound, v.baseVolume * (v.fadeFrom + (v.fadeTo - v.fadeFrom) * k));
        }
        if (v.stopWhenFaded && v.fadeTime >= v.fadeDuration) remove = true;
        if (!v.loop && ma_sound_at_end(&v.sound)) remove = true;
        if (remove) {
            ma_sound_uninit(&v.sound);
            it = im.voices.erase(it);
        } else {
            ++it;
        }
    }
}

std::uint32_t AudioSystem::activeVoices() const { return static_cast<std::uint32_t>(impl_->voices.size()); }

std::uint32_t AudioSystem::failedPlays() const { return impl_->failed; }

std::string AudioSystem::lastProblem() const { return impl_->problem; }

std::uint32_t AudioSystem::renderFrames(float* out, std::uint32_t frames) {
    if (!impl_->settings.headless || !out) return 0;
    ma_uint64 read = 0;
    if (ma_engine_read_pcm_frames(&impl_->engine, out, frames, &read) != MA_SUCCESS) return static_cast<std::uint32_t>(read);
    return static_cast<std::uint32_t>(read);
}

std::uint32_t AudioSystem::sampleRate() const { return ma_engine_get_sample_rate(&impl_->engine); }
}  // namespace han::audio
