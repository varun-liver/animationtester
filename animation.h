// Loads and samples an animationlib-style animation clip (the same
// Bedrock/Blockbench-ish JSON schema used by the real Minecraft mod at
// assets/animationtest/animationlib/animations/*.json): top-level
// "animations" object, each entry keyed by name, with "animation_length",
// "loop", and "bones.<name>.{rotation,position}" channels keyed by
// timestamp-in-seconds, values [x,y,z].
#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_mini.h"

struct Keyframe {
    float time = 0.0f;
    glm::vec3 value{ 0.0f };
};

// One channel (rotation or position) of keyframes for a single bone,
// sorted by time.
struct BoneChannel {
    std::vector<Keyframe> keyframes;

    bool empty() const { return keyframes.empty(); }

    // Linear interpolation; holds the first/last value outside the range.
    glm::vec3 sample(float t) const {
        if (keyframes.empty()) return glm::vec3(0.0f);
        if (t <= keyframes.front().time) return keyframes.front().value;
        if (t >= keyframes.back().time) return keyframes.back().value;
        for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
            const Keyframe& a = keyframes[i];
            const Keyframe& b = keyframes[i + 1];
            if (t >= a.time && t <= b.time) {
                float span = b.time - a.time;
                float f = span > 0.0f ? (t - a.time) / span : 0.0f;
                return glm::mix(a.value, b.value, f);
            }
        }
        return keyframes.back().value;
    }

    // Inserts a new keyframe at `time`, or overwrites the value of an
    // existing one within epsilon of it (editing in place rather than
    // creating a near-duplicate). Keeps keyframes sorted by time. Returns
    // the index of the inserted/updated keyframe.
    size_t upsert(float time, const glm::vec3& value, float epsilon = 1e-3f) {
        for (size_t i = 0; i < keyframes.size(); ++i) {
            if (std::fabs(keyframes[i].time - time) <= epsilon) {
                keyframes[i].value = value;
                return i;
            }
        }
        keyframes.push_back(Keyframe{ time, value });
        std::sort(keyframes.begin(), keyframes.end(),
            [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
        for (size_t i = 0; i < keyframes.size(); ++i) {
            if (std::fabs(keyframes[i].time - time) <= epsilon) return i;
        }
        return keyframes.size() - 1;
    }

    // Retimes the keyframe at `index` to `newTime` (clamped by the caller)
    // and re-sorts; returns the keyframe's new index.
    size_t retime(size_t index, float newTime) {
        keyframes[index].time = newTime;
        std::sort(keyframes.begin(), keyframes.end(),
            [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
        for (size_t i = 0; i < keyframes.size(); ++i) {
            if (std::fabs(keyframes[i].time - newTime) < 1e-6f) return i;
        }
        return index;
    }

    void eraseAt(size_t index) {
        if (index < keyframes.size()) keyframes.erase(keyframes.begin() + static_cast<long>(index));
    }

    // Index of the keyframe nearest `time`, or -1 if the channel is empty.
    int nearestIndex(float time) const {
        if (keyframes.empty()) return -1;
        int best = 0;
        float bestDist = std::fabs(keyframes[0].time - time);
        for (size_t i = 1; i < keyframes.size(); ++i) {
            float dist = std::fabs(keyframes[i].time - time);
            if (dist < bestDist) { bestDist = dist; best = static_cast<int>(i); }
        }
        return best;
    }
};

struct BoneAnimation {
    BoneChannel rotation; // Euler degrees, XYZ, delta from rest pose.
    BoneChannel position; // Offset from rest pose.
};

struct AnimationClip {
    std::string name;
    float length = 0.0f;
    bool loop = false;
    std::map<std::string, BoneAnimation> bones;

    // Clip-level playback options, editable from the Properties sidebar:
    // force_third_person, hide_inventory, and cameraFollowsPlayer are all
    // read by animationlib's JSON loader (see AnimationData.java).
    bool forceThirdPerson = false;
    bool hideInventory = false;
    std::string cameraFollowsPlayer;

    glm::vec3 sampleRotation(const std::string& bone, float t) const {
        auto it = bones.find(bone);
        if (it == bones.end() || it->second.rotation.empty()) return glm::vec3(0.0f);
        return it->second.rotation.sample(t);
    }

    glm::vec3 samplePosition(const std::string& bone, float t) const {
        auto it = bones.find(bone);
        if (it == bones.end() || it->second.position.empty()) return glm::vec3(0.0f);
        return it->second.position.sample(t);
    }

    // Fetches (creating an empty one if needed) the rotation or position
    // channel for a bone, for the sidebar/timeline to edit in place.
    BoneChannel& rotationChannel(const std::string& bone) { return bones[bone].rotation; }
    BoneChannel& positionChannel(const std::string& bone) { return bones[bone].position; }

    // The latest keyframe time across every bone's rotation/position
    // channel - the floor `length` is never allowed to drop below, so
    // dragging the timeline's end marker can extend the hold past the
    // last keyframe but never hide one. Falls back to `length` itself
    // when there are no keyframes yet (e.g. a brand-new clip).
    float computedEndTime() const {
        float maxTime = 0.0f;
        bool any = false;
        for (auto& kv : bones) {
            for (auto& kf : kv.second.rotation.keyframes) { maxTime = std::max(maxTime, kf.time); any = true; }
            for (auto& kf : kv.second.position.keyframes) { maxTime = std::max(maxTime, kf.time); any = true; }
        }
        return any ? maxTime : length;
    }

    // Keeps `length` (the draggable, authoritative animation end, written
    // out as "animation_length") from ever being shorter than the latest
    // real keyframe. Call after any edit that might move a keyframe
    // further out; never shrinks `length` on its own.
    void ensureLengthCoversKeyframes() {
        length = std::max(length, computedEndTime());
    }
};

inline BoneChannel parseChannel(const JsonValue* channelJson) {
    BoneChannel channel;
    if (!channelJson || !channelJson->isObject()) return channel;
    for (auto& kv : channelJson->objectValue) {
        Keyframe kf;
        kf.time = std::stof(kv.first);
        const JsonValue& arr = kv.second;
        if (arr.isArray() && arr.arrayValue.size() >= 3) {
            kf.value = glm::vec3(
                static_cast<float>(arr.arrayValue[0].asNumber()),
                static_cast<float>(arr.arrayValue[1].asNumber()),
                static_cast<float>(arr.arrayValue[2].asNumber()));
        } else if (arr.isObject()) {
            // {"pre": [...], "post": [...], "lerp_mode": "..."} discontinuity
            // form: use "post" (the value immediately after this time).
            if (const JsonValue* post = arr.find("post")) {
                if (post->isArray() && post->arrayValue.size() >= 3) {
                    kf.value = glm::vec3(
                        static_cast<float>(post->arrayValue[0].asNumber()),
                        static_cast<float>(post->arrayValue[1].asNumber()),
                        static_cast<float>(post->arrayValue[2].asNumber()));
                }
            }
        }
        channel.keyframes.push_back(kf);
    }
    std::sort(channel.keyframes.begin(), channel.keyframes.end(),
        [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
    return channel;
}

inline AnimationClip loadAnimation(const std::string& path, const std::string& clipName) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("failed to open animation file: " + path);
    std::ostringstream ss;
    ss << file.rdbuf();

    JsonValue root = parseJson(ss.str());
    const JsonValue* animations = root.find("animations");
    if (!animations) throw std::runtime_error("animation file has no \"animations\" object: " + path);
    const JsonValue* clipJson = animations->find(clipName);
    if (!clipJson) throw std::runtime_error("animation \"" + clipName + "\" not found in " + path);

    AnimationClip clip;
    clip.name = clipName;
    clip.length = static_cast<float>(clipJson->find("animation_length") ? clipJson->find("animation_length")->asNumber() : 0.0);
    clip.loop = clipJson->find("loop") ? clipJson->find("loop")->asBool() : false;
    clip.forceThirdPerson = clipJson->find("force_third_person") ? clipJson->find("force_third_person")->asBool() : false;
    clip.hideInventory = clipJson->find("hide_inventory") ? clipJson->find("hide_inventory")->asBool() : false;
    if (const JsonValue* followJson = clipJson->find("cameraFollowsPlayer")) clip.cameraFollowsPlayer = followJson->asString();

    const JsonValue* bonesJson = clipJson->find("bones");
    if (bonesJson && bonesJson->isObject()) {
        for (auto& kv : bonesJson->objectValue) {
            BoneAnimation anim;
            anim.rotation = parseChannel(kv.second.find("rotation"));
            anim.position = parseChannel(kv.second.find("position"));
            clip.bones[kv.first] = anim;
        }
    }

    std::printf("Loaded animation \"%s\" (length=%.2fs, loop=%s, bones=%zu):\n",
        clip.name.c_str(), clip.length, clip.loop ? "true" : "false", clip.bones.size());
    for (auto& kv : clip.bones) {
        std::printf("  %-12s rotation keys=%zu  position keys=%zu\n",
            kv.first.c_str(), kv.second.rotation.keyframes.size(), kv.second.position.keyframes.size());
    }

    return clip;
}

// Peeks the first clip name under an animation file's top-level
// "animations" object, without fully parsing it into an AnimationClip.
inline std::string firstClipName(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("failed to open animation file: " + path);
    std::ostringstream ss;
    ss << file.rdbuf();

    JsonValue root = parseJson(ss.str());
    const JsonValue* animations = root.find("animations");
    if (!animations || animations->objectValue.empty()) {
        throw std::runtime_error("animation file has no \"animations\" entries: " + path);
    }
    return animations->objectValue.front().first;
}

// %g-formats a float the way this writer wants its JSON numbers: compact,
// with a trailing ".0" when it would otherwise look like an integer (so
// keyframe-time keys stay unambiguous, matching wave.json's own style).
inline std::string formatNumber(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    std::string s = buf;
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
    return s;
}

inline void writeChannel(std::ostream& out, const char* key, const BoneChannel& channel, bool trailingComma) {
    out << "\t\t\t\t\t\"" << key << "\": {\n";
    for (size_t i = 0; i < channel.keyframes.size(); ++i) {
        const Keyframe& kf = channel.keyframes[i];
        out << "\t\t\t\t\t\t\"" << formatNumber(kf.time) << "\": ["
            << formatNumber(kf.value.x) << ", " << formatNumber(kf.value.y) << ", " << formatNumber(kf.value.z) << "]"
            << (i + 1 < channel.keyframes.size() ? "," : "") << "\n";
    }
    out << "\t\t\t\t\t}" << (trailingComma ? "," : "") << "\n";
}

// Writes `clip` out as a standalone animation JSON file, in the same
// {"animations": {"<name>": {"animation_length", "loop", "bones"}}} shape
// animationlib's AnimationLoader reads - regenerated from the in-memory
// clip only (per the user's choice, this drops any other top-level fields
// or sibling clips an originally-loaded file had).
// Minimal JSON string escaping for user-typed text (cameraFollowsPlayer).
inline std::string escapeJsonString(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

inline void saveAnimation(const std::string& path, const AnimationClip& clip) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("failed to open animation file for writing: " + path);

    out << "{\n\t\"animations\": {\n\t\t\"" << clip.name << "\": {\n";
    out << "\t\t\t\"animation_length\": " << formatNumber(clip.length) << ",\n";
    out << "\t\t\t\"loop\": " << (clip.loop ? "true" : "false") << ",\n";
    out << "\t\t\t\"force_third_person\": " << (clip.forceThirdPerson ? "true" : "false") << ",\n";
    out << "\t\t\t\"hide_inventory\": " << (clip.hideInventory ? "true" : "false") << ",\n";
    if (!clip.cameraFollowsPlayer.empty()) {
        out << "\t\t\t\"cameraFollowsPlayer\": \"" << escapeJsonString(clip.cameraFollowsPlayer) << "\",\n";
    }
    out << "\t\t\t\"bones\": {\n";

    std::vector<const std::pair<const std::string, BoneAnimation>*> nonEmpty;
    for (auto& kv : clip.bones) {
        if (!kv.second.rotation.empty() || !kv.second.position.empty()) nonEmpty.push_back(&kv);
    }
    for (size_t b = 0; b < nonEmpty.size(); ++b) {
        const std::string& boneName = nonEmpty[b]->first;
        const BoneAnimation& anim = nonEmpty[b]->second;
        bool hasRotation = !anim.rotation.empty();
        bool hasPosition = !anim.position.empty();

        out << "\t\t\t\t\"" << boneName << "\": {\n";
        if (hasRotation) writeChannel(out, "rotation", anim.rotation, hasPosition);
        if (hasPosition) writeChannel(out, "position", anim.position, false);
        out << "\t\t\t\t}" << (b + 1 < nonEmpty.size() ? "," : "") << "\n";
    }

    out << "\t\t\t}\n\t\t}\n\t}\n}\n";
}
