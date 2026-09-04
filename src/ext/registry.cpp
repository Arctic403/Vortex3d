#include "vortex/ext/registry.hpp"

#include <algorithm>

namespace vortex {

bool OperatorRegistry::registerFactory(std::string id, Factory factory) {
    if (id.empty() || !factory || factories_.contains(id)) {
        return false;
    }
    factories_.emplace(std::move(id), std::move(factory));
    return true;
}

std::unique_ptr<Operator> OperatorRegistry::create(const std::string_view id) const {
    const auto it = factories_.find(std::string{id});
    return it == factories_.end() ? nullptr : it->second();
}

std::vector<std::string> OperatorRegistry::ids() const {
    std::vector<std::string> result;
    result.reserve(factories_.size());
    for (const auto& [id, factory] : factories_) {
        (void)factory;
        result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool ModifierRegistry::registerFactory(std::string id, Factory factory) {
    if (id.empty() || !factory || factories_.contains(id)) {
        return false;
    }
    factories_.emplace(std::move(id), std::move(factory));
    return true;
}

std::unique_ptr<MeshModifier> ModifierRegistry::create(const std::string_view id) const {
    const auto it = factories_.find(std::string{id});
    return it == factories_.end() ? nullptr : it->second();
}

std::vector<std::string> ModifierRegistry::ids() const {
    std::vector<std::string> result;
    result.reserve(factories_.size());
    for (const auto& [id, factory] : factories_) {
        (void)factory;
        result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool GeometryNodeRegistry::registerFactory(std::string id, Factory factory) {
    if (id.empty() || !factory || factories_.contains(id)) return false;
    factories_.emplace(std::move(id), std::move(factory));
    return true;
}

std::optional<GeometryNodePayload> GeometryNodeRegistry::create(const std::string_view id) const {
    const auto it = factories_.find(std::string{id});
    return it == factories_.end() ? std::nullopt : std::optional<GeometryNodePayload>{it->second()};
}

std::vector<std::string> GeometryNodeRegistry::ids() const {
    std::vector<std::string> result;
    result.reserve(factories_.size());
    for (const auto& [id, factory] : factories_) { (void)factory; result.push_back(id); }
    std::sort(result.begin(), result.end());
    return result;
}

bool ProjectAdapterRegistry::registerAdapter(std::string extension, Importer importerValue, Exporter exporterValue) {
    if (extension.empty() || !importerValue || !exporterValue || adapters_.contains(extension)) return false;
    adapters_.emplace(std::move(extension), Adapter{std::move(importerValue), std::move(exporterValue)});
    return true;
}

const ProjectAdapterRegistry::Importer* ProjectAdapterRegistry::importer(const std::string_view extension) const noexcept {
    const auto it = adapters_.find(std::string{extension});
    return it == adapters_.end() ? nullptr : &it->second.importer;
}

const ProjectAdapterRegistry::Exporter* ProjectAdapterRegistry::exporter(const std::string_view extension) const noexcept {
    const auto it = adapters_.find(std::string{extension});
    return it == adapters_.end() ? nullptr : &it->second.exporter;
}

void registerBuiltinExtensions(
    OperatorRegistry& operators,
    ModifierRegistry& modifiers,
    GeometryNodeRegistry* nodes,
    ProjectAdapterRegistry* adapters) {
    (void)operators.registerFactory("mesh.extrude_faces", [] {
        return std::make_unique<ExtrudeFaceOperator>(Vec3{0.0F, 0.0F, 1.0F});
    });
    (void)operators.registerFactory("mesh.translate_selection", [] {
        return std::make_unique<TranslateSelectionOperator>(Vec3{1.0F, 0.0F, 0.0F});
    });
    (void)modifiers.registerFactory("transform", [] { return std::make_unique<TransformModifier>(); });
    (void)modifiers.registerFactory("mirror", [] { return std::make_unique<MirrorModifier>(); });
    (void)modifiers.registerFactory("triangulate", [] { return std::make_unique<TriangulateModifier>(); });
    (void)modifiers.registerFactory("simple_deform_twist", [] { return std::make_unique<SimpleDeformTwistModifier>(); });
    if (nodes != nullptr) {
        (void)nodes->registerFactory("transform", [] { return GeometryNodePayload{GeometryTransformNode{}}; });
        (void)nodes->registerFactory("mirror", [] { return GeometryNodePayload{GeometryMirrorNode{}}; });
        (void)nodes->registerFactory("triangulate", [] { return GeometryNodePayload{GeometryTriangulateNode{}}; });
        (void)nodes->registerFactory("simple_deform_twist", [] { return GeometryNodePayload{GeometryTwistNode{}}; });
    }
    if (adapters != nullptr) {
        (void)adapters->registerAdapter(
            ".vortex",
            [](std::span<const std::uint8_t> bytes) { return ProjectCodec::decode(bytes); },
            [](const Document& document) { return ProjectCodec::encode(document); });
    }
}

} // namespace vortex
