#pragma once

#include "vortex/core/document.hpp"
#include "vortex/eval/evaluated_mesh.hpp"
#include "vortex/eval/modifier.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace vortex {

enum class MeshEvaluationError : std::uint8_t {
    None,
    MissingAuthoredMesh,
    InvalidSourceMesh,
    ElementCountOverflow,
    MissingTopologyReference,
    NullModifier,
    ModifierFailed,
};

struct MeshEvaluationKeyResult final {
    std::optional<EvaluationCacheKey> key;
    MeshEvaluationError error = MeshEvaluationError::None;
    std::optional<std::size_t> modifierIndex;

    [[nodiscard]] bool ok() const noexcept { return key.has_value() && error == MeshEvaluationError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

struct MeshEvaluationResult final {
    std::optional<EvaluatedMesh> mesh;
    MeshEvaluationError error = MeshEvaluationError::None;
    ModifierApplyError modifierError = ModifierApplyError::None;
    std::optional<std::size_t> modifierIndex;

    [[nodiscard]] bool ok() const noexcept { return mesh.has_value() && error == MeshEvaluationError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

class MeshEvaluator final {
public:
    [[nodiscard]] static MeshEvaluationKeyResult cacheKeyFor(
        const MeshBlock& source,
        std::span<const MeshModifier* const> modifiers = {}) noexcept;

    [[nodiscard]] static MeshEvaluationResult evaluate(
        const MeshBlock& source,
        std::span<const MeshModifier* const> modifiers = {});
};

} // namespace vortex
