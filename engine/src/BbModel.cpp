#include "engine/BbModel.h"

// Vendored single-header JSON parser (MIT, third_party/json/). This is the
// only TU that includes it -- keep it out of headers.
#include <json.hpp>

#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using nlohmann::json;

namespace engine {

    namespace {

        // ---- Coordinate funnels ------------------------------------------
        // Every bbmodel -> world conversion goes through these, so if a model
        // renders mirrored against its Blockbench preview the fix is a sign
        // flip here, not a refactor. Blockbench: 16 units = 1 block, Y up,
        // models face "north" (-Z) -- matching the game's conventions, so the
        // initial mapping is a pure rescale.

        glm::vec3 geoToWorld(const glm::vec3& v) {
            return v / 16.0f;
        }

        glm::vec3 animPosToWorld(const glm::vec3& v) {
            return v / 16.0f;
        }

        glm::vec3 animRotToWorld(const glm::vec3& v) {
            return v;
        }

        // ---- Lenient JSON access -----------------------------------------
        // Blockbench writes numbers, numeric strings, and (in animation
        // data_points) simple molang like "0". Anything unparseable degrades
        // to `def`; the caller logs once per model via `warned`.

        float numOr(const json& v, float def, bool& warned) {
            if (v.is_number()) {
                return v.get<float>();
            }
            if (v.is_string()) {
                const std::string s = v.get<std::string>();
                char* end = nullptr;
                const float f = std::strtof(s.c_str(), &end);
                if (end != s.c_str()) {
                    return f;
                }
            }
            warned = true;
            return def;
        }

        glm::vec3 vec3Or(const json& v, const glm::vec3& def, bool& warned) {
            if (!v.is_array() || v.size() < 3) {
                return def;
            }
            return {numOr(v[0], def.x, warned), numOr(v[1], def.y, warned),
                    numOr(v[2], def.z, warned)};
        }

        // data_points entries are objects {x, y, z}.
        glm::vec3 dataPointOr(const json& v, bool& warned) {
            if (!v.is_object()) {
                warned = true;
                return glm::vec3(0.0f);
            }
            glm::vec3 out(0.0f);
            if (v.contains("x")) out.x = numOr(v["x"], 0.0f, warned);
            if (v.contains("y")) out.y = numOr(v["y"], 0.0f, warned);
            if (v.contains("z")) out.z = numOr(v["z"], 0.0f, warned);
            return out;
        }

        // ---- base64 (for embedded "data:image/png;base64,..." textures) ---

        bool base64Decode(const std::string& in, std::vector<unsigned char>& out) {
            auto val = [](char c) -> int {
                if (c >= 'A' && c <= 'Z') return c - 'A';
                if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                if (c >= '0' && c <= '9') return c - '0' + 52;
                if (c == '+') return 62;
                if (c == '/') return 63;
                return -1; // '=', whitespace, or junk
            };
            out.clear();
            out.reserve(in.size() * 3 / 4);
            int accum = 0, bits = 0;
            for (char c : in) {
                if (c == '=' ) break;
                const int v = val(c);
                if (v < 0) continue; // tolerate newlines/whitespace
                accum = (accum << 6) | v;
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    out.push_back(static_cast<unsigned char>((accum >> bits) & 0xFF));
                }
            }
            return !out.empty();
        }

        // ---- Geometry bake -------------------------------------------------

        // ZYX euler rotation matrix from degrees (Blockbench's order).
        glm::mat4 rotZYX(const glm::vec3& deg) {
            glm::mat4 m(1.0f);
            m = glm::rotate(m, glm::radians(deg.z), {0.0f, 0.0f, 1.0f});
            m = glm::rotate(m, glm::radians(deg.y), {0.0f, 1.0f, 0.0f});
            m = glm::rotate(m, glm::radians(deg.x), {1.0f, 0.0f, 0.0f});
            return m;
        }

        struct FaceDef {
            const char* name;
            glm::vec3   normal;
            // Corner selectors into {from(0) / to(1)} per axis, ordered
            // TL, TR, BR, BL as seen from outside the cuboid.
            int corners[4][3];
        };

        // Face layout follows the Minecraft/Blockbench convention: the UV
        // rect's top-left lands on the face's top-left as viewed from outside.
        const FaceDef kFaces[6] = {
            {"north", {0, 0, -1}, {{1, 1, 0}, {0, 1, 0}, {0, 0, 0}, {1, 0, 0}}},
            {"south", {0, 0, 1},  {{0, 1, 1}, {1, 1, 1}, {1, 0, 1}, {0, 0, 1}}},
            {"east",  {1, 0, 0},  {{1, 1, 1}, {1, 1, 0}, {1, 0, 0}, {1, 0, 1}}},
            {"west",  {-1, 0, 0}, {{0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {0, 0, 0}}},
            {"up",    {0, 1, 0},  {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}},
            {"down",  {0, -1, 0}, {{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}}},
        };

        void pushVertex(std::vector<float>& data, const glm::vec3& p,
                        const glm::vec3& n, float u, float v, float bone) {
            data.insert(data.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, v, bone});
        }

    } // namespace

    int BbModel::findAnimation(const std::string& name) const {
        for (std::size_t i = 0; i < animations.size(); ++i) {
            if (animations[i].name == name) return static_cast<int>(i);
        }
        // Bedrock-style names ("animation.model.walk") answer to their last
        // segment, so a model authored either way binds to the same "walk".
        // Second pass, so an exact name always wins.
        for (std::size_t i = 0; i < animations.size(); ++i) {
            const std::string& n = animations[i].name;
            const std::size_t dot = n.rfind('.');
            if (dot != std::string::npos &&
                n.compare(dot + 1, std::string::npos, name) == 0) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool loadBbModel(const std::string& path, BbModel& out, int maxBones) {
        out = BbModel{};

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            SDL_Log("BbModel: cannot open '%s'", path.c_str());
            return false;
        }
        const json doc = json::parse(file, nullptr, /*allow_exceptions=*/false);
        if (doc.is_discarded() || !doc.is_object()) {
            SDL_Log("BbModel: '%s' is not valid JSON", path.c_str());
            return false;
        }

        bool lenient = false;      // any value that needed the lenient parse
        bool skippedFaces = false; // any face without usable texture/uv
        bool oddAnim = false;      // unknown animator uuids / scale channels

        // UV space.
        float resW = 16.0f, resH = 16.0f;
        if (doc.contains("resolution") && doc["resolution"].is_object()) {
            resW = std::max(1.0f, numOr(doc["resolution"].value("width", json(16)), 16.0f, lenient));
            resH = std::max(1.0f, numOr(doc["resolution"].value("height", json(16)), 16.0f, lenient));
        }

        // ---- Bones: DFS over the outliner tree. --------------------------
        std::unordered_map<std::string, int> boneByUuid;    // group uuid -> bone
        std::unordered_map<std::string, int> boneByElement; // element uuid -> bone
        int syntheticRoot = -1; // lazily created for stray root-level elements

        // Blockbench 5.0 stripped the outliner to {uuid, children} and moved a
        // group's name/origin/rotation into a flat `groups` table; 4.x wrote
        // them inline. Read inline first, then the table, so one walk loads
        // either layout (and a 5.0 model's bones get their real pivots --
        // without them every limb would rotate about the model's origin).
        std::unordered_map<std::string, const json*> groupProps;
        if (doc.contains("groups") && doc["groups"].is_array()) {
            for (const json& g : doc["groups"]) {
                if (g.is_object() && g.contains("uuid") && g["uuid"].is_string()) {
                    groupProps.emplace(g["uuid"].get<std::string>(), &g);
                }
            }
        }
        auto groupField = [&](const json& node, const char* key) -> json {
            if (node.contains(key)) return node[key];
            if (node.contains("uuid") && node["uuid"].is_string()) {
                const auto it = groupProps.find(node["uuid"].get<std::string>());
                if (it != groupProps.end() && it->second->contains(key)) {
                    return (*it->second)[key];
                }
            }
            return json();
        };

        auto ensureSyntheticRoot = [&]() {
            if (syntheticRoot < 0) {
                BbBone root;
                root.name = "root";
                out.bones.push_back(root);
                syntheticRoot = static_cast<int>(out.bones.size()) - 1;
            }
            return syntheticRoot;
        };

        // Recursive group walk (iterative stack keeps it simple).
        std::function<void(const json&, int)> walk = [&](const json& node, int parent) {
            if (node.is_string()) { // a leaf element uuid
                const int bone = (parent >= 0) ? parent : ensureSyntheticRoot();
                boneByElement[node.get<std::string>()] = bone;
                return;
            }
            if (!node.is_object()) return;
            const json name = groupField(node, "name");
            BbBone bone;
            bone.name = name.is_string() ? name.get<std::string>() : std::string("bone");
            bone.parent = parent;
            bone.pivot = geoToWorld(
                vec3Or(groupField(node, "origin"), glm::vec3(0.0f), lenient));
            bone.restRotationDeg = animRotToWorld(
                vec3Or(groupField(node, "rotation"), glm::vec3(0.0f), lenient));
            out.bones.push_back(bone);
            const int index = static_cast<int>(out.bones.size()) - 1;
            if (node.contains("uuid") && node["uuid"].is_string()) {
                boneByUuid[node["uuid"].get<std::string>()] = index;
            }
            if (node.contains("children") && node["children"].is_array()) {
                for (const json& child : node["children"]) walk(child, index);
            }
        };
        if (doc.contains("outliner") && doc["outliner"].is_array()) {
            for (const json& node : doc["outliner"]) walk(node, -1);
        }
        if (static_cast<int>(out.bones.size()) > maxBones) {
            SDL_Log("BbModel: '%s' has %d bones (max %d)", path.c_str(),
                    static_cast<int>(out.bones.size()), maxBones);
            return false;
        }

        // ---- Geometry: bake each cuboid element into triangles. -----------
        if (doc.contains("elements") && doc["elements"].is_array()) {
            for (const json& el : doc["elements"]) {
                if (!el.is_object() || !el.contains("from") || !el.contains("to")) {
                    continue;
                }
                glm::vec3 from = vec3Or(el["from"], glm::vec3(0.0f), lenient);
                glm::vec3 to = vec3Or(el["to"], glm::vec3(0.0f), lenient);
                const float inflate = el.contains("inflate")
                    ? numOr(el["inflate"], 0.0f, lenient) : 0.0f;
                from -= glm::vec3(inflate);
                to += glm::vec3(inflate);

                // Static element rotation, baked into the vertices.
                glm::mat4 rot(1.0f);
                glm::vec3 origin(0.0f);
                const bool rotated = el.contains("rotation");
                if (rotated) {
                    origin = geoToWorld(vec3Or(el.value("origin", json::array()),
                                               glm::vec3(0.0f), lenient));
                    rot = rotZYX(vec3Or(el["rotation"], glm::vec3(0.0f), lenient));
                }

                int bone = 0;
                if (el.contains("uuid") && el["uuid"].is_string()) {
                    const auto it = boneByElement.find(el["uuid"].get<std::string>());
                    if (it != boneByElement.end()) bone = it->second;
                    else bone = ensureSyntheticRoot();
                } else {
                    bone = ensureSyntheticRoot();
                }

                const json faces = el.value("faces", json::object());
                for (const FaceDef& f : kFaces) {
                    if (!faces.contains(f.name) || !faces[f.name].is_object()) {
                        skippedFaces = true;
                        continue;
                    }
                    const json& face = faces[f.name];
                    if ((face.contains("texture") && face["texture"].is_null()) ||
                        !face.contains("uv") || !face["uv"].is_array() ||
                        face["uv"].size() < 4) {
                        skippedFaces = true;
                        continue;
                    }
                    const float u0 = numOr(face["uv"][0], 0.0f, lenient) / resW;
                    const float v0 = numOr(face["uv"][1], 0.0f, lenient) / resH;
                    const float u1 = numOr(face["uv"][2], 0.0f, lenient) / resW;
                    const float v1 = numOr(face["uv"][3], 0.0f, lenient) / resH;

                    glm::vec3 corner[4];
                    for (int c = 0; c < 4; ++c) {
                        const glm::vec3 raw(
                            f.corners[c][0] ? to.x : from.x,
                            f.corners[c][1] ? to.y : from.y,
                            f.corners[c][2] ? to.z : from.z);
                        glm::vec3 p = geoToWorld(raw);
                        if (rotated) {
                            p = origin + glm::vec3(rot * glm::vec4(p - origin, 1.0f));
                        }
                        corner[c] = p;
                    }
                    glm::vec3 n = f.normal;
                    if (rotated) {
                        n = glm::normalize(glm::vec3(rot * glm::vec4(n, 0.0f)));
                    }
                    const float uvs[4][2] = {
                        {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}; // TL TR BR BL
                    const float b = static_cast<float>(bone);
                    // Two CCW triangles as seen from outside: TL-BL-BR, TL-BR-TR.
                    const int tri[6] = {0, 3, 2, 0, 2, 1};
                    for (int idx : tri) {
                        pushVertex(out.vertexData, corner[idx], n,
                                   uvs[idx][0], uvs[idx][1], b);
                    }
                }
            }
        }

        // ---- Texture: first entry, embedded PNG data URI. ------------------
        if (doc.contains("textures") && doc["textures"].is_array() &&
            !doc["textures"].empty() && doc["textures"][0].is_object()) {
            const std::string src = doc["textures"][0].value("source", std::string());
            const std::string prefix = "data:image/png;base64,";
            if (src.rfind(prefix, 0) == 0) {
                std::vector<unsigned char> bytes;
                if (!base64Decode(src.substr(prefix.size()), bytes) ||
                    !loadImage(bytes.data(), bytes.size(), out.texture)) {
                    SDL_Log("BbModel: '%s' embedded texture failed to decode",
                            path.c_str());
                }
            } else if (!src.empty()) {
                SDL_Log("BbModel: '%s' texture is not an embedded PNG data URI",
                        path.c_str());
            }
        }

        // ---- Animations. ----------------------------------------------------
        if (doc.contains("animations") && doc["animations"].is_array()) {
            for (const json& an : doc["animations"]) {
                if (!an.is_object()) continue;
                BbAnimation anim;
                anim.name = an.value("name", std::string("animation"));
                anim.length = numOr(an.value("length", json(0)), 0.0f, lenient);
                anim.loop = an.value("loop", std::string("once")) == "loop";

                const json animators = an.value("animators", json::object());
                for (auto it = animators.begin(); it != animators.end(); ++it) {
                    const auto boneIt = boneByUuid.find(it.key());
                    if (boneIt == boneByUuid.end()) {
                        oddAnim = true; // effect/element animators etc.
                        continue;
                    }
                    BbBoneTrack track;
                    track.bone = boneIt->second;
                    const json keyframes = it.value().value("keyframes", json::array());
                    for (const json& kf : keyframes) {
                        if (!kf.is_object()) continue;
                        const std::string channel = kf.value("channel", std::string());
                        std::vector<BbKeyframe>* dest = nullptr;
                        bool isRotation = false;
                        if (channel == "rotation") { dest = &track.rotation; isRotation = true; }
                        else if (channel == "position") { dest = &track.position; }
                        else { oddAnim = true; continue; } // scale etc.

                        BbKeyframe key;
                        key.time = numOr(kf.value("time", json(0)), 0.0f, lenient);
                        const json points = kf.value("data_points", json::array());
                        const glm::vec3 raw = points.empty()
                            ? glm::vec3(0.0f) : dataPointOr(points[0], lenient);
                        key.value = isRotation ? animRotToWorld(raw) : animPosToWorld(raw);
                        const std::string interp =
                            kf.value("interpolation", std::string("linear"));
                        if (interp == "step") key.interp = BbKeyframe::Interp::Step;
                        else if (interp == "catmullrom") key.interp = BbKeyframe::Interp::CatmullRom;
                        else {
                            key.interp = BbKeyframe::Interp::Linear;
                            if (interp != "linear") oddAnim = true;
                        }
                        dest->push_back(key);
                    }
                    auto byTime = [](const BbKeyframe& a, const BbKeyframe& b) {
                        return a.time < b.time;
                    };
                    std::sort(track.rotation.begin(), track.rotation.end(), byTime);
                    std::sort(track.position.begin(), track.position.end(), byTime);
                    if (!track.rotation.empty() || !track.position.empty()) {
                        anim.tracks.push_back(std::move(track));
                    }
                }
                out.animations.push_back(std::move(anim));
            }
        }

        if (out.bones.empty()) {
            ensureSyntheticRoot(); // a model with no outliner still needs bone 0
        }
        if (lenient) {
            SDL_Log("BbModel: '%s' had non-numeric values (parsed leniently)", path.c_str());
        }
        if (skippedFaces) {
            SDL_Log("BbModel: '%s' has faces without texture/uv (skipped; "
                    "box-UV models are unsupported)", path.c_str());
        }
        if (oddAnim) {
            SDL_Log("BbModel: '%s' has unsupported animator/channel/interpolation "
                    "entries (degraded)", path.c_str());
        }
        if (out.vertexData.empty()) {
            SDL_Log("BbModel: '%s' produced no geometry", path.c_str());
            return false;
        }
        return true;
    }

    namespace {

        // Uniform Catmull-Rom through p1..p2, shaped by the neighbours p0/p3
        // (Blockbench's own `Math.catmullrom`).
        glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                             const glm::vec3& p2, const glm::vec3& p3, float t) {
            const float t2 = t * t, t3 = t2 * t;
            return 0.5f * (2.0f * p1 + (p2 - p0) * t +
                           (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                           (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
        }

        // Sample a keyframe track at time t: clamp outside the range, then
        // step / linear / Catmull-Rom between brackets. A smooth segment
        // borrows the keys on either side, duplicating the end ones at the
        // edges the way the editor's preview does.
        glm::vec3 sampleTrack(const std::vector<BbKeyframe>& keys, float t,
                              const glm::vec3& rest) {
            if (keys.empty()) return rest;
            if (t <= keys.front().time) return keys.front().value;
            if (t >= keys.back().time) return keys.back().value;
            std::size_t hi = 1;
            while (keys[hi].time < t) ++hi; // few keys; linear scan is fine
            const BbKeyframe& a = keys[hi - 1];
            const BbKeyframe& b = keys[hi];
            if (a.interp == BbKeyframe::Interp::Step) return a.value;
            const float span = b.time - a.time;
            const float f = (span > 0.0f) ? (t - a.time) / span : 0.0f;
            // Either end of the segment asking for smooth makes it smooth,
            // which is how Blockbench reads a mixed pair.
            if (a.interp == BbKeyframe::Interp::CatmullRom ||
                b.interp == BbKeyframe::Interp::CatmullRom) {
                const glm::vec3& p0 = keys[hi >= 2 ? hi - 2 : hi - 1].value;
                const glm::vec3& p3 =
                    keys[hi + 1 < keys.size() ? hi + 1 : hi].value;
                return catmullRom(p0, a.value, b.value, p3, f);
            }
            return glm::mix(a.value, b.value, f);
        }

    } // namespace

    void evaluateBbPose(const BbModel& model, int animIndex, float time,
                        std::vector<glm::mat4>& outBones) {
        const std::size_t n = model.bones.size();
        outBones.assign(n, glm::mat4(1.0f));

        const BbAnimation* anim =
            (animIndex >= 0 && animIndex < static_cast<int>(model.animations.size()))
                ? &model.animations[animIndex] : nullptr;
        float t = 0.0f;
        if (anim && anim->length > 0.0f) {
            t = anim->loop ? std::fmod(time, anim->length)
                           : std::min(time, anim->length);
        }

        // Per-bone animated offsets (rest pose when no track).
        std::vector<glm::vec3> rotDeg(n), posOff(n, glm::vec3(0.0f));
        for (std::size_t i = 0; i < n; ++i) rotDeg[i] = model.bones[i].restRotationDeg;
        if (anim) {
            for (const BbBoneTrack& track : anim->tracks) {
                if (track.bone < 0 || track.bone >= static_cast<int>(n)) continue;
                rotDeg[track.bone] = model.bones[track.bone].restRotationDeg +
                    sampleTrack(track.rotation, t, glm::vec3(0.0f));
                posOff[track.bone] = sampleTrack(track.position, t, glm::vec3(0.0f));
            }
        }

        // Compose down the hierarchy. DFS load order guarantees parent < i.
        // Vertices are baked in rest space, so with zero rotation/offset each
        // local collapses to identity -- no inverse-bind matrices needed.
        for (std::size_t i = 0; i < n; ++i) {
            const BbBone& bone = model.bones[i];
            glm::mat4 local =
                glm::translate(glm::mat4(1.0f), bone.pivot + posOff[i]);
            local = glm::rotate(local, glm::radians(rotDeg[i].z), {0.0f, 0.0f, 1.0f});
            local = glm::rotate(local, glm::radians(rotDeg[i].y), {0.0f, 1.0f, 0.0f});
            local = glm::rotate(local, glm::radians(rotDeg[i].x), {1.0f, 0.0f, 0.0f});
            local = glm::translate(local, -bone.pivot);
            outBones[i] = (bone.parent >= 0) ? outBones[bone.parent] * local : local;
        }
    }

} // namespace engine
