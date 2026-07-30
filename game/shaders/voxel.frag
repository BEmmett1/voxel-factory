#version 330 core

in vec3 vNormal;
in vec2 vUv;
in float vEmissive;

out vec4 FragColor;

uniform sampler2D uAtlas;
uniform vec3 uLightDir;     // direction the light travels
uniform float uRainDim;     // 0..~0.35: storm dimming of the lit color
uniform int  uUseFlatColor; // 1 = ignore texture/lighting, draw uFlatColor
uniform vec3 uFlatColor;

// Alpha CUTOUT, not alpha blending: a texel is either drawn or thrown away,
// never mixed. That is what keeps the world pass order-independent -- no depth
// sorting, no back-to-front traversal, no second pass -- which matters because
// chunks are drawn in hash-map order and belts/crops can be seen through each
// other from any angle. Blending would need all three of those.
//
// Every block tile in atlas.png and every packed rect in shapes.png is fully
// opaque today, so this is inert until a texture is authored WITH alpha (a
// crossed-plane crop, a glass tube). Nothing existing changes appearance.
const float kAlphaCutoff = 0.5;

void main() {
    if (uUseFlatColor == 1) {
        FragColor = vec4(uFlatColor, 1.0);
        return;
    }

    vec4 texel = texture(uAtlas, vUv);
    if (texel.a < kAlphaCutoff) discard;
    vec3 base = texel.rgb;

    float diffuse = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    float shade = 0.35 + 0.65 * diffuse; // ambient + directional

    // Energized power blocks self-illuminate so satisfied networks glow.
    // Emissive stays undimmed by rain, so glowing blocks read as beacons.
    vec3 color = base * shade * (1.0 - uRainDim) + vEmissive * base;
    FragColor = vec4(min(color, vec3(1.0)), 1.0);
}
