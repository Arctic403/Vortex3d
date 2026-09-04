#include "vortex/engine.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr std::size_t projectHeaderBytes = 28U;

std::uint64_t readU64(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    assert(offset + sizeof(std::uint64_t) <= bytes.size());
    std::uint64_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    offset += sizeof(value);
    return value;
}

void writeU64(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint64_t value) {
    assert(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void skipString(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    const std::uint64_t size = readU64(bytes, offset);
    assert(size <= bytes.size() - offset);
    offset += static_cast<std::size_t>(size);
}

std::uint64_t payloadChecksumWithOffset(
    const std::vector<std::uint8_t>& bytes,
    const std::uint64_t offset) {
    std::uint64_t hash = offset;
    for (std::size_t index = projectHeaderBytes; index < bytes.size(); ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t payloadChecksum(const std::vector<std::uint8_t>& bytes) {
    return payloadChecksumWithOffset(bytes, 14695981039346656037ULL);
}

std::uint64_t legacyPayloadChecksum(const std::vector<std::uint8_t>& bytes) {
    return payloadChecksumWithOffset(bytes, 1469598103934665603ULL);
}

void refreshChecksum(std::vector<std::uint8_t>& bytes) {
    writeU64(bytes, 20U, payloadChecksum(bytes));
}

std::pair<std::size_t, std::uint64_t> firstCollectionParentField(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = projectHeaderBytes;
    offset += sizeof(std::uint64_t) * 3U; // DocumentId, nextId, revision.

    const std::uint64_t sceneCount = readU64(bytes, offset);
    for (std::uint64_t index = 0; index < sceneCount; ++index) {
        offset += sizeof(std::uint64_t); // SceneId.
        skipString(bytes, offset);
        offset += sizeof(std::uint64_t) * 2U; // root collection + revision.
    }

    const std::uint64_t collectionCount = readU64(bytes, offset);
    assert(collectionCount > 0U);
    const std::uint64_t collectionId = readU64(bytes, offset);
    skipString(bytes, offset);
    offset += sizeof(std::uint64_t); // SceneId.
    return {offset, collectionId};
}

} // namespace

int main() {
    vortex::EditableMesh mesh;
    const auto a=mesh.addVertex({0.0F,0.0F,0.0F});
    const auto b=mesh.addVertex({2.0F,0.0F,0.0F});
    const auto c=mesh.addVertex({2.0F,2.0F,0.0F});
    const auto d=mesh.addVertex({0.0F,2.0F,0.0F});
    const auto face=mesh.addFace({a,b,c,d});
    assert(face);
    const auto temporary=mesh.addVertex({9.0F,9.0F,9.0F});
    assert(temporary && mesh.removeVertex(temporary));
    const auto postGap=mesh.addVertex({4.0F,4.0F,0.0F});
    assert(postGap && postGap.value() > temporary.value());
    assert(mesh.attributes().create<float>("weight", vortex::AttributeDomain::Vertex, 0.5F));
    auto* weights=mesh.attributes().values<float>("weight", vortex::AttributeDomain::Vertex);
    assert(weights && weights->size()==5U);
    (*weights)[2]=0.9F;

    vortex::Document document;
    const auto scene=document.createScene("Scene");
    const auto meshId=document.createMesh("Shared Mesh", std::move(mesh));
    const auto parent=document.createObject("Parent",meshId);
    const auto child=document.createObject("Child",meshId);
    assert(scene&&meshId&&parent&&child);
    assert(document.setObjectParent(child,parent));
    assert(document.linkObjectToCollection(parent,document.scene(scene)->rootCollectionId));
    assert(document.linkObjectToCollection(child,document.scene(scene)->rootCollectionId));

    const auto encoded=vortex::ProjectCodec::encode(document);
    assert(encoded.ok() && encoded.bytes.size()>32U);
    const auto decoded=vortex::ProjectCodec::decode(encoded.bytes);
    assert(decoded.ok());

    auto legacyChecksumFile = encoded.bytes;
    writeU64(legacyChecksumFile, 20U, legacyPayloadChecksum(legacyChecksumFile));
    assert(vortex::ProjectCodec::decode(legacyChecksumFile).ok());
    assert(decoded.document.id()==document.id());
    assert(decoded.document.meshCount()==1U && decoded.document.objectCount()==2U && decoded.document.sceneCount()==1U);
    assert(decoded.document.hasMesh(meshId) && decoded.document.hasObject(parent) && decoded.document.hasObject(child));
    assert(decoded.document.object(child)->parentId==parent);
    const auto* loaded=decoded.document.authoredMesh(meshId);
    assert(loaded && loaded->hasFace(face) && loaded->hasVertex(c) && loaded->hasVertex(postGap) && loaded->validateStrict());
    const auto* loadedWeights=loaded->attributes().values<float>("weight",vortex::AttributeDomain::Vertex);
    assert(loadedWeights && loadedWeights->size()==5U && (*loadedWeights)[2]==0.9F);

    auto corrupted=encoded.bytes;
    corrupted.back()^=0x5AU;
    assert(vortex::ProjectCodec::decode(corrupted).error==vortex::ProjectIoError::IntegrityMismatch);

    // A valid-checksum file may still be hostile. A rewound allocator would make the
    // next created datablock collide with a live stable ID, so the trust boundary rejects it.
    auto rewoundAllocator = encoded.bytes;
    writeU64(rewoundAllocator, projectHeaderBytes + sizeof(std::uint64_t), 2U);
    refreshChecksum(rewoundAllocator);
    assert(vortex::ProjectCodec::decode(rewoundAllocator).error == vortex::ProjectIoError::InvalidDocument);

    // Collection hierarchy cycles cannot be produced through Document APIs and must not
    // become legal merely because a file reconstructed them directly.
    auto cyclicCollection = encoded.bytes;
    const auto [parentOffset, collectionId] = firstCollectionParentField(cyclicCollection);
    writeU64(cyclicCollection, parentOffset, collectionId);
    refreshChecksum(cyclicCollection);
    assert(vortex::ProjectCodec::decode(cyclicCollection).error == vortex::ProjectIoError::InvalidDocument);

    vortex::AttributeLayer mismatchedLayer;
    mismatchedLayer.key = {"mismatch", vortex::AttributeDomain::Vertex};
    mismatchedLayer.type = vortex::AttributeType::Float;
    mismatchedLayer.defaultValue = true;
    mismatchedLayer.storage = std::vector<float>{0.0F};
    vortex::AttributeSet attributes;
    assert(!attributes.replaceSerializedLayers({1U, 0U, 0U, 0U}, {mismatchedLayer}));

    vortex::EditableMesh allocatorMesh;
    assert(allocatorMesh.addVertex({}));
    auto state = allocatorMesh.serializedState();
    state.nextElementId = std::numeric_limits<std::uint64_t>::max();
    assert(!vortex::EditableMesh::fromSerializedState(std::move(state)).has_value());

    std::cout << "Vortex3D project format v0.1 smoke passed\n";
    return 0;
}
