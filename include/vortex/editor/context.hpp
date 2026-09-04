#pragma once

#include "vortex/core/document.hpp"
#include "vortex/core/editor_history.hpp"

#include <unordered_set>

namespace vortex {

enum class EditorMode : std::uint8_t {
    Object,
    Edit,
};

enum class SelectionDomain : std::uint8_t {
    Vertex,
    Edge,
    Face,
};

struct MeshSelection final {
    std::unordered_set<VertexId, IdHash<VertexId>> vertices;
    std::unordered_set<EdgeId, IdHash<EdgeId>> edges;
    std::unordered_set<FaceId, IdHash<FaceId>> faces;

    void clear() noexcept {
        vertices.clear();
        edges.clear();
        faces.clear();
    }
};

class EditorContext final {
public:
    EditorContext(Document& document, EditorHistory& history) noexcept
        : document_(&document), history_(&history) {}

    [[nodiscard]] Document& document() const noexcept { return *document_; }
    [[nodiscard]] EditorHistory& history() const noexcept { return *history_; }

    [[nodiscard]] EditorMode mode() const noexcept { return mode_; }
    void setMode(EditorMode mode) noexcept { mode_ = mode; }

    [[nodiscard]] SelectionDomain selectionDomain() const noexcept { return selectionDomain_; }
    void setSelectionDomain(SelectionDomain domain) noexcept { selectionDomain_ = domain; }

    [[nodiscard]] ObjectId activeObject() const noexcept { return activeObject_; }
    [[nodiscard]] MeshId activeMesh() const noexcept;
    [[nodiscard]] bool setActiveObject(ObjectId objectId) noexcept;

    [[nodiscard]] MeshSelection& selection() noexcept { return selection_; }
    [[nodiscard]] const MeshSelection& selection() const noexcept { return selection_; }

private:
    Document* document_ = nullptr;
    EditorHistory* history_ = nullptr;
    EditorMode mode_ = EditorMode::Object;
    SelectionDomain selectionDomain_ = SelectionDomain::Vertex;
    ObjectId activeObject_;
    MeshSelection selection_;
};

} // namespace vortex
