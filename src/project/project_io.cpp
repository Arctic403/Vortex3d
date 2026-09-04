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
private:
    std::span<const std::uint8_t> input_;
    std::size_t offset_ = 0;
};

std::uint64_t checksum(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}

template <typename IdType> void writeId(Writer& writer, const IdType id) { writer.pod<std::uint64_t>(id.value()); }
template <typename IdType> bool readId(Reader& reader, IdType& id) { std::uint64_t value=0; if(!reader.pod(value)) return false; id=IdType{value}; return true; }

template <typename T> void writeVectorPod(Writer& writer, const std::vector<T>& values) {
    writer.pod<std::uint64_t>(values.size());
    for (const T& value : values) writer.pod<T>(value);
}
template <typename T> bool readVectorPod(Reader& reader, std::vector<T>& values) {
    std::uint64_t count=0; if(!reader.pod(count) || count>maxElements) return false;
    values.resize(static_cast<std::size_t>(count));
    for (T& value : values) if(!reader.pod(value)) return false;
    return true;
}

void writeScalar(Writer& w, const AttributeScalar& scalar) {
    w.pod<std::uint8_t>(static_cast<std::uint8_t>(scalar.index()));
    std::visit([&](const auto& value){ w.pod(value); }, scalar);
}

bool readScalar(Reader& r, AttributeScalar& scalar) {
    std::uint8_t index=0; if(!r.pod(index) || index>6U) return false;
    switch(index){
        case 0:{bool v=false; if(!r.pod(v)) return false; scalar=v; break;}
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
    const auto n=static_cast<std::size_t>(count);
    switch(index){
        case 0:{std::vector<bool> v(n); for(std::size_t i=0;i<n;++i){std::uint8_t x=0;if(!r.pod(x))return false;v[i]=x!=0;} storage=std::move(v);break;}
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
    const auto layers = attributes.layersSnapshot();
    w.pod<std::uint64_t>(layers.size());
    for (const AttributeLayer& layer : layers) {
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
    const EditableMeshSerializedState state = mesh.serializedState();
    w.pod<std::uint64_t>(state.nextElementId);
    writeAttributes(w, state.attributes);

    w.pod<std::uint64_t>(state.vertices.size());
    for (const MeshVertex& value : state.vertices) {
        writeId(w, value.id);
    }
    w.pod<std::uint64_t>(state.edges.size());
    for (const MeshEdge& value : state.edges) {
        writeId(w, value.id);
        writeId(w, value.vertexA);
        writeId(w, value.vertexB);
        writeId(w, value.anyCorner);
    }
    w.pod<std::uint64_t>(state.faces.size());
    for (const MeshFace& value : state.faces) {
        writeId(w, value.id);
        writeId(w, value.firstCorner);
        w.pod(value.cornerCount);
    }
    w.pod<std::uint64_t>(state.corners.size());
    for (const MeshCorner& value : state.corners) {
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
    if (!readCount(r, count)) return false;
    state.vertices.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        MeshVertex value{};
        if (!readId(r, value.id)) return false;
        state.vertices.push_back(value);
    }
    if (!readCount(r, count)) return false;
    state.edges.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        MeshEdge value{};
        if (!readId(r, value.id) || !readId(r, value.vertexA) || !readId(r, value.vertexB) || !readId(r, value.anyCorner)) return false;
        state.edges.push_back(value);
    }
    if (!readCount(r, count)) return false;
    state.faces.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        MeshFace value{};
        if (!readId(r, value.id) || !readId(r, value.firstCorner) || !r.pod(value.cornerCount)) return false;
        state.faces.push_back(value);
    }
    if (!readCount(r, count)) return false;
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

    Writer payload;
    writeId(payload, document.id_);
    payload.pod(document.nextId_);
    payload.pod(document.revision_);

    const auto sceneIds = sortedIds<SceneId>(document.scenes_);
    payload.pod<std::uint64_t>(sceneIds.size());
    for (const SceneId id : sceneIds) {
        const auto& scene = document.scenes_.at(id);
        writeId(payload, scene.id);
        payload.string(scene.name);
        writeId(payload, scene.rootCollectionId);
        payload.pod(scene.revision);
    }

    const auto collectionIds = sortedIds<CollectionId>(document.collections_);
    payload.pod<std::uint64_t>(collectionIds.size());
    for (const CollectionId id : collectionIds) {
        const auto& collection = document.collections_.at(id);
        writeId(payload, collection.id);
        payload.string(collection.name);
        writeId(payload, collection.sceneId);
        writeId(payload, collection.parentId);
        payload.pod(collection.revision);
        std::vector<ObjectId> objectIds(collection.objectIds.begin(), collection.objectIds.end());
        std::sort(objectIds.begin(), objectIds.end(), [](const auto a, const auto b) { return a.value() < b.value(); });
        payload.pod<std::uint64_t>(objectIds.size());
        for (const ObjectId objectId : objectIds) {
            writeId(payload, objectId);
        }
    }

    const auto meshIds = sortedIds<MeshId>(document.meshes_);
    payload.pod<std::uint64_t>(meshIds.size());
    for (const MeshId id : meshIds) {
        const auto& mesh = document.meshes_.at(id);
        writeId(payload, mesh.id);
        payload.string(mesh.name);
        payload.pod(mesh.revision);
        payload.pod(mesh.evaluationRevision_);
        writeMesh(payload, *mesh.authoredMesh_);
    }

    const auto objectIds = sortedIds<ObjectId>(document.objects_);
    payload.pod<std::uint64_t>(objectIds.size());
    for (const ObjectId id : objectIds) {
        const auto& object = document.objects_.at(id);
        writeId(payload, object.id);
        payload.string(object.name);
        writeId(payload, object.meshId);
        writeId(payload, object.parentId);
        payload.pod(object.revision);
    }

    Writer out;
    out.bytes.insert(out.bytes.end(), magic.begin(), magic.end());
    out.pod<std::uint32_t>(schemaVersion);
    out.pod<std::uint64_t>(payload.bytes.size());
    out.pod<std::uint64_t>(checksum(payload.bytes));
    out.bytes.insert(out.bytes.end(), payload.bytes.begin(), payload.bytes.end());
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
    if (version != schemaVersion) {
        return {Document{}, ProjectIoError::UnsupportedVersion};
    }
    if (payloadSize != bytes.size() - 28U) {
        return {Document{}, ProjectIoError::Truncated};
    }

    const auto payloadSpan = bytes.subspan(28);
    if (checksum(payloadSpan) != expectedChecksum) {
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
        document.scenes_.emplace(scene.id, std::move(scene));
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
            collection.objectIds.insert(objectId);
        }
        document.collections_.emplace(collection.id, std::move(collection));
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
        document.meshes_.emplace(id, std::move(block));
    }

    if (!readCount(reader, count)) {
        return {Document{}, ProjectIoError::InvalidCount};
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        ObjectBlock object{};
        if (!readId(reader, object.id) || !reader.string(object.name) || !readId(reader, object.meshId) ||
            !readId(reader, object.parentId) || !reader.pod(object.revision)) {
            return {Document{}, ProjectIoError::Truncated};
        }
        document.objects_.emplace(object.id, std::move(object));
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
