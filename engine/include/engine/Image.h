#pragma once

#include <string>
#include <vector>

namespace engine {

    // A decoded image: tightly-packed RGBA8, 4 bytes per pixel.
    struct Image {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;
    };

    // Decode a PNG file into RGBA8 (any source format is expanded to 4
    // channels). Returns false on missing file or decode error.
    bool loadImage(const std::string& path, Image& out);

    // Decode an in-memory PNG (e.g. a data-URI payload) into RGBA8.
    bool loadImage(const unsigned char* bytes, std::size_t len, Image& out);

} // namespace engine
