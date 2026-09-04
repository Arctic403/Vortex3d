#pragma once

#include "vortex/eval/evaluated_mesh.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vortex {

enum class EvaluatedMeshValidationCode : std::uint8_t {
    InvalidSourceIdentity,
    AttributeSizeMismatch,
    ElementCountOverflow,
    InvalidEdgeEndpoints,
    DuplicateEdge,
    InvalidFaceSize,
    BrokenFaceCycle,
    BrokenRadialCycle,
    InvalidCornerReference,
    CornerEdgeMismatch,
    UnreachableCorner,
    CornerUsedByMultipleFaces,
};

struct EvaluatedMeshValidationIssue final {
    EvaluatedMeshValidationCode code = EvaluatedMeshValidationCode::InvalidSourceIdentity;
    std::uint64_t elementIndex = 0;
    std::string message;
};

struct EvaluatedMeshValidationResult final {
    std::vector<EvaluatedMeshValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

class EvaluatedMeshValidator final {
public:
    [[nodiscard]] static EvaluatedMeshValidationResult validate(const EvaluatedMesh& mesh);
};

} // namespace vortex
