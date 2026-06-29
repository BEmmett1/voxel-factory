#include "engine/Shader.h"

#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace engine {

    namespace {
        std::string readFile(const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                std::fprintf(stderr, "Shader: could not open '%s'\n", path.c_str());
                return {};
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    } // namespace

    Shader::~Shader() {
        if (m_program) {
            glDeleteProgram(m_program);
        }
    }

    GLuint Shader::compile(GLenum type, const std::string& src) {
        const GLuint shader = glCreateShader(type);
        const char* csrc = src.c_str();
        glShaderSource(shader, 1, &csrc, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Shader compile error (%s):\n%s\n",
                         type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool Shader::loadFromSource(const std::string& vertSrc, const std::string& fragSrc) {
        const GLuint vs = compile(GL_VERTEX_SHADER, vertSrc);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return false;
        }

        const GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Shader link error:\n%s\n", log);
            glDeleteProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);

        if (m_program) {
            glDeleteProgram(m_program);
        }
        m_program = program;
        return true;
    }

    bool Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath) {
        const std::string vsrc = readFile(vertPath);
        const std::string fsrc = readFile(fragPath);
        if (vsrc.empty() || fsrc.empty()) {
            return false;
        }
        return loadFromSource(vsrc, fsrc);
    }

    void Shader::use() const {
        glUseProgram(m_program);
    }

    void Shader::setMat4(const char* name, const glm::mat4& m) const {
        glUniformMatrix4fv(glGetUniformLocation(m_program, name), 1, GL_FALSE, glm::value_ptr(m));
    }

    void Shader::setVec3(const char* name, const glm::vec3& v) const {
        glUniform3fv(glGetUniformLocation(m_program, name), 1, glm::value_ptr(v));
    }

    void Shader::setInt(const char* name, int v) const {
        glUniform1i(glGetUniformLocation(m_program, name), v);
    }

    void Shader::setFloat(const char* name, float v) const {
        glUniform1f(glGetUniformLocation(m_program, name), v);
    }

} // namespace engine
