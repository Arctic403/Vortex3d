#pragma once

#include "vortex/core/document.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vortex {

enum class ProjectIoError : std::uint8_t {
    None,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    IntegrityMismatch,
    InvalidCount,
    InvalidData,
    InvalidDocument,
};

struct ProjectWriteResult final {
    std::vector<std::uint8_t> bytes;
    ProjectIoError error = ProjectIoError::None;
    [[nodiscard]] bool ok() const noexcept { return error == ProjectIoError::None; }
};

struct ProjectReadResult final {
    Document document;
    ProjectIoError error = ProjectIoError::None;
    [[nodiscard]] bool ok() const noexcept { return error == ProjectIoError::None; }
};

class ProjectCodec final {
public:
    static constexpr std::uint32_t minimumSupportedSchemaVersion = 1;
    static constexpr std::uint32_t schemaVersion = 3;
    [[nodiscard]] static ProjectWriteResult encode(const Document& document);
    [[nodiscard]] static ProjectReadResult decode(std::span<const std::uint8_t> bytes);
};

} // namespace vortex
