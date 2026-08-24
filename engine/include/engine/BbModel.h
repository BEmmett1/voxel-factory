#pragma once

#include "engine/Image.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {

    // A Blockbench model (.bbmodel, the editor's native JSON): textured
    // cuboids rigidly attached to a bone hierarchy, plus keyframe animations.
    // Loaded into plain data here; the JSON parser lives only in BbModel.cpp.
    //
    // Conventions after load: distances are in world blocks (Blockbench's
    // 16 units = 1 block), rotations in degrees. Faces are read from their
    // own `uv` rect, which the editor writes for box-UV models too — a face
    // without one logs and is skipped. Textures must be embedded PNG data
    // URIs (the .bbmodel default); a failed texture leaves `texture` empty
    // and the caller substitutes a fallback. Both outliner layouts load:
    // 4.x wrote a group's name/origin/rotation inline, 5.0 keeps them in a
    // flat `groups` table the outliner references by uuid.

    struct BbBone {
        std::string name;
        int         parent = -1;      // index into bones; -1 = root.
                                      // DFS order guarantees parent < child.
        glm::vec3   pivot{0.0f};      // rotation origin, block units
        glm::vec3   restRotationDeg{0.0f};
    };

    struct BbKeyframe {
        // CatmullRom is Blockbench's "smooth" keyframe: a spline through the
        // neighbouring values rather than a straight line between two.
        enum class Interp { Linear, Step, CatmullRom };
        float     time = 0.0f;        // seconds
        glm::vec3 value{0.0f};        // rotation: degrees; position: blocks
        Interp    interp = Interp::Linear;
    };

    struct BbBoneTrack {
        int bone = -1;
        std::vector<BbKeyframe> rotation; // sorted by time
        std::vector<BbKeyframe> position; // sorted by time
    };

    struct BbAnimation {
        std::string name;
        float       length = 0.0f;    // seconds
        bool        loop = true;      // wrap (vs. clamp at length)
        std::vector<BbBoneTrack> tracks;
    };

    struct BbModel {
        std::vector<BbBone>      bones;
        // Interleaved triangles: {3 pos, 3 normal, 2 uv, 1 boneIndex}.
        std::vector<float>       vertexData;
        Image                    texture;   // empty rgba => use a fallback
        std::vector<BbAnimation> animations;

        int findAnimation(const std::string& name) const;
    };

    // Parse a .bbmodel file. Returns false (with SDL_Log reasons) on a
    // missing file, malformed JSON, or more than maxBones bones; lenient on
    // everything else (bad faces/textures/keyframes log and degrade).
    bool loadBbModel(const std::string& path, BbModel& out, int maxBones = 32);

    // Sample animation `animIndex` at `time` seconds into one skinning matrix
    // per bone (outBones is resized to bones.size()). animIndex -1, or an
    // animation with no track for a bone, yields that bone's rest pose.
    void evaluateBbPose(const BbModel& model, int animIndex, float time,
                        std::vector<glm::mat4>& outBones);

} // namespace engine
