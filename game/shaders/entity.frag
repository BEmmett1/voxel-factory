#version 330 core

in vec3 vNormal;
in vec2 vUv;

out vec4 FragColor;

uniform sampler2D uTex;
uniform vec3 uLightDir;  // direction the light travels (same as voxel.frag)
uniform float uRainDim;  // 0..~0.35: storm dimming of the lit color

void main() {
    vec3 base = texture(uTex, vUv).rgb;

    // Same ambient + directional shade as the world, so entities sit in the
    // exact same light as the blocks around them.
    float diffuse = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    float shade = 0.35 + 0.65 * diffuse;

    FragColor = vec4(base * shade * (1.0 - uRainDim), 1.0);
}
