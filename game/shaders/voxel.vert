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
// Moving block parts. aPartOff is this corner's offset FROM its part's pivot,
// baked by tools/bbmodel_to_shape.py; aPartSlot names the part's row in
// uPartRot. Only the shaped pass supplies them; every other mesh leaves them
// disabled, which reads as (0,0,0) and slot 0 -- both inert.
layout(location = 5) in vec3 aPartOff;
layout(location = 6) in float aPartSlot;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
// This frame's v-offset into an animated shape texture, one per ShapeId.
// Frames are bands stacked down shapes.png, so advancing one is a shift in v
// -- which is why a bubbling cauldron never costs a remesh.
uniform float uAnimV[32]; // must match vg::kMaxShapeBanks
// This frame's transform for every moving part, one per slot. Slot 0 is
// permanently identity, so a static vertex costs a multiply and moves nowhere.
// A 3x3 rather than a 4x4 because no translation is needed (see below) -- which
// leaves room for uniform SCALE in the same matrix, for a part that pulses
// rather than turns.
uniform mat3 uPartRot[32]; // must match vg::kMaxShapeParts
// ...and each slot's TRANSLATION, applied after the turn: a pestle plunging
// into its bowl is a move, which no 3x3 can say. Slot 0 is (0,0,0).
uniform vec3 uPartOff[32]; // must match vg::kMaxShapeParts

out vec3 vNormal;
out vec2 vUv;
out float vEmissive;

void main() {
    // A chunk vertex is WORLD-space, so its cell -- and therefore its part's
    // pivot -- is not recoverable here (floor(aPos) is not safe: geometry
    // touching a cell's top face lands in the next one up). It never has to be:
    // rotating p about `pivot` is p + (M*d - d) where d = p - pivot, and d is a
    // vector, identical in cell and world space. The bake supplies d.
    int slot = int(aPartSlot + 0.5);
    mat3 part = uPartRot[slot];
    vec3 pos = aPos + (part * aPartOff - aPartOff) + uPartOff[slot];

    gl_Position = uProj * uView * uModel * vec4(pos, 1.0);
    // The normal turns with the part, or a spinning drill would keep the
    // shading of the pose it was baked in. voxel.frag normalizes, so a part
    // matrix carrying uniform scale stays lit correctly.
    vNormal = part * aNormal;
    vUv = aUv + vec2(0.0, uAnimV[int(aAnimBank + 0.5)]);
    vEmissive = aEmissive;
}
