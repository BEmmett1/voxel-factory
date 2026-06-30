#include "engine/GL.h"

#include "engine/UiRenderer.h"

namespace engine {

    namespace {
        const char* kVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // already in NDC
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
out vec2 vUv;
out vec4 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
}
)";

        const char* kFrag = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTex;
void main() {
    FragColor = texture(uTex, vUv) * vColor;
}
)";

        // 5x7 bitmaps (one byte per row, low 5 bits). Order: digits 0-9,
        // letters A-Z, then a few symbols (see glyphIndex).
        const unsigned char kGlyph[][7] = {
            {14, 17, 19, 21, 25, 17, 14}, // 0
            { 4, 12,  4,  4,  4,  4, 14}, // 1
            {14, 17,  1,  2,  4,  8, 31}, // 2
            {31,  2,  4,  2,  1, 17, 14}, // 3
            { 2,  6, 10, 18, 31,  2,  2}, // 4
            {31, 16, 30,  1,  1, 17, 14}, // 5
            { 6,  8, 16, 30, 17, 17, 14}, // 6
            {31,  1,  2,  4,  8,  8,  8}, // 7
            {14, 17, 17, 14, 17, 17, 14}, // 8
            {14, 17, 17, 15,  1,  2, 12}, // 9
            {14, 17, 17, 31, 17, 17, 17}, // A
            {30, 17, 17, 30, 17, 17, 30}, // B
            {14, 17, 16, 16, 16, 17, 14}, // C
            {30, 17, 17, 17, 17, 17, 30}, // D
            {31, 16, 16, 30, 16, 16, 31}, // E
            {31, 16, 16, 30, 16, 16, 16}, // F
            {14, 17, 16, 23, 17, 17, 15}, // G
            {17, 17, 17, 31, 17, 17, 17}, // H
            {14,  4,  4,  4,  4,  4, 14}, // I
            { 7,  2,  2,  2,  2, 18, 12}, // J
            {17, 18, 20, 24, 20, 18, 17}, // K
            {16, 16, 16, 16, 16, 16, 31}, // L
            {17, 27, 21, 21, 17, 17, 17}, // M
            {17, 17, 25, 21, 19, 17, 17}, // N
            {14, 17, 17, 17, 17, 17, 14}, // O
            {30, 17, 17, 30, 16, 16, 16}, // P
            {14, 17, 17, 17, 21, 18, 13}, // Q
            {30, 17, 17, 30, 20, 18, 17}, // R
            {15, 16, 16, 14,  1,  1, 30}, // S
            {31,  4,  4,  4,  4,  4,  4}, // T
            {17, 17, 17, 17, 17, 17, 14}, // U
            {17, 17, 17, 17, 17, 10,  4}, // V
            {17, 17, 17, 21, 21, 27, 17}, // W
            {17, 17, 10,  4, 10, 17, 17}, // X
            {17, 17, 10,  4,  4,  4,  4}, // Y
            {31,  1,  2,  4,  8, 16, 31}, // Z
            { 0,  0,  0,  0,  0,  0,  0}, // space (36)
            { 2,  4,  8,  8,  8,  4,  2}, // (     (37)
            { 8,  4,  2,  2,  2,  4,  8}, // )     (38)
            { 0,  0,  0, 14,  0,  0,  0}, // -     (39)
            { 0,  4,  4,  0,  4,  4,  0}, // :     (40)
            { 1,  2,  2,  4,  8,  8, 16}, // /     (41)
            { 0,  0,  0,  0,  0,  4,  4}, // .     (42)
            { 0,  0,  0,  0,  4,  4,  8}, // ,     (43)
        };

        // Maps a character to its glyph index, or -1 if unsupported.
        int glyphIndex(char ch) {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
            if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a'); // lowercase -> uppercase glyph
            switch (ch) {
                case ' ': return 36;
                case '(': return 37;
                case ')': return 38;
                case '-': return 39;
                case ':': return 40;
                case '/': return 41;
                case '.': return 42;
                case ',': return 43;
                default:  return -1;
            }
        }

        constexpr int kCellW = 6;  // glyph (5) + 1px spacing
        constexpr int kCellH = 8;  // glyph (7) + 1px spacing
        constexpr int kGlyphs = 44;
        constexpr int kFontW = kCellW * kGlyphs;
        constexpr int kFontH = kCellH;
    } // namespace

    void UiRenderer::init() {
        m_shader.loadFromSource(kVert, kFrag);

        const unsigned char white[4] = {255, 255, 255, 255};
        m_white.createFromPixels(1, 1, white);

        // Build the digit font texture: white glyphs on a transparent field.
        std::vector<unsigned char> font(static_cast<std::size_t>(kFontW) * kFontH * 4, 0);
        for (int d = 0; d < kGlyphs; ++d) {
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    const bool on = (kGlyph[d][row] >> (4 - col)) & 1;
                    if (!on) continue;
                    const int x = d * kCellW + col;
                    const int y = row;
                    const std::size_t i = (static_cast<std::size_t>(y) * kFontW + x) * 4;
                    font[i + 0] = 255;
                    font[i + 1] = 255;
                    font[i + 2] = 255;
                    font[i + 3] = 255;
                }
            }
        }
        m_font.createFromPixels(kFontW, kFontH, font.data());
    }

    void UiRenderer::begin(int screenWidth, int screenHeight) {
        m_width = screenWidth > 0 ? screenWidth : 1;
        m_height = screenHeight > 0 ? screenHeight : 1;
        m_solid.clear();
        m_icons.clear();
        m_text.clear();
        m_iconTex = nullptr;
    }

    glm::vec2 UiRenderer::toNdc(float px, float py) const {
        return {px / static_cast<float>(m_width) * 2.0f - 1.0f,
                1.0f - py / static_cast<float>(m_height) * 2.0f};
    }

    void UiRenderer::pushQuad(std::vector<float>& out, float x, float y, float w, float h,
                              const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec4& c) {
        const glm::vec2 tl = toNdc(x, y);
        const glm::vec2 tr = toNdc(x + w, y);
        const glm::vec2 br = toNdc(x + w, y + h);
        const glm::vec2 bl = toNdc(x, y + h);

        auto v = [&](const glm::vec2& p, float u, float w2) {
            out.insert(out.end(), {p.x, p.y, u, w2, c.r, c.g, c.b, c.a});
        };
        v(tl, uv0.x, uv0.y); v(tr, uv1.x, uv0.y); v(br, uv1.x, uv1.y);
        v(tl, uv0.x, uv0.y); v(br, uv1.x, uv1.y); v(bl, uv0.x, uv1.y);
    }

    void UiRenderer::rect(float x, float y, float w, float h, const glm::vec4& color) {
        pushQuad(m_solid, x, y, w, h, {0.0f, 0.0f}, {1.0f, 1.0f}, color);
    }

    void UiRenderer::icon(const Texture& atlas, float x, float y, float w, float h,
                          const glm::vec2& uvMin, const glm::vec2& uvMax, const glm::vec4& tint) {
        m_iconTex = &atlas;
        pushQuad(m_icons, x, y, w, h, uvMin, uvMax, tint);
    }

    float UiRenderer::textWidth(float pixelHeight, const std::string& s) const {
        const float cw = pixelHeight * static_cast<float>(kCellW) / static_cast<float>(kCellH);
        return cw * static_cast<float>(s.size());
    }

    float UiRenderer::text(float x, float y, float pixelHeight, const std::string& s,
                           const glm::vec4& color) {
        const float cw = pixelHeight * static_cast<float>(kCellW) / static_cast<float>(kCellH);
        float cursor = x;
        for (char ch : s) {
            const int gi = glyphIndex(ch);
            if (gi >= 0) {
                const glm::vec2 uv0(static_cast<float>(gi * kCellW) / kFontW, 0.0f);
                const glm::vec2 uv1(static_cast<float>((gi + 1) * kCellW) / kFontW, 1.0f);
                pushQuad(m_text, cursor, y, cw, pixelHeight, uv0, uv1, color);
            }
            cursor += cw;
        }
        return cursor - x;
    }

    void UiRenderer::end() {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_shader.use();
        m_shader.setInt("uTex", 0);

        if (!m_solid.empty()) {
            m_white.bind(0);
            m_mesh.upload(m_solid, {2, 2, 4});
            m_mesh.draw();
        }
        if (!m_icons.empty() && m_iconTex) {
            m_iconTex->bind(0);
            m_mesh.upload(m_icons, {2, 2, 4});
            m_mesh.draw();
        }
        if (!m_text.empty()) {
            m_font.bind(0);
            m_mesh.upload(m_text, {2, 2, 4});
            m_mesh.draw();
        }

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

} // namespace engine
