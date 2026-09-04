#pragma once

#include "vortex/core/document.hpp"
#include "vortex/eval/evaluated_mesh.hpp"

#include <cstdint>
#include <optional>

namespace vortex {

enum class MeshEvaluationError : std::uint8_t {
    None,
    MissingAuthoredMesh,
    InvalidSourceMesh,
    ElementCountOverflow,
    MissingTopologyReference,
};

struct MeshEvaluationResult final {
    std::optional<EvaluatedMesh> mesh;
    MeshEvaluationError error = MeshEvaluationError::None;

    [[nodiscard]] bool ok() const noexcept { return mesh.has_value() && error == MeshEvaluationError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

class MeshEvaluator final {
public:
    [[nodiscard]] static MeshEvaluationResult evaluate(const MeshBlock& source);
};

} // namespace vortex
