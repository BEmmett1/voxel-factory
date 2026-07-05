#pragma once

#include "engine/GL.h"

#include <vector>

namespace engine {

    // A GPU mesh: one interleaved vertex buffer drawn as triangles. The vertex
    // layout is described per-upload by a list of attribute component counts,
    // e.g. {3, 3, 3} for position + normal + color.
    class Mesh {
    public:
        Mesh() = default;
        ~Mesh();

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&& other) noexcept;
        Mesh& operator=(Mesh&& other) noexcept;

        // `usage` hints the driver how often the buffer will be re-uploaded
        // (GL_DYNAMIC_DRAW for meshes rebuilt during play, e.g. chunks).
        void upload(const std::vector<float>& data, const std::vector<int>& attributeSizes,
                    GLenum usage = GL_STATIC_DRAW);
        void draw(GLenum mode = GL_TRIANGLES) const;

        bool empty() const { return m_vertexCount == 0; }

    private:
        void release();

        GLuint  m_vao = 0;
        GLuint  m_vbo = 0;
        GLsizei m_vertexCount = 0;
    };

} // namespace engine
