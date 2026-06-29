#pragma once

#include "engine/GL.h"

namespace engine {

    // A 2D RGBA texture. For the voxel atlas we sample with nearest-neighbor
    // filtering for a crisp, pixelated look.
    class Texture {
    public:
        Texture() = default;
        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        // Upload tightly-packed RGBA8 pixel data (width*height*4 bytes).
        void createFromPixels(int width, int height, const unsigned char* rgba);

        void bind(int unit = 0) const;

        GLuint id() const { return m_tex; }

    private:
        GLuint m_tex = 0;
    };

} // namespace engine
