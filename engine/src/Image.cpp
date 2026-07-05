#include "engine/Image.h"

// Vendored single-header PNG decoder (public domain, third_party/stb/).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

namespace engine {

    bool loadImage(const std::string& path, Image& out) {
        int w = 0, h = 0, comp = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
        if (!data) {
            return false;
        }
        out.width = w;
        out.height = h;
        out.rgba.assign(data, data + static_cast<std::size_t>(w) * h * 4);
        stbi_image_free(data);
        return true;
    }

} // namespace engine
