#version 330 core

in vec3 vNormal;
in vec3 vColor;

out vec4 FragColor;

uniform vec3 uLightDir;     // direction the light travels
uniform int  uUseFlatColor; // 1 = ignore lighting, draw uFlatColor (for the highlight)
uniform vec3 uFlatColor;

void main() {
    if (uUseFlatColor == 1) {
        FragColor = vec4(uFlatColor, 1.0);
        return;
    }
    float diffuse = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    float shade = 0.35 + 0.65 * diffuse; // ambient + directional
    FragColor = vec4(vColor * shade, 1.0);
}
