#pragma once

#include "engine/Shader.h"
#include "engine/Mesh.h"
#include "engine/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace engine {

    // Immediate-mode 2D overlay renderer. Coordinates are in pixels with the
    // origin at the top-left. Call begin(), issue rect/icon/text, then end()
    // to flush. Draws with alpha blending and depth test disabled, on top of
    // whatever was already rendered.
    class UiRenderer {
    public:
        void init();
        void begin(int screenWidth, int screenHeight);

        void rect(float x, float y, float w, float h, const glm::vec4& color);

        // Textured quad sampling `atlas` over [uvMin, uvMax], tinted by `tint`.
        void icon(const Texture& atlas, float x, float y, float w, float h,
                  const glm::vec2& uvMin, const glm::vec2& uvMax,
                  const glm::vec4& tint = glm::vec4(1.0f));

        // Renders digits and spaces (other characters advance as a space).
        // `pixelHeight` is the glyph cell height; returns the advance width.
        float text(float x, float y, float pixelHeight, const std::string& s,
                   const glm::vec4& color);
        float textWidth(float pixelHeight, const std::string& s) const;

        void end();

    private:
        glm::vec2 toNdc(float px, float py) const;
        void pushQuad(std::vector<float>& out, float x, float y, float w, float h,
                      const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec4& c);

        int m_width = 1;
        int m_height = 1;

        Shader  m_shader;
        Mesh    m_mesh;
        Texture m_white; // 1x1 white texel for solid rects
        Texture m_font;  // digit glyph atlas

        const Texture* m_iconTex = nullptr;
        std::vector<float> m_solid;
        std::vector<float> m_icons;
        std::vector<float> m_text;
    };

} // namespace engine
