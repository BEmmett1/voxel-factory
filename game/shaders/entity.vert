#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aBone;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uBones[32]; // must match vg::kMaxEntityBones

out vec3 vNormal;
out vec2 vUv;

void main() {
    mat4 skin = uBones[int(aBone + 0.5)];
    gl_Position = uProj * uView * uModel * skin * vec4(aPos, 1.0);
    // Rotation + uniform scale only, so the plain 3x3 is fine for normals.
    vNormal = mat3(uModel) * mat3(skin) * aNormal;
    vUv = aUv;
}
