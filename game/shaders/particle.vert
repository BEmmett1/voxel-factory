#version 330 core

// One corner of a camera-facing particle quad. The quad is built on the CPU
// (ParticleSystem::render), so this only projects it.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aCorner; // -1..1 across the quad
layout(location = 2) in vec3 aColor;  // already multiplied by the fade

uniform mat4 uView;
uniform mat4 uProj;

out vec2 vCorner;
out vec3 vColor;

void main() {
    vCorner = aCorner;
    vColor = aColor;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
