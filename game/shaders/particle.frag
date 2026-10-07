#version 330 core

// A soft round glow with a hot core, blended ADDITIVELY (GL_ONE, GL_ONE), so
// black contributes nothing and overlapping sparks brighten each other.
in vec2 vCorner;
in vec3 vColor;

out vec4 FragColor;

void main() {
    float r = length(vCorner);
    if (r >= 1.0) discard;
    float glow = (1.0 - r) * (1.0 - r);   // soft falloff to the edge
    float core = smoothstep(0.35, 0.0, r); // white-hot centre
    FragColor = vec4(vColor * glow + vec3(core) * 0.6 * length(vColor), 1.0);
}
