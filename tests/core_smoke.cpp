#include "vortex/core/command.hpp"
#include "vortex/core/document.hpp"
#include "vortex/core/document_commands.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <string_view>
#include <type_traits>

namespace {

class FailingCommand final : public vortex::Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Fail"; }
    [[nodiscard]] std::optional<vortex::DocumentHistoryRecord> apply(vortex::Document&) override {
        return std::nullopt;
    }
};

} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<vortex::Document>);
    static_assert(!std::is_copy_assignable_v<vortex::Document>);
    static_assert(std::is_move_constructible_v<vortex::Document>);

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

    assert(document.id().value() != scene.value());
    assert(scene.value() != sceneData->rootCollectionId.value());
    assert(!document.hasObject({}));
    assert(!document.hasMesh({}));

    const vortex::MeshId sharedMesh = document.createMesh("Shared Cube Mesh");
    const vortex::ObjectId parent = document.createObject("Parent", sharedMesh);
    const vortex::ObjectId child = document.createObject("Child", sharedMesh);
    assert(parent && child);
    assert(document.meshUserCount(sharedMesh) == 2);
    assert(document.linkObjectToCollection(parent, props));
    assert(document.linkObjectToCollection(child, props));
    assert(document.validate());

    const vortex::MeshId childMesh = document.makeObjectMeshUnique(child);
    assert(childMesh);
    assert(childMesh != sharedMesh);
    assert(document.meshCount() == 2);
    assert(document.meshUserCount(sharedMesh) == 1);
    assert(document.meshUserCount(childMesh) == 1);
    assert(document.object(parent)->meshId == sharedMesh);
    assert(document.object(child)->meshId == childMesh);

    vortex::SetObjectParentCommand setParent{child, parent};
    {
        vortex::Transaction transaction{document};
        assert(transaction.execute(setParent));
        assert(transaction.commit());
    }
    assert(document.object(child)->parentId == parent);

    {
        vortex::Transaction transaction{document};
        vortex::RenameObjectCommand rename{child, "Child Renamed"};
        assert(transaction.execute(rename));
        assert(transaction.commit());
    }
    assert(document.object(child)->name == "Child Renamed");

    const std::uint64_t beforeFailure = document.revision();
    FailingCommand fail;
    {
        vortex::Transaction transaction{document};
        assert(!transaction.execute(fail));
        assert(transaction.failed());
    }
    assert(document.revision() == beforeFailure);
    assert(document.object(child)->parentId == parent);

    const std::uint64_t beforeImplicitRollback = document.revision();
    {
        vortex::Transaction transaction{document};
        vortex::SetObjectParentCommand clearParent{child, {}};
        vortex::RenameObjectCommand temporaryName{child, "Temporary"};
        assert(transaction.execute(clearParent));
        assert(transaction.execute(temporaryName));
        assert(!document.object(child)->parentId);
        assert(document.object(child)->name == "Temporary");
    }
    assert(document.revision() == beforeImplicitRollback);
    assert(document.object(child)->parentId == parent);
    assert(document.object(child)->name == "Child Renamed");

    assert(!document.setObjectParent(parent, child));
    assert(document.validate());

    assert(document.removeObject(parent));
    assert(!document.collection(props)->objectIds.contains(parent));
    assert(!document.object(child)->parentId);
    assert(document.validate());

    const auto allChanges = document.changesSince(0);
    assert(!allChanges.empty());
    assert(allChanges.back().revision == document.revision());

    assert(document.setObjectMesh(child, {}));
    assert(document.removeMesh(childMesh));
    assert(document.removeMesh(sharedMesh));
    assert(document.meshCount() == 0);
    assert(document.validate());

    std::cout << "Vortex3D document delta-transaction smoke test passed\n";
    return 0;
}
