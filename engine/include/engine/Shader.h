#pragma once

#include "engine/GL.h"

#include <glm/glm.hpp>
#include <string>

namespace engine {

    // A linked GLSL program. Compile from source strings or from files on disk.
    class Shader {
    public:
        Shader() = default;
        ~Shader();

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        bool loadFromSource(const std::string& vertSrc, const std::string& fragSrc);
        bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);

        void use() const;

        void setMat4(const char* name, const glm::mat4& m) const;
        void setMat4Array(const char* name, const glm::mat4* m, int count) const;
        void setVec3(const char* name, const glm::vec3& v) const;
        void setInt(const char* name, int v) const;
        void setFloat(const char* name, float v) const;
        void setFloatArray(const char* name, const float* v, int count) const;

        GLuint id() const { return m_program; }

    private:
        GLuint compile(GLenum type, const std::string& src);

        GLuint m_program = 0;
    };

} // namespace engine
