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

[[nodiscard]] bool sameRotation(
    const vortex::Quaternion a,
    const vortex::Quaternion b) {
    const auto na = vortex::normalizedQuaternion(a);
    const auto nb = vortex::normalizedQuaternion(b);
    if (!na || !nb) return false;
    const float dot = na->x * nb->x + na->y * nb->y + na->z * nb->z + na->w * nb->w;
    return std::abs(std::abs(dot) - 1.0F) <= 2.0e-5F;
}

} // namespace

int main() {
    vortex::Document document;
    const vortex::ObjectId objectId = document.createObject("Object");
    assert(objectId);

    const vortex::Vec3 legacyEuler{0.25F, -0.5F, 1.0F};
    const auto authoredRotation = vortex::quaternionFromEulerRadians(legacyEuler);
    assert(authoredRotation.has_value());

    vortex::ObjectTransform transform;
    transform.translation = {2.5F, -3.0F, 7.25F};
    transform.rotation = *authoredRotation;
    transform.scale = {1.5F, 0.75F, 2.25F};
    assert(document.setObjectTransform(objectId, transform));

    const auto encoded = vortex::ProjectCodec::encode(document);
    assert(encoded.ok());
    assert(encoded.bytes.size() > headerBytes + 64U);

    std::uint32_t encodedVersion = 0;
    std::memcpy(&encodedVersion, encoded.bytes.data() + 8U, sizeof(encodedVersion));
    assert(encodedVersion == vortex::ProjectCodec::schemaVersion);
    assert(encodedVersion == 3U);

    const auto decoded = vortex::ProjectCodec::decode(encoded.bytes);
    assert(decoded.ok());
    const vortex::ObjectBlock* loaded = decoded.document.object(objectId);
    assert(loaded != nullptr);
    assert(nearVec3(loaded->transform.translation, transform.translation));
    assert(sameRotation(loaded->transform.rotation, transform.rotation));
    assert(nearVec3(loaded->transform.scale, transform.scale));

    // A schema-v3 file with a valid checksum but non-finite authored transform is hostile.
    auto nonFinite = encoded.bytes;
    const std::size_t transformOffset = singleObjectTransformOffset(nonFinite);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(nonFinite.data() + transformOffset, &nan, sizeof(nan));
    refreshHeader(nonFinite);
    assert(vortex::ProjectCodec::decode(nonFinite).error == vortex::ProjectIoError::InvalidData);

    // Build a real schema-v2 stream by replacing the quaternion (4 floats) with the legacy
    // ZYX Euler triple (3 floats). The compatibility reader must migrate it to quaternion state.
    auto schemaV2 = encoded.bytes;
    const std::size_t v2TransformOffset = singleObjectTransformOffset(schemaV2);
    const std::size_t rotationOffset = v2TransformOffset + sizeof(vortex::Vec3);
    std::memcpy(schemaV2.data() + rotationOffset, &legacyEuler, sizeof(legacyEuler));
    schemaV2.erase(
        schemaV2.begin() + static_cast<std::ptrdiff_t>(rotationOffset + sizeof(legacyEuler)),
        schemaV2.begin() + static_cast<std::ptrdiff_t>(rotationOffset + sizeof(vortex::Quaternion)));
    writeU32(schemaV2, 8U, 2U);
    refreshHeader(schemaV2);

    const auto migratedV2 = vortex::ProjectCodec::decode(schemaV2);
    assert(migratedV2.ok());
    const vortex::ObjectBlock* migratedV2Object = migratedV2.document.object(objectId);
    assert(migratedV2Object != nullptr);
    assert(nearVec3(migratedV2Object->transform.translation, transform.translation));
    assert(sameRotation(migratedV2Object->transform.rotation, transform.rotation));
    assert(nearVec3(migratedV2Object->transform.scale, transform.scale));

    // Build schema v1 from the valid v2 stream by removing its nine authored transform floats.
    auto schemaV1 = schemaV2;
    const std::size_t v1TransformOffset = singleObjectTransformOffset(schemaV1);
    constexpr std::size_t legacyTransformBytes = sizeof(float) * 9U;
    schemaV1.erase(
        schemaV1.begin() + static_cast<std::ptrdiff_t>(v1TransformOffset),
        schemaV1.begin() + static_cast<std::ptrdiff_t>(v1TransformOffset + legacyTransformBytes));
    writeU32(schemaV1, 8U, 1U);
    refreshHeader(schemaV1);

    const auto migratedV1 = vortex::ProjectCodec::decode(schemaV1);
    assert(migratedV1.ok());
    const vortex::ObjectBlock* migratedV1Object = migratedV1.document.object(objectId);
    assert(migratedV1Object != nullptr);
    const vortex::ObjectTransform identity{};
    assert(migratedV1Object->transform == identity);
    assert(migratedV1.document.validate());

    std::cout << "Vortex3D project format v0.3 quaternion transform smoke passed\n";
    return 0;
}
