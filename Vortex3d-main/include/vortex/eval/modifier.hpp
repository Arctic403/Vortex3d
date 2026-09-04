#pragma once

#include "vortex/eval/evaluated_mesh.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace vortex {

enum class MeshModifierType : std::uint8_t {
    Transform = 1,
    Mirror = 2,
    Triangulate = 3,
    SimpleDeformTwist = 4,
};

enum class ModifierApplyError : std::uint8_t {
    None,
    MissingPositionAttribute,
    InvalidTransform,
    InvalidMirror,
    InvalidMirrorWeld,
    GeneratedTopologyOverflow,
    GeneratedTopologyInvalid,
    TriangulationFailed,
    AttributeCopyFailed,
    InvalidSimpleDeform,
};

struct ModifierApplyResult final {
    ModifierApplyError error = ModifierApplyError::None;

    [[nodiscard]] bool ok() const noexcept { return error == ModifierApplyError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

class MeshModifier {
public:
    virtual ~MeshModifier() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual MeshModifierType type() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t revisionToken() const noexcept = 0;
    [[nodiscard]] virtual ModifierApplyResult apply(EvaluatedMesh& mesh) const = 0;

protected:
    [[nodiscard]] static AttributeSet& mutableAttributes(EvaluatedMesh& mesh) noexcept;
    [[nodiscard]] static std::vector<EvaluatedVertex>& mutableVertices(EvaluatedMesh& mesh) noexcept;
    [[nodiscard]] static std::vector<EvaluatedEdge>& mutableEdges(EvaluatedMesh& mesh) noexcept;
    [[nodiscard]] static std::vector<EvaluatedFace>& mutableFaces(EvaluatedMesh& mesh) noexcept;
    [[nodiscard]] static std::vector<EvaluatedCorner>& mutableCorners(EvaluatedMesh& mesh) noexcept;
};

class TransformModifier final : public MeshModifier {
public:
    TransformModifier(
        Vec3 translation = {},
        Vec3 rotationRadians = {},
        Vec3 scale = {1.0F, 1.0F, 1.0F}) noexcept
        : translation_(translation), rotationRadians_(rotationRadians), scale_(scale) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Transform"; }
    [[nodiscard]] MeshModifierType type() const noexcept override { return MeshModifierType::Transform; }
    [[nodiscard]] std::uint64_t revisionToken() const noexcept override;
    [[nodiscard]] ModifierApplyResult apply(EvaluatedMesh& mesh) const override;

    [[nodiscard]] Vec3 translation() const noexcept { return translation_; }
    [[nodiscard]] Vec3 rotationRadians() const noexcept { return rotationRadians_; }
    [[nodiscard]] Vec3 scale() const noexcept { return scale_; }

private:
    Vec3 translation_;
    Vec3 rotationRadians_;
    Vec3 scale_;
};

enum class MirrorAxis : std::uint8_t {
    X,
    Y,
    Z,
};

struct MirrorWeldSettings final {
    bool enabled = false;
    float tolerance = 0.0F;

    [[nodiscard]] bool operator==(const MirrorWeldSettings&) const noexcept = default;
};

class MirrorModifier final : public MeshModifier {
public:
    explicit MirrorModifier(
        const MirrorAxis axis = MirrorAxis::X,
        const float planeOffset = 0.0F,
        const MirrorWeldSettings weld = {}) noexcept
        : axis_(axis), planeOffset_(planeOffset), weld_(weld) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Mirror"; }
    [[nodiscard]] MeshModifierType type() const noexcept override { return MeshModifierType::Mirror; }
    [[nodiscard]] std::uint64_t revisionToken() const noexcept override;
    [[nodiscard]] ModifierApplyResult apply(EvaluatedMesh& mesh) const override;

    [[nodiscard]] MirrorAxis axis() const noexcept { return axis_; }
    [[nodiscard]] float planeOffset() const noexcept { return planeOffset_; }
    [[nodiscard]] MirrorWeldSettings weldSettings() const noexcept { return weld_; }

private:
    MirrorAxis axis_;
    float planeOffset_ = 0.0F;
    MirrorWeldSettings weld_;
};

class SimpleDeformTwistModifier final : public MeshModifier {
public:
    explicit SimpleDeformTwistModifier(float radiansPerUnit = 0.0F, float originZ = 0.0F) noexcept
        : radiansPerUnit_(radiansPerUnit), originZ_(originZ) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Simple Deform Twist"; }
    [[nodiscard]] MeshModifierType type() const noexcept override { return MeshModifierType::SimpleDeformTwist; }
    [[nodiscard]] std::uint64_t revisionToken() const noexcept override;
    [[nodiscard]] ModifierApplyResult apply(EvaluatedMesh& mesh) const override;

    [[nodiscard]] float radiansPerUnit() const noexcept { return radiansPerUnit_; }
    [[nodiscard]] float originZ() const noexcept { return originZ_; }

private:
    float radiansPerUnit_ = 0.0F;
    float originZ_ = 0.0F;
};

class TriangulateModifier final : public MeshModifier {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Triangulate"; }
    [[nodiscard]] MeshModifierType type() const noexcept override { return MeshModifierType::Triangulate; }
    [[nodiscard]] std::uint64_t revisionToken() const noexcept override;
    [[nodiscard]] ModifierApplyResult apply(EvaluatedMesh& mesh) const override;
};

} // namespace vortex
