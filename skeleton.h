// A simple 7-bone rig shaped like a Minecraft character: 6 rigid body
// parts (head, body, left_arm, right_arm, left_leg, right_leg) plus a 7th,
// non-rendered "camera" bone that drives the view.
//
// Each bone has a rest-pose pivot (in its parent's local space) and, for
// the 6 body bones, a box mesh offset from that pivot (so rotations swing
// around the anatomical joint - shoulder, hip, neck - rather than the
// box's own center). An animation clip's rotation/position values are
// degrees-of-rotation / offset *deltas* added on top of the rest pose,
// consistent with the "relative": true flag in wave.json.
#pragma once

#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <array>
#include <string>

#include "animation.h"

enum BoneId {
    kBoneBody = 0, // root
    kBoneHead,
    kBoneLeftArm,
    kBoneRightArm,
    kBoneLeftLeg,
    kBoneRightLeg,
    kBoneCamera,
    kBoneCount
};

struct Bone {
    std::string name;
    int parent = -1;              // -1 = no parent (world space)
    glm::vec3 restPivot{ 0.0f };  // pivot position, relative to parent's pivot
    glm::vec3 restRotation{ 0.0f }; // rest-pose rotation, degrees XYZ
    glm::vec3 meshOffset{ 0.0f };   // box center, relative to this bone's own pivot
    glm::vec3 meshHalfExtents{ 0.0f }; // box half-size (x,y,z); zero for non-mesh bones (camera)
    glm::vec3 color{ 1.0f };
};

struct Skeleton {
    std::array<Bone, kBoneCount> bones;
    std::array<glm::mat4, kBoneCount> worldMatrix; // pivot-space -> world, after FK + animation

    Skeleton() {
        // Proportions mirror vanilla Minecraft's player model ratio
        // (legs 12px / body 12px / head 8px of 32px total height): here
        // legs 0.75, body 0.75, head 0.5, for a 2.0-unit-tall character.
        constexpr float legH = 0.75f, bodyH = 0.75f, headH = 0.5f;
        constexpr float bodyHalfW = 0.25f, bodyHalfD = 0.125f;
        constexpr float limbHalfW = 0.125f, limbHalfD = 0.125f;

        bones[kBoneBody] = Bone{
            "body", -1,
            glm::vec3(0.0f, legH, 0.0f),      // pivot at waist height
            glm::vec3(0.0f),
            glm::vec3(0.0f, bodyH * 0.5f, 0.0f),
            glm::vec3(bodyHalfW, bodyH * 0.5f, bodyHalfD),
            glm::vec3(0.25f, 0.55f, 0.85f) // blue shirt
        };
        bones[kBoneHead] = Bone{
            "head", kBoneBody,
            glm::vec3(0.0f, bodyH, 0.0f),      // top of body
            glm::vec3(0.0f),
            glm::vec3(0.0f, headH * 0.5f, 0.0f),
            glm::vec3(headH * 0.5f, headH * 0.5f, headH * 0.5f),
            glm::vec3(0.93f, 0.76f, 0.60f) // skin tone
        };
        bones[kBoneLeftArm] = Bone{
            "left_arm", kBoneBody,
            glm::vec3(-(bodyHalfW + limbHalfW), bodyH, 0.0f), // left shoulder
            glm::vec3(0.0f),
            glm::vec3(0.0f, -bodyH * 0.5f, 0.0f),
            glm::vec3(limbHalfW, bodyH * 0.5f, limbHalfD),
            glm::vec3(0.93f, 0.76f, 0.60f)
        };
        bones[kBoneRightArm] = Bone{
            "right_arm", kBoneBody,
            glm::vec3(bodyHalfW + limbHalfW, bodyH, 0.0f), // right shoulder
            glm::vec3(0.0f),
            glm::vec3(0.0f, -bodyH * 0.5f, 0.0f),
            glm::vec3(limbHalfW, bodyH * 0.5f, limbHalfD),
            glm::vec3(0.93f, 0.76f, 0.60f)
        };
        bones[kBoneLeftLeg] = Bone{
            "left_leg", kBoneBody,
            glm::vec3(-bodyHalfW + limbHalfW, 0.0f, 0.0f), // left hip (pivot = body's own pivot height)
            glm::vec3(0.0f),
            glm::vec3(0.0f, -legH * 0.5f, 0.0f),
            glm::vec3(limbHalfW, legH * 0.5f, limbHalfD),
            glm::vec3(0.20f, 0.25f, 0.55f) // trousers
        };
        bones[kBoneRightLeg] = Bone{
            "right_leg", kBoneBody,
            glm::vec3(bodyHalfW - limbHalfW, 0.0f, 0.0f), // right hip
            glm::vec3(0.0f),
            glm::vec3(0.0f, -legH * 0.5f, 0.0f),
            glm::vec3(limbHalfW, legH * 0.5f, limbHalfD),
            glm::vec3(0.20f, 0.25f, 0.55f)
        };
        // Camera: no parent, no mesh. Rest pose sits a few units back and
        // at head height, looking toward the character at the origin.
        bones[kBoneCamera] = Bone{
            "camera", -1,
            glm::vec3(0.0f, 1.4f, 4.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        };

        for (auto& m : worldMatrix) m = glm::mat4(1.0f);
    }

    static glm::mat4 eulerDegrees(const glm::vec3& degrees) {
        glm::vec3 r = glm::radians(degrees);
        return glm::eulerAngleXYZ(r.x, r.y, r.z);
    }

    // Applies the clip's per-bone rotation/position deltas on top of each
    // bone's rest pose and recomputes world matrices via the parent chain.
    void update(const AnimationClip& clip, float t) {
        std::array<glm::mat4, kBoneCount> local;
        for (int i = 0; i < kBoneCount; ++i) {
            const Bone& b = bones[i];
            glm::vec3 rotDelta = clip.sampleRotation(b.name, t);
            glm::vec3 posDelta = clip.samplePosition(b.name, t);
            glm::mat4 rot = eulerDegrees(b.restRotation + rotDelta);
            glm::vec3 pivot = b.restPivot + posDelta;
            local[i] = glm::translate(glm::mat4(1.0f), pivot) * rot;
        }

        for (int i = 0; i < kBoneCount; ++i) {
            int parent = bones[i].parent;
            worldMatrix[i] = (parent >= 0) ? worldMatrix[parent] * local[i] : local[i];
        }
    }

    // World transform of a bone's mesh box (pivot world transform * local mesh offset).
    glm::mat4 meshWorldMatrix(int boneId) const {
        return worldMatrix[boneId] * glm::translate(glm::mat4(1.0f), bones[boneId].meshOffset);
    }
};
