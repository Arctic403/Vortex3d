#include "vortex/project/project_io.hpp"

#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace vortex {
namespace {

constexpr std::array<std::uint8_t, 8> magic{'V','T','X','3','D','0','0','1'};
constexpr std::uint64_t maxElements = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t fnv64Offset = 14695981039346656037ULL;
constexpr std::uint64_t legacyFnv64Offset = 1469598103934665603ULL;
constexpr std::uint64_t fnv64Prime = 1099511628211ULL;

static_assert(std::endian::native == std::endian::little, "ProjectCodec schema v3 requires little-endian encoding");
static_assert(sizeof(bool) == 1U, "ProjectCodec schema v3 requires one-byte bool");
static_assert(sizeof(float) == 4U && std::numeric_limits<float>::is_iec559,
              "ProjectCodec schema v3 requires IEEE-754 32-bit float");
static_assert(sizeof(Vec2) == 8U && sizeof(Vec3) == 12U && sizeof(Vec4) == 16U,
              "ProjectCodec schema v3 requires tightly packed vector value types");
static_assert(sizeof(Quaternion) == 16U,
              "ProjectCodec schema v3 requires a tightly packed quaternion value type");

class Writer final {
public:
    template <typename T> void pod(const T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(T));
    }
    void string(const std::string& value) {
        pod<std::uint64_t>(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    std::vector<std::uint8_t> bytes;
};

class Reader final {
public:
    explicit Reader(std::span<const std::uint8_t> input) : input_(input) {}
    template <typename T> bool pod(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (remaining() < sizeof(T)) return false;
        std::memcpy(&value, input_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return true;
    }
    bool string(std::string& value) {
        std::uint64_t size = 0;
        if (!pod(size) || size > remaining() || size > (16ULL * 1024ULL * 1024ULL)) return false;
        value.assign(reinterpret_cast<const char*>(input_.data() + offset_), static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return true;
    }
    [[nodiscard]] std::size_t remaining() const noexcept { return input_.size() - offset_; }
    [[nodiscard]] bool canReadElements(const std::uint64_t count, const std::size_t elementBytes) const noexcept {
        if (elementBytes == 0U) {
            return true;
        }
        if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / elementBytes)) {
            return false;
        }
        return static_cast<std::size_t>(count) * elementBytes <= remaining();
    }
private:
    std::span<const std::uint8_t> input_;
    std::size_t offset_ = 0;
};

std::uint64_t checksumWithOffset(
    const std::span<const std::uint8_t> bytes,
    const std::uint64_t offset) noexcept {
    std::uint64_t hash = offset;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv64Prime;
    }
    return hash;
}

std::uint64_t checksum(const std::span<const std::uint8_t> bytes) noexcept {
    return checksumWithOffset(bytes, fnv64Offset);
}

std::uint64_t legacyChecksum(const std::span<const std::uint8_t> bytes) noexcept {
    return checksumWithOffset(bytes, legacyFnv64Offset);
}

template <typename IdType> void writeId(Writer& writer, const IdType id) { writer.pod<std::uint64_t>(id.value()); }
template <typename IdType> bool readId(Reader& reader, IdType& id) { std::uint64_t value=0; if(!reader.pod(value)) return false; id=IdType{value}; return true; }

template <typename T> void writeVectorPod(Writer& writer, const std::vector<T>& values) {
    writer.pod<std::uint64_t>(values.size());
    for (const T& value : values) writer.pod<T>(value);
}
template <typename T> bool readVectorPod(Reader& reader, std::vector<T>& values) {
    std::uint64_t count=0; if(!reader.pod(count) || count>maxElements || !reader.canReadElements(count, sizeof(T))) return false;
    values.resize(static_cast<std::size_t>(count));
    for (T& value : values) if(!reader.pod(value)) return false;
    return true;
}

void writeScalar(Writer& w, const AttributeScalar& scalar) {
    w.pod<std::uint8_t>(static_cast<std::uint8_t>(scalar.index()));
    std::visit([&](const auto& value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, bool>) {
            w.pod<std::uint8_t>(value ? 1U : 0U);
        } else {
            w.pod(value);
        }
    }, scalar);
}

bool readScalar(Reader& r, AttributeScalar& scalar) {
    std::uint8_t index=0; if(!r.pod(index) || index>6U) return false;
    switch(index){
        case 0:{std::uint8_t v=0; if(!r.pod(v) || v>1U) return false; scalar=(v!=0U); break;}
        case 1:{std::int32_t v=0; if(!r.pod(v)) return false; scalar=v; break;}
        case 2:{std::uint32_t v=0; if(!r.pod(v)) return false; scalar=v; break;}
        case 3:{float v=0; if(!r.pod(v)) return false; scalar=v; break;}
        case 4:{Vec2 v{}; if(!r.pod(v)) return false; scalar=v; break;}
        case 5:{Vec3 v{}; if(!r.pod(v)) return false; scalar=v; break;}
        case 6:{Vec4 v{}; if(!r.pod(v)) return false; scalar=v; break;}
        default:return false;
    }
    return true;
}

void writeStorage(Writer& w, const AttributeStorage& storage) {
    w.pod<std::uint8_t>(static_cast<std::uint8_t>(storage.index()));
    std::visit([&](const auto& values){
        w.pod<std::uint64_t>(values.size());
        using Vector=std::decay_t<decltype(values)>; using Value=typename Vector::value_type;
        if constexpr(std::is_same_v<Value,bool>){ for(bool v: values) w.pod<std::uint8_t>(v?1U:0U); }
        else { for(const auto& v: values) w.pod(v); }
    }, storage);
}

bool readStorage(Reader& r, AttributeStorage& storage) {
    std::uint8_t index=0; std::uint64_t count=0; if(!r.pod(index)||index>6U||!r.pod(count)||count>maxElements) return false;
    const std::size_t elementBytes = [&]() -> std::size_t {
        switch (index) {
            case 0: return sizeof(std::uint8_t);
            case 1: return sizeof(std::int32_t);
            case 2: return sizeof(std::uint32_t);
            case 3: return sizeof(float);
            case 4: return sizeof(Vec2);
            case 5: return sizeof(Vec3);
            case 6: return sizeof(Vec4);
            default: return 0U;
        }
    }();
    if (elementBytes == 0U || !r.canReadElements(count, elementBytes)) return false;
    const auto n=static_cast<std::size_t>(count);
    switch(index){
        case 0:{std::vector<bool> v(n); for(std::size_t i=0;i<n;++i){std::uint8_t x=0;if(!r.pod(x)||x>1U)return false;v[i]=x!=0;} storage=std::move(v);break;}
        case 1:{std::vector<std::int32_t> v(n);for(auto& x:v)if(!r.pod(x))return false;storage=std::move(v);break;}
        case 2:{std::vector<std::uint32_t> v(n);for(auto& x:v)if(!r.pod(x))return false;storage=std::move(v);break;}
        case 3:{std::vector<float> v(n);for(auto& x:v)if(!r.pod(x))return false;storage=std::move(v);break;}
        case 4:{std::vector<Vec2> v(n);for(auto& x:v)if(!r.pod(x))return false;storage=std::move(v);break;}
        case 5:{std::vector<Vec3> v(n);for(auto& x:v)if(!r.pod(x))return false;storage=std::move(v);break;}
        case 6:{std::vector<Vec4> v(n);for(auto& x:v)if(!r.pod(x))return false;storage=std::move(v);break;}
        default:return false;
    }
    return true;
}

void writeAttributes(Writer& w, const AttributeSet& attributes) {
    for (std::uint8_t domain = 0; domain < 4U; ++domain) {
        w.pod<std::uint64_t>(attributes.domainSize(static_cast<AttributeDomain>(domain)));
    }
    const auto layerKeys = attributes.sortedLayerKeys();
    w.pod<std::uint64_t>(layerKeys.size());
    for (const AttributeKey& key : layerKeys) {
        const AttributeLayer& layer = *attributes.layer(key.name, key.domain);
        w.string(layer.key.name);
        w.pod<std::uint8_t>(static_cast<std::uint8_t>(layer.key.domain));
        w.pod<std::uint8_t>(static_cast<std::uint8_t>(layer.type));
        writeScalar(w, layer.defaultValue);
        writeStorage(w, layer.storage);
    }
}

bool readAttributes(Reader& r, AttributeSet& attributes) {
    std::array<std::size_t, 4> domainSizes{};
    for (std::size_t& size : domainSizes) {
        std::uint64_t value = 0;
        if (!r.pod(value) || value > maxElements) {
            return false;
        }
        size = static_cast<std::size_t>(value);
    }

    std::uint64_t count = 0;
    if (!r.pod(count) || count > 1024U) {
        return false;
    }
    std::vector<AttributeLayer> layers;
    layers.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        AttributeLayer layer;
        std::uint8_t domain = 0;
        std::uint8_t type = 0;
        if (!r.string(layer.key.name) || !r.pod(domain) || domain > 3U || !r.pod(type) || type > 6U) {
            return false;
        }
        layer.key.domain = static_cast<AttributeDomain>(domain);
        layer.type = static_cast<AttributeType>(type);
        if (!readScalar(r, layer.defaultValue) || !readStorage(r, layer.storage)) {
            return false;
        }
        layers.push_back(std::move(layer));
    }
    return attributes.replaceSerializedLayers(domainSizes, std::move(layers));
}

void writeMesh(Writer& w, const EditableMesh& mesh) {
    w.pod<std::uint64_t>(mesh.serializedNextElementId());
    writeAttributes(w, mesh.attributes());

    w.pod<std::uint64_t>(mesh.vertexCount());
    for (const VertexId id : mesh.vertexIds()) {
        writeId(w, id);
    }
    w.pod<std::uint64_t>(mesh.edgeCount());
    for (const EdgeId id : mesh.edgeIds()) {
        const MeshEdge& value = *mesh.edge(id);
        writeId(w, value.id);
        writeId(w, value.vertexA);
        writeId(w, value.vertexB);
        writeId(w, value.anyCorner);
    }
    w.pod<std::uint64_t>(mesh.faceCount());
    for (const FaceId id : mesh.faceIds()) {
        const MeshFace& value = *mesh.face(id);
        writeId(w, value.id);
        writeId(w, value.firstCorner);
        w.pod(value.cornerCount);
    }
    w.pod<std::uint64_t>(mesh.cornerCount());
    for (const CornerId id : mesh.cornerIds()) {
        const MeshCorner& value = *mesh.corner(id);
        writeId(w, value.id);
        writeId(w, value.faceId);
        writeId(w, value.vertexId);
        writeId(w, value.edgeId);
        writeId(w, value.next);
        writeId(w, value.prev);
        writeId(w, value.radialNext);
        writeId(w, value.radialPrev);
    }
}

bool readCount(Reader& r, std::uint64_t& count) {
    return r.pod(count) && count <= maxElements;
}

bool readMesh(Reader& r, EditableMesh& mesh) {
    EditableMeshSerializedState state;
    if (!r.pod(state.nextElementId) || !readAttributes(r, state.attributes)) {
        return false;
    }
    std::uint64_t count = 0;
    if (!readCount(r, count) || !r.canReadElements(count, sizeof(std::uint64_t))) return false;
    state.vertices.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        MeshVertex value{};
        if (!readId(r, value.id)) return false;
        state.vertices.push_back(value);
    }
    if (!readCount(r, count) || !r.canReadElements(count, sizeof(std::uint64_t) * 4U)) return false;
    state.edges.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        MeshEdge value{};
        if (!readId(r, value.id) || !readId(r, value.vertexA) || !readId(r, value.vertexB) || !readId(r, value.anyCorner)) return false;
        state.edges.push_back(value);
    }
    if (!readCount(r, count) || !r.canReadElements(count, sizeof(std::uint64_t) * 2U + sizeof(std::uint32_t))) return false;
    state.faces.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        MeshFace value{};
        if (!readId(r, value.id) || !readId(r, value.firstCorner) || !r.pod(value.cornerCount)) return false;
        state.faces.push_back(value);
    }
    if (!readCount(r, count) || !r.canReadElements(count, sizeof(std::uint64_t) * 8U)) return false;
    state.corners.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        MeshCorner value{};
        if (!readId(r, value.id) || !readId(r, value.faceId) || !readId(r, value.vertexId) || !readId(r, value.edgeId) ||
            !readId(r, value.next) || !readId(r, value.prev) || !readId(r, value.radialNext) || !readId(r, value.radialPrev)) {
            return false;
        }
        state.corners.push_back(value);
    }
    auto decoded = EditableMesh::fromSerializedState(std::move(state));
    if (!decoded) return false;
    mesh = std::move(*decoded);
    return true;
}

template <typename IdType, typename Map>
std::vector<IdType> sortedIds(const Map& map) {
    std::vector<IdType> ids;
    ids.reserve(map.size());
    for (const auto& [id, value] : map) {
        (void)value;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [](const auto a, const auto b) { return a.value() < b.value(); });
    return ids;
}

} // namespace

ProjectWriteResult ProjectCodec::encode(const Document& document) {
    if (!document.validate()) {
        return {{}, ProjectIoError::InvalidDocument};
    }

    Writer out;
    out.bytes.insert(out.bytes.end(), magic.begin(), magic.end());
    out.pod<std::uint32_t>(schemaVersion);
    const std::size_t payloadSizeOffset = out.bytes.size();
    out.pod<std::uint64_t>(0U);
    const std::size_t checksumOffset = out.bytes.size();
    out.pod<std::uint64_t>(0U);
    const std::size_t payloadOffset = out.bytes.size();

    writeId(out, document.id_);
    out.pod(document.nextId_);
    out.pod(document.revision_);

    const auto sceneIds = sortedIds<SceneId>(document.scenes_);
    out.pod<std::uint64_t>(sceneIds.size());
    for (const SceneId id : sceneIds) {
        const auto& scene = document.scenes_.at(id);
        writeId(out, scene.id);
        out.string(scene.name);
        writeId(out, scene.rootCollectionId);
        out.pod(scene.revision);
    }

    const auto collectionIds = sortedIds<CollectionId>(document.collections_);
    out.pod<std::uint64_t>(collectionIds.size());
    for (const CollectionId id : collectionIds) {
        const auto& collection = document.collections_.at(id);
        writeId(out, collection.id);
        out.string(collection.name);
        writeId(out, collection.sceneId);
        writeId(out, collection.parentId);
        out.pod(collection.revision);
        std::vector<ObjectId> objectIds(collection.objectIds.begin(), collection.objectIds.end());
        std::sort(objectIds.begin(), objectIds.end(), [](const auto a, const auto b) { return a.value() < b.value(); });
        out.pod<std::uint64_t>(objectIds.size());
        for (const ObjectId objectId : objectIds) {
            writeId(out, objectId);
        }
    }

    const auto meshIds = sortedIds<MeshId>(document.meshes_);
    out.pod<std::uint64_t>(meshIds.size());
    for (const MeshId id : meshIds) {
        const auto& mesh = document.meshes_.at(id);
        writeId(out, mesh.id);
        out.string(mesh.name);
        out.pod(mesh.revision);
        out.pod(mesh.evaluationRevision_);
        writeMesh(out, *mesh.authoredMesh_);
    }

    const auto objectIds = sortedIds<ObjectId>(document.objects_);
    out.pod<std::uint64_t>(objectIds.size());
    for (const ObjectId id : objectIds) {
        const auto& object = document.objects_.at(id);
        writeId(out, object.id);
        out.string(object.name);
        writeId(out, object.meshId);
        writeId(out, object.parentId);
        out.pod(object.transform.translation);
        out.pod(object.transform.rotation);
        out.pod(object.transform.scale);
        out.pod(object.revision);
    }

    const std::uint64_t payloadSize = static_cast<std::uint64_t>(out.bytes.size() - payloadOffset);
    const std::span<const std::uint8_t> payloadSpan{out.bytes.data() + payloadOffset, out.bytes.size() - payloadOffset};
    const std::uint64_t payloadChecksum = checksum(payloadSpan);
    std::memcpy(out.bytes.data() + payloadSizeOffset, &payloadSize, sizeof(payloadSize));
    std::memcpy(out.bytes.data() + checksumOffset, &payloadChecksum, sizeof(payloadChecksum));
    return {std::move(out.bytes), ProjectIoError::None};
}

ProjectReadResult ProjectCodec::decode(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 28U) {
        return {Document{}, ProjectIoError::Truncated};
    }
    if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
        return {Document{}, ProjectIoError::InvalidMagic};
    }

    Reader header(bytes.subspan(8));
    std::uint32_t version = 0;
    std::uint64_t payloadSize = 0;
    std::uint64_t expectedChecksum = 0;
    if (!header.pod(version) || !header.pod(payloadSize) || !header.pod(expectedChecksum)) {
        return {Document{}, ProjectIoError::Truncated};
    }
    if (version < minimumSupportedSchemaVersion || version > schemaVersion) {
        return {Document{}, ProjectIoError::UnsupportedVersion};
    }
    if (payloadSize != bytes.size() - 28U) {
        return {Document{}, ProjectIoError::Truncated};
    }

    const auto payloadSpan = bytes.subspan(28);
    if (checksum(payloadSpan) != expectedChecksum && legacyChecksum(payloadSpan) != expectedChecksum) {
        return {Document{}, ProjectIoError::IntegrityMismatch};
    }

    Reader reader(payloadSpan);
    Document document;
    if (!readId(reader, document.id_) || !reader.pod(document.nextId_) || !reader.pod(document.revision_)) {
        return {Document{}, ProjectIoError::Truncated};
    }
    document.scenes_.clear();
    document.collections_.clear();
    document.meshes_.clear();
    document.objects_.clear();
    document.changes_.clear();
    document.discardedChangesThroughRevision_ = 0;
    document.pendingClearChangesThroughRevision_ = 0;
    document.changeHistoryBatchDepth_ = 0;

    std::uint64_t count = 0;
    if (!readCount(reader, count)) {
        return {Document{}, ProjectIoError::InvalidCount};
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        SceneBlock scene{};
        if (!readId(reader, scene.id) || !reader.string(scene.name) || !readId(reader, scene.rootCollectionId) ||
            !reader.pod(scene.revision)) {
            return {Document{}, ProjectIoError::Truncated};
        }
        if (!document.scenes_.emplace(scene.id, std::move(scene)).second) {
            return {Document{}, ProjectIoError::InvalidData};
        }
    }

    if (!readCount(reader, count)) {
        return {Document{}, ProjectIoError::InvalidCount};
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        CollectionBlock collection{};
        if (!readId(reader, collection.id) || !reader.string(collection.name) || !readId(reader, collection.sceneId) ||
            !readId(reader, collection.parentId) || !reader.pod(collection.revision)) {
            return {Document{}, ProjectIoError::Truncated};
        }
        std::uint64_t objectCount = 0;
        if (!readCount(reader, objectCount)) {
            return {Document{}, ProjectIoError::InvalidCount};
        }
        for (std::uint64_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
            ObjectId objectId;
            if (!readId(reader, objectId)) {
                return {Document{}, ProjectIoError::Truncated};
            }
            if (!collection.objectIds.insert(objectId).second) {
                return {Document{}, ProjectIoError::InvalidData};
            }
        }
        if (!document.collections_.emplace(collection.id, std::move(collection)).second) {
            return {Document{}, ProjectIoError::InvalidData};
        }
    }

    if (!readCount(reader, count)) {
        return {Document{}, ProjectIoError::InvalidCount};
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        MeshId id;
        std::string name;
        std::uint64_t revision = 0;
        std::uint64_t evaluationRevision = 0;
        if (!readId(reader, id) || !reader.string(name) || !reader.pod(revision) || !reader.pod(evaluationRevision)) {
            return {Document{}, ProjectIoError::Truncated};
        }
        auto authored = std::make_unique<EditableMesh>();
        if (!readMesh(reader, *authored)) {
            return {Document{}, ProjectIoError::InvalidData};
        }
        MeshBlock block{document.runtimeId_, id, std::move(name), std::move(authored), revision};
        block.evaluationRevision_ = evaluationRevision;
        if (!document.meshes_.emplace(id, std::move(block)).second) {
            return {Document{}, ProjectIoError::InvalidData};
        }
    }

    if (!readCount(reader, count)) {
        return {Document{}, ProjectIoError::InvalidCount};
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        ObjectBlock object{};
        if (!readId(reader, object.id) || !reader.string(object.name) || !readId(reader, object.meshId) ||
            !readId(reader, object.parentId)) {
            return {Document{}, ProjectIoError::Truncated};
        }
        if (version >= 3U) {
            if (!reader.pod(object.transform.translation) ||
                !reader.pod(object.transform.rotation) ||
                !reader.pod(object.transform.scale) ||
                !isFiniteObjectTransform(object.transform)) {
                return {Document{}, ProjectIoError::InvalidData};
            }
        } else if (version == 2U) {
            Vec3 legacyEuler{};
            if (!reader.pod(object.transform.translation) ||
                !reader.pod(legacyEuler) ||
                !reader.pod(object.transform.scale)) {
                return {Document{}, ProjectIoError::InvalidData};
            }
            const auto migratedRotation = quaternionFromEulerRadians(legacyEuler);
            if (!migratedRotation) {
                return {Document{}, ProjectIoError::InvalidData};
            }
            object.transform.rotation = *migratedRotation;
            if (!isFiniteObjectTransform(object.transform)) {
                return {Document{}, ProjectIoError::InvalidData};
            }
        }
        if (!reader.pod(object.revision)) {
            return {Document{}, ProjectIoError::Truncated};
        }
        if (!document.objects_.emplace(object.id, std::move(object)).second) {
            return {Document{}, ProjectIoError::InvalidData};
        }
    }

    if (reader.remaining() != 0U || !document.validate()) {
        return {Document{}, ProjectIoError::InvalidDocument};
    }

    ProjectReadResult result;
    result.document = std::move(document);
    result.error = ProjectIoError::None;
    return result;
}

} // namespace vortex