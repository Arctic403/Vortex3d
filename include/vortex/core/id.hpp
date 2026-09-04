#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace vortex {

template <typename Tag>
class Id final {
public:
    using value_type = std::uint64_t;

    constexpr Id() noexcept = default;
    explicit constexpr Id(value_type value) noexcept : value_(value) {}

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
    explicit constexpr operator bool() const noexcept { return valid(); }

    constexpr auto operator<=>(const Id&) const noexcept = default;

private:
    value_type value_ = 0;
};

struct DocumentTag;
struct SceneTag;
struct ObjectTag;
struct MeshTag;
struct MaterialTag;
struct ImageTag;
struct CollectionTag;
struct VertexTag;
struct EdgeTag;
struct FaceTag;
struct CornerTag;

using DocumentId = Id<DocumentTag>;
using SceneId = Id<SceneTag>;
using ObjectId = Id<ObjectTag>;
using MeshId = Id<MeshTag>;
using MaterialId = Id<MaterialTag>;
using ImageId = Id<ImageTag>;
using CollectionId = Id<CollectionTag>;
using VertexId = Id<VertexTag>;
using EdgeId = Id<EdgeTag>;
using FaceId = Id<FaceTag>;
using CornerId = Id<CornerTag>;

template <typename IdType>
struct IdHash final {
    [[nodiscard]] std::size_t operator()(const IdType id) const noexcept {
        return std::hash<typename IdType::value_type>{}(id.value());
    }
};

} // namespace vortex
