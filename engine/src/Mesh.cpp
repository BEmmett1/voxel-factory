#include "engine/Mesh.h"

#include <numeric>
#include <utility>

namespace engine {

    Mesh::~Mesh() {
        release();
    }

    Mesh::Mesh(Mesh&& other) noexcept
        : m_vao(other.m_vao), m_vbo(other.m_vbo), m_vertexCount(other.m_vertexCount) {
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_vertexCount = 0;
    }

    Mesh& Mesh::operator=(Mesh&& other) noexcept {
        if (this != &other) {
            release();
            m_vao = other.m_vao;
            m_vbo = other.m_vbo;
            m_vertexCount = other.m_vertexCount;
            other.m_vao = 0;
            other.m_vbo = 0;
            other.m_vertexCount = 0;
        }
        return *this;
    }

    void Mesh::release() {
        if (m_vbo) {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
        if (m_vao) {
            glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }
        m_vertexCount = 0;
    }

    void Mesh::upload(const std::vector<float>& data, const std::vector<int>& attributeSizes) {
        const int stride = std::accumulate(attributeSizes.begin(), attributeSizes.end(), 0);
        if (stride <= 0) {
            release();
            return;
        }
        m_vertexCount = static_cast<GLsizei>(data.size() / static_cast<std::size_t>(stride));

        if (!m_vao) glGenVertexArrays(1, &m_vao);
        if (!m_vbo) glGenBuffers(1, &m_vbo);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                     data.data(), GL_STATIC_DRAW);

        const GLsizei strideBytes = stride * static_cast<GLsizei>(sizeof(float));
        std::size_t offset = 0;
        for (GLuint i = 0; i < attributeSizes.size(); ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, attributeSizes[i], GL_FLOAT, GL_FALSE, strideBytes,
                                  reinterpret_cast<const void*>(offset * sizeof(float)));
            offset += static_cast<std::size_t>(attributeSizes[i]);
        }

        glBindVertexArray(0);
    }

    void Mesh::draw() const {
        if (m_vertexCount == 0) return;
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
        glBindVertexArray(0);
    }

} // namespace engine
