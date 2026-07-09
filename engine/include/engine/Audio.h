#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace engine {

    // Handle to a persistent looping sound. 0 is invalid; every operation on
    // an invalid or stale handle is a safe no-op.
    using AudioLoop = std::uint32_t;

    // Playback engine (miniaudio under the hood -- kept out of this header so
    // only Audio.cpp compiles it). If the output device can't be opened
    // (headless CI, remote session), the system logs once and every call
    // becomes a no-op: audio can never crash or block the game.
    class Audio {
    public:
        Audio();
        ~Audio();

        bool enabled() const;

        // Load every .wav in dir (non-recursive); a sound's name is its file
        // stem ("mine", "rain_loop"). A missing directory or file logs and
        // stays silent -- same philosophy as the texture-atlas fallback.
        void loadDirectory(const std::string& dir);

        // Fire-and-forget one-shots: play() is flat (UI), playAt() is
        // spatialized at a world position with linear distance attenuation.
        void play(const std::string& name, float volume = 1.0f, float pitch = 1.0f);
        void playAt(const std::string& name, const glm::vec3& pos,
                    float volume = 1.0f, float pitch = 1.0f);

        // Persistent loops (ambience, machine hums). Loops start immediately
        // at `gain`; maxDistance only applies when spatial.
        AudioLoop createLoop(const std::string& name, bool spatial, float gain,
                             float maxDistance = 24.0f, float pitch = 1.0f);
        void setLoopGain(AudioLoop loop, float gain);
        void setLoopPosition(AudioLoop loop, const glm::vec3& pos);
        void setLoopPaused(AudioLoop loop, bool paused); // pauses at the cursor
        void destroyLoop(AudioLoop loop);

        // Called once per frame by Application: moves the 3D listener and
        // reaps finished one-shots.
        void update(const glm::vec3& listenerPos, const glm::vec3& front,
                    const glm::vec3& up);

        void setMasterVolume(float volume); // 0..1

    private:
        struct Impl; // miniaudio types live only in Audio.cpp
        std::unique_ptr<Impl> m_impl;
    };

} // namespace engine
