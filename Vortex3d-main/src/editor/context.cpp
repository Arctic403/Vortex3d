#include "vortex/editor/context.hpp"

namespace vortex {

MeshId EditorContext::activeMesh() const noexcept {
    if (!activeObject_) {
        return {};
    }
    const ObjectBlock* object = document_->object(activeObject_);
    return object == nullptr ? MeshId{} : object->meshId;
}

bool EditorContext::setActiveObject(const ObjectId objectId) noexcept {
    if (objectId && !document_->hasObject(objectId)) {
        return false;
    }
    activeObject_ = objectId;
    selection_.clear();
    return true;
}

} // namespace vortex
