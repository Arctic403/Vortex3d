#pragma once

#include "vortex/eval/evaluated_mesh.hpp"

#include <cstdint>

namespace vortex {

enum class NormalGenerationError : std::uint8_t {
    None,
    MissingPositionAttribute,
    InvalidTopology,
    NonFinitePosition,
    DegenerateFace,
    DegenerateSmoothFan,
    InvalidShadingAttribute,
    AttributeWriteFailed,
};

struct NormalGenerationResult final {
    NormalGenerationError error = NormalGenerationError::None;

    [[nodiscard]] bool ok() const noexcept { return error == NormalGenerationError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

class DerivedNormalsGenerator final {
public:
    [[nodiscard]] static NormalGenerationResult generate(EvaluatedMesh& mesh);
};

} // namespace vortex
