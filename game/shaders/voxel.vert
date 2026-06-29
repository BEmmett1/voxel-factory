#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aEmissive;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormal;
out vec2 vUv;
out float vEmissive;

void main() {
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
    vNormal = aNormal;
    vUv = aUv;
    vEmissive = aEmissive;
}
