#include "vortex/core/document.hpp"

#include <cassert>
#include <iostream>

int main() {
    vortex::Document document;
    assert(document.id());
    assert(document.validate());

    const vortex::MeshId sharedMesh = document.createMesh("Shared Cube Mesh");
    assert(sharedMesh);
    assert(document.hasMesh(sharedMesh));

    const vortex::ObjectId parent = document.createObject("Parent", sharedMesh);
    const vortex::ObjectId child = document.createObject("Child", sharedMesh);
    assert(parent && child);
    assert(parent != child);
    assert(document.objectCount() == 2);
    assert(document.meshCount() == 1);

    assert(document.setObjectParent(child, parent));
    assert(!document.setObjectParent(parent, child));
    assert(document.validate());

    // A used data-block cannot be deleted.
    assert(!document.removeMesh(sharedMesh));

    // Invalid references are rejected without damaging the document.
    assert(!document.setObjectMesh(child, vortex::MeshId{999999}));
    assert(document.validate());

    // Deleting a parent detaches its children rather than leaving a stale ID.
    assert(document.removeObject(parent));
    const vortex::ObjectBlock* childData = document.object(child);
    assert(childData != nullptr);
    assert(!childData->parentId);
    assert(document.validate());

    assert(document.setObjectMesh(child, {}));
    assert(document.removeMesh(sharedMesh));
    assert(document.meshCount() == 0);
    assert(document.validate());

    std::cout << "Vortex3D Phase 0 core smoke test passed\n";
    return 0;
}
