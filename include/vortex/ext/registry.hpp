#pragma once

#include "vortex/editor/operator.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/procedural/geometry_graph.hpp"
#include "vortex/project/project_io.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vortex {

class OperatorRegistry final {
public:
    using Factory = std::function<std::unique_ptr<Operator>()>;

    [[nodiscard]] bool registerFactory(std::string id, Factory factory);
    [[nodiscard]] std::unique_ptr<Operator> create(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> ids() const;

private:
    std::unordered_map<std::string, Factory> factories_;
};

class ModifierRegistry final {
public:
    using Factory = std::function<std::unique_ptr<MeshModifier>()>;

    [[nodiscard]] bool registerFactory(std::string id, Factory factory);
    [[nodiscard]] std::unique_ptr<MeshModifier> create(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> ids() const;

private:
    std::unordered_map<std::string, Factory> factories_;
};

class GeometryNodeRegistry final {
public:
    using Factory = std::function<GeometryNodePayload()>;
    [[nodiscard]] bool registerFactory(std::string id, Factory factory);
    [[nodiscard]] std::optional<GeometryNodePayload> create(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> ids() const;
private:
    std::unordered_map<std::string, Factory> factories_;
};

class ProjectAdapterRegistry final {
public:
    using Importer = std::function<ProjectReadResult(std::span<const std::uint8_t>)>;
    using Exporter = std::function<ProjectWriteResult(const Document&)>;
    [[nodiscard]] bool registerAdapter(std::string extension, Importer importer, Exporter exporter);
    [[nodiscard]] const Importer* importer(std::string_view extension) const noexcept;
    [[nodiscard]] const Exporter* exporter(std::string_view extension) const noexcept;
private:
    struct Adapter final { Importer importer; Exporter exporter; };
    std::unordered_map<std::string, Adapter> adapters_;
};

void registerBuiltinExtensions(
    OperatorRegistry& operators,
    ModifierRegistry& modifiers,
    GeometryNodeRegistry* nodes = nullptr,
    ProjectAdapterRegistry* adapters = nullptr);

} // namespace vortex
