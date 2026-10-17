// Cuboid mesh builder: each Minecraft-style body part is a single rigid
// box (position/normal/color per vertex, one flat-shaded quad per face).
#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Appends a box centered at the origin (in the mesh's local space) with
// the given half-extents and flat color. Returns the index of the first
// index appended, so the caller can record a {firstIndex, indexCount}
// range for this box within a shared buffer.
inline uint32_t appendCuboid(Mesh& mesh, const glm::vec3& halfExtents, const glm::vec3& color) {
    uint32_t firstIndex = static_cast<uint32_t>(mesh.indices.size());

    const glm::vec3& h = halfExtents;
    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };
    const Face faces[6] = {
        { glm::vec3(0, 0, 1),  { {-h.x,-h.y, h.z}, { h.x,-h.y, h.z}, { h.x, h.y, h.z}, {-h.x, h.y, h.z} } }, // +Z
        { glm::vec3(0, 0, -1), { { h.x,-h.y,-h.z}, {-h.x,-h.y,-h.z}, {-h.x, h.y,-h.z}, { h.x, h.y,-h.z} } }, // -Z
        { glm::vec3(1, 0, 0),  { { h.x,-h.y, h.z}, { h.x,-h.y,-h.z}, { h.x, h.y,-h.z}, { h.x, h.y, h.z} } }, // +X
        { glm::vec3(-1, 0, 0), { {-h.x,-h.y,-h.z}, {-h.x,-h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y,-h.z} } }, // -X
        { glm::vec3(0, 1, 0),  { {-h.x, h.y, h.z}, { h.x, h.y, h.z}, { h.x, h.y,-h.z}, {-h.x, h.y,-h.z} } }, // +Y
        { glm::vec3(0, -1, 0), { {-h.x,-h.y,-h.z}, { h.x,-h.y,-h.z}, { h.x,-h.y, h.z}, {-h.x,-h.y, h.z} } }, // -Y
    };

    for (const Face& f : faces) {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back(Vertex{ f.corners[i], f.normal, color });
        }
        mesh.indices.insert(mesh.indices.end(), {
            base + 0, base + 1, base + 2,
            base + 0, base + 2, base + 3,
        });
    }

    return firstIndex;
}
