#include "engine/Audio.h"

// The one translation unit that compiles miniaudio.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <SDL3/SDL_log.h>

#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine {

    namespace {
        // One-shots past this cap are dropped, not queued -- a click barrage
        // should never grow an unbounded sound list.
        constexpr std::size_t kMaxOneShots = 32;

        // One-shot spatial falloff: full volume within 1 block, silent at 24.
        constexpr float kOneShotMaxDistance = 24.0f;
    } // namespace

    struct Audio::Impl {
        ma_engine engine{};
        bool ok = false;

        // One decoded, never-started sound per file. Pins the PCM in the
        // resource-manager cache and is the source for cheap init_copy.
        std::unordered_map<std::string, std::unique_ptr<ma_sound>> prototypes;

        std::vector<std::unique_ptr<ma_sound>> oneShots;
        std::unordered_map<AudioLoop, std::unique_ptr<ma_sound>> loops;
        AudioLoop nextLoop = 1;

        std::unordered_set<std::string> warnedNames; // log unknown names once

        // Looks up a prototype and clones it (shared PCM, fresh playback
        // state). Returns nullptr (and logs once) for unknown names.
        std::unique_ptr<ma_sound> spawn(const std::string& name) {
            const auto it = prototypes.find(name);
            if (it == prototypes.end()) {
                if (warnedNames.insert(name).second) {
                    SDL_Log("Audio: no sound named '%s' is loaded", name.c_str());
                }
                return nullptr;
            }
            auto sound = std::make_unique<ma_sound>();
            if (ma_sound_init_copy(&engine, it->second.get(), 0, nullptr,
                                   sound.get()) != MA_SUCCESS) {
                return nullptr;
            }
            return sound;
        }
    };

    namespace {
        void makeSpatial(ma_sound& s, const glm::vec3& pos, float maxDistance) {
            ma_sound_set_spatialization_enabled(&s, MA_TRUE);
            ma_sound_set_attenuation_model(&s, ma_attenuation_model_linear);
            ma_sound_set_min_distance(&s, 1.0f);
            ma_sound_set_max_distance(&s, maxDistance);
            ma_sound_set_position(&s, pos.x, pos.y, pos.z);
        }
    } // namespace

    Audio::Audio() : m_impl(std::make_unique<Impl>()) {
        if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS) {
            SDL_Log("Audio disabled: no output device could be opened");
            return;
        }
        m_impl->ok = true;
    }

    Audio::~Audio() {
        // Sounds must die before the engine they play on.
        for (auto& s : m_impl->oneShots) ma_sound_uninit(s.get());
        for (auto& [h, s] : m_impl->loops) ma_sound_uninit(s.get());
        for (auto& [n, s] : m_impl->prototypes) ma_sound_uninit(s.get());
        if (m_impl->ok) {
            ma_engine_uninit(&m_impl->engine);
        }
    }

    bool Audio::enabled() const {
        return m_impl->ok;
    }

    void Audio::loadDirectory(const std::string& dir) {
        if (!m_impl->ok) return;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            SDL_Log("Audio: no sounds directory at '%s' -- the game will be silent",
                    dir.c_str());
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".wav") {
                continue;
            }
            const std::string name = entry.path().stem().string();
            auto sound = std::make_unique<ma_sound>();
            if (ma_sound_init_from_file(&m_impl->engine,
                                        entry.path().string().c_str(),
                                        MA_SOUND_FLAG_DECODE, nullptr, nullptr,
                                        sound.get()) != MA_SUCCESS) {
                SDL_Log("Audio: failed to load '%s' -- skipping",
                        entry.path().string().c_str());
                continue;
            }
            m_impl->prototypes[name] = std::move(sound);
        }
    }

    void Audio::play(const std::string& name, float volume, float pitch) {
        if (!m_impl->ok || m_impl->oneShots.size() >= kMaxOneShots) return;
        auto sound = m_impl->spawn(name);
        if (!sound) return;
        ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_pitch(sound.get(), pitch);
        ma_sound_start(sound.get());
        m_impl->oneShots.push_back(std::move(sound));
    }

    void Audio::playAt(const std::string& name, const glm::vec3& pos,
                       float volume, float pitch) {
        if (!m_impl->ok || m_impl->oneShots.size() >= kMaxOneShots) return;
        auto sound = m_impl->spawn(name);
        if (!sound) return;
        makeSpatial(*sound, pos, kOneShotMaxDistance);
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_pitch(sound.get(), pitch);
        ma_sound_start(sound.get());
        m_impl->oneShots.push_back(std::move(sound));
    }

    AudioLoop Audio::createLoop(const std::string& name, bool spatial, float gain,
                                float maxDistance, float pitch) {
        if (!m_impl->ok) return 0;
        auto sound = m_impl->spawn(name);
        if (!sound) return 0;
        if (spatial) {
            makeSpatial(*sound, glm::vec3(0.0f), maxDistance);
        } else {
            ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
        }
        ma_sound_set_looping(sound.get(), MA_TRUE);
        ma_sound_set_volume(sound.get(), gain);
        ma_sound_set_pitch(sound.get(), pitch);
        ma_sound_start(sound.get());
        const AudioLoop handle = m_impl->nextLoop++;
        m_impl->loops[handle] = std::move(sound);
        return handle;
    }

    void Audio::setLoopGain(AudioLoop loop, float gain) {
        const auto it = m_impl->loops.find(loop);
        if (it != m_impl->loops.end()) ma_sound_set_volume(it->second.get(), gain);
    }

    void Audio::setLoopPosition(AudioLoop loop, const glm::vec3& pos) {
        const auto it = m_impl->loops.find(loop);
        if (it != m_impl->loops.end()) {
            ma_sound_set_position(it->second.get(), pos.x, pos.y, pos.z);
        }
    }

    void Audio::setLoopPaused(AudioLoop loop, bool paused) {
        const auto it = m_impl->loops.find(loop);
        if (it == m_impl->loops.end()) return;
        if (paused) {
            ma_sound_stop(it->second.get()); // stops at the cursor, no rewind
        } else {
            ma_sound_start(it->second.get());
        }
    }

    void Audio::destroyLoop(AudioLoop loop) {
        const auto it = m_impl->loops.find(loop);
        if (it == m_impl->loops.end()) return;
        ma_sound_uninit(it->second.get());
        m_impl->loops.erase(it);
    }

    void Audio::update(const glm::vec3& listenerPos, const glm::vec3& front,
                       const glm::vec3& up) {
        if (!m_impl->ok) return;
        ma_engine_listener_set_position(&m_impl->engine, 0, listenerPos.x,
                                        listenerPos.y, listenerPos.z);
        ma_engine_listener_set_direction(&m_impl->engine, 0, front.x, front.y,
                                         front.z);
        ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);

        auto& shots = m_impl->oneShots;
        for (std::size_t i = 0; i < shots.size();) {
            if (ma_sound_at_end(shots[i].get())) {
                ma_sound_uninit(shots[i].get());
                shots[i] = std::move(shots.back());
                shots.pop_back();
            } else {
                ++i;
            }
        }
    }

    void Audio::setMasterVolume(float volume) {
        if (!m_impl->ok) return;
        ma_engine_set_volume(&m_impl->engine, volume);
    }

} // namespace engine
