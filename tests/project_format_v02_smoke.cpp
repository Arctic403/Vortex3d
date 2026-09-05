#include "vortex/project/project_io.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr std::size_t headerBytes = 28U;

void writeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    assert(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void writeU64(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint64_t value) {
    assert(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::uint64_t readU64(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    assert(offset + sizeof(std::uint64_t) <= bytes.size());
    std::uint64_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    offset += sizeof(value);
    return value;
}

std::uint64_t checksum(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = headerBytes; index < bytes.size(); ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void refreshHeader(std::vector<std::uint8_t>& bytes) {
    writeU64(bytes, 12U, static_cast<std::uint64_t>(bytes.size() - headerBytes));
    writeU64(bytes, 20U, checksum(bytes));
}

std::size_t singleObjectTransformOffset(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = headerBytes;
    offset += sizeof(std::uint64_t) * 3U; // DocumentId, nextId, revision.
    assert(readU64(bytes, offset) == 0U); // scenes
    assert(readU64(bytes, offset) == 0U); // collections
    assert(readU64(bytes, offset) == 0U); // meshes
    assert(readU64(bytes, offset) == 1U); // objects
    offset += sizeof(std::uint64_t); // ObjectId.
    const std::uint64_t nameBytes = readU64(bytes, offset);
    assert(nameBytes <= bytes.size() - offset);
    offset += static_cast<std::size_t>(nameBytes);
    offset += sizeof(std::uint64_t) * 2U; // MeshId + parentId.
    return offset;
}

[[nodiscard]] bool nearVec3(const vortex::Vec3 a, const vortex::Vec3 b) {
    constexpr float epsilon = 1.0e-6F;
    return std::abs(a.x - b.x) <= epsilon &&
           std::abs(a.y - b.y) <= epsilon &&
           std::abs(a.z - b.z) <= epsilon;
}

} // namespace

int main() {
    vortex::Document document;
    const vortex::ObjectId objectId = document.createObject("Object");
    assert(objectId);

    vortex::ObjectTransform transform;
    transform.translation = {2.5F, -3.0F, 7.25F};
    transform.rotationRadians = {0.25F, -0.5F, 1.0F};
    transform.scale = {1.5F, 0.75F, 2.25F};
    assert(document.setObjectTransform(objectId, transform));

    const auto encoded = vortex::ProjectCodec::encode(document);
    assert(encoded.ok());
    assert(encoded.bytes.size() > headerBytes + 64U);

    std::uint32_t encodedVersion = 0;
    std::memcpy(&encodedVersion, encoded.bytes.data() + 8U, sizeof(encodedVersion));
    assert(encodedVersion == vortex::ProjectCodec::schemaVersion);
    assert(encodedVersion == 2U);

    const auto decoded = vortex::ProjectCodec::decode(encoded.bytes);
    assert(decoded.ok());
    const vortex::ObjectBlock* loaded = decoded.document.object(objectId);
    assert(loaded != nullptr);
    assert(nearVec3(loaded->transform.translation, transform.translation));
    assert(nearVec3(loaded->transform.rotationRadians, transform.rotationRadians));
    assert(nearVec3(loaded->transform.scale, transform.scale));

    // A schema-v2 file with a valid checksum but non-finite authored transform is hostile.
    auto nonFinite = encoded.bytes;
    const std::size_t transformOffset = singleObjectTransformOffset(nonFinite);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(nonFinite.data() + transformOffset, &nan, sizeof(nan));
    refreshHeader(nonFinite);
    assert(vortex::ProjectCodec::decode(nonFinite).error == vortex::ProjectIoError::InvalidData);

    // Build a real schema-v1 byte stream from the minimal v2 stream by removing the nine
    // transform floats. The compatibility reader must supply an identity transform rather
    // than shifting the legacy revision field or rejecting a valid old project.
    auto schemaV1 = encoded.bytes;
    const std::size_t v1TransformOffset = singleObjectTransformOffset(schemaV1);
    constexpr std::size_t transformBytes = sizeof(float) * 9U;
    schemaV1.erase(
        schemaV1.begin() + static_cast<std::ptrdiff_t>(v1TransformOffset),
        schemaV1.begin() + static_cast<std::ptrdiff_t>(v1TransformOffset + transformBytes));
    writeU32(schemaV1, 8U, 1U);
    refreshHeader(schemaV1);

    const auto migrated = vortex::ProjectCodec::decode(schemaV1);
    assert(migrated.ok());
    const vortex::ObjectBlock* migratedObject = migrated.document.object(objectId);
    assert(migratedObject != nullptr);
    const vortex::ObjectTransform identity{};
    assert(migratedObject->transform == identity);
    assert(migrated.document.validate());

    std::cout << "Vortex3D project format v0.2 transform smoke passed\n";
    return 0;
}
