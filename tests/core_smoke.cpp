#include "vortex/core/command.hpp"
#include "vortex/core/document.hpp"

#include <cassert>
#include <iostream>
#include <string_view>

namespace {

class ParentCommand final : public vortex::Command {
public:
    ParentCommand(vortex::ObjectId child, vortex::ObjectId parent) : child_(child), parent_(parent) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Set Parent"; }

    [[nodiscard]] bool apply(vortex::Document& document) override {
        return document.setObjectParent(child_, parent_);
    }

private:
    vortex::ObjectId child_;
    vortex::ObjectId parent_;
};

class FailingCommand final : public vortex::Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Fail"; }
    [[nodiscard]] bool apply(vortex::Document&) override { return false; }
};

} // namespace

int main() {
    vortex::Document document;
    assert(document.id());
    assert(document.revision() == 0);
    assert(document.validate());

    const vortex::SceneId scene = document.createScene("Main Scene");
    assert(scene);
    const vortex::SceneBlock* sceneData = document.scene(scene);
    assert(sceneData != nullptr);
    assert(sceneData->rootCollectionId);
    assert(document.collection(sceneData->rootCollectionId) != nullptr);

    const vortex::CollectionId props = document.createCollection(scene, "Props");
    assert(props);
    assert(document.collectionCount() == 2);

    const vortex::MeshId sharedMesh = document.createMesh("Shared Cube Mesh");
    const vortex::ObjectId parent = document.createObject("Parent", sharedMesh);
    const vortex::ObjectId child = document.createObject("Child", sharedMesh);
    assert(parent && child);
    assert(document.meshUserCount(sharedMesh) == 2);
    assert(document.linkObjectToCollection(parent, props));
    assert(document.linkObjectToCollection(child, props));
    assert(document.validate());

    // Shared data can be split without mutating the other object.
    const vortex::MeshId childMesh = document.makeObjectMeshUnique(child);
    assert(childMesh);
    assert(childMesh != sharedMesh);
    assert(document.meshCount() == 2);
    assert(document.meshUserCount(sharedMesh) == 1);
    assert(document.meshUserCount(childMesh) == 1);
    assert(document.object(parent)->meshId == sharedMesh);
    assert(document.object(child)->meshId == childMesh);

    // Commands execute through a transaction and commit atomically.
    ParentCommand setParent{child, parent};
    {
        vortex::Transaction transaction{document};
        assert(transaction.execute(setParent));
        assert(transaction.commit());
    }
    assert(document.object(child)->parentId == parent);

    // A failed command restores the complete document snapshot.
    const std::uint64_t beforeFailure = document.revision();
    FailingCommand fail;
    {
        vortex::Transaction transaction{document};
        assert(!transaction.execute(fail));
        assert(transaction.failed());
    }
    assert(document.revision() == beforeFailure);
    assert(document.object(child)->parentId == parent);

    // A transaction that is not committed rolls back on destruction.
    {
        vortex::Transaction transaction{document};
        ParentCommand clearParent{child, {}};
        assert(transaction.execute(clearParent));
        assert(!document.object(child)->parentId);
    }
    assert(document.object(child)->parentId == parent);

    // Parent cycles remain forbidden.
    assert(!document.setObjectParent(parent, child));
    assert(document.validate());

    // Removing an object clears collection membership and child references safely.
    assert(document.removeObject(parent));
    assert(!document.collection(props)->objectIds.contains(parent));
    assert(!document.object(child)->parentId);
    assert(document.validate());

    // Change history is queryable by document revision.
    const auto allChanges = document.changesSince(0);
    assert(!allChanges.empty());
    assert(allChanges.back().revision == document.revision());

    assert(document.setObjectMesh(child, {}));
    assert(document.removeMesh(childMesh));
    assert(document.removeMesh(sharedMesh));
    assert(document.meshCount() == 0);
    assert(document.validate());

    std::cout << "Vortex3D Phase 0.1 document hardening smoke test passed\n";
    return 0;
}
