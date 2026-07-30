#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aEmissive;
// Which shape this vertex animates as (a ShapeId), naming its row in uAnimV.
// Only the shaped pass supplies it; every other mesh leaves the attribute
// disabled, which reads as 0 -- ShapeId::FullCube, whose offset is always 0.
// The mesher also passes 0 for an UNPOWERED machine, parking it on frame 0.
layout(location = 4) in float aAnimBank;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
// This frame's v-offset into an animated shape texture, one per ShapeId.
// Frames are bands stacked down shapes.png, so advancing one is a shift in v
// -- which is why a bubbling cauldron never costs a remesh.
uniform float uAnimV[32]; // must match vg::kMaxShapeBanks

out vec3 vNormal;
out vec2 vUv;
out float vEmissive;

void main() {
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
    vNormal = aNormal;
    vUv = aUv + vec2(0.0, uAnimV[int(aAnimBank + 0.5)]);
    vEmissive = aEmissive;
}
