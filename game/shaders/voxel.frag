#version 330 core

in vec3 vNormal;
in vec2 vUv;
in float vEmissive;

out vec4 FragColor;

uniform sampler2D uAtlas;
uniform vec3 uLightDir;     // direction the light travels
uniform int  uUseFlatColor; // 1 = ignore texture/lighting, draw uFlatColor
uniform vec3 uFlatColor;

void main() {
    if (uUseFlatColor == 1) {
        FragColor = vec4(uFlatColor, 1.0);
        return;
    }

    vec3 base = texture(uAtlas, vUv).rgb;

    float diffuse = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    float shade = 0.35 + 0.65 * diffuse; // ambient + directional

    // Energized power blocks self-illuminate so satisfied networks glow.
    vec3 color = base * shade + vEmissive * base;
    FragColor = vec4(min(color, vec3(1.0)), 1.0);
}
