#include "vortex/engine.hpp"

#include <cassert>
#include <iostream>

int main() {
    vortex::EditableMesh mesh;
    const auto a=mesh.addVertex({0.0F,0.0F,0.0F});
    const auto b=mesh.addVertex({2.0F,0.0F,0.0F});
    const auto c=mesh.addVertex({2.0F,2.0F,0.0F});
    const auto d=mesh.addVertex({0.0F,2.0F,0.0F});
    const auto face=mesh.addFace({a,b,c,d});
    assert(face);
    const auto temporary=mesh.addVertex({9.0F,9.0F,9.0F});
    assert(temporary && mesh.removeVertex(temporary));
    const auto postGap=mesh.addVertex({4.0F,4.0F,0.0F});
    assert(postGap && postGap.value() > temporary.value());
    assert(mesh.attributes().create<float>("weight", vortex::AttributeDomain::Vertex, 0.5F));
    auto* weights=mesh.attributes().values<float>("weight", vortex::AttributeDomain::Vertex);
    assert(weights && weights->size()==5U);
    (*weights)[2]=0.9F;

    vortex::Document document;
    const auto scene=document.createScene("Scene");
    const auto meshId=document.createMesh("Shared Mesh", std::move(mesh));
    const auto parent=document.createObject("Parent",meshId);
    const auto child=document.createObject("Child",meshId);
    assert(scene&&meshId&&parent&&child);
    assert(document.setObjectParent(child,parent));
    assert(document.linkObjectToCollection(parent,document.scene(scene)->rootCollectionId));
    assert(document.linkObjectToCollection(child,document.scene(scene)->rootCollectionId));

    const auto encoded=vortex::ProjectCodec::encode(document);
    assert(encoded.ok() && encoded.bytes.size()>32U);
    const auto decoded=vortex::ProjectCodec::decode(encoded.bytes);
    assert(decoded.ok());
    assert(decoded.document.id()==document.id());
    assert(decoded.document.meshCount()==1U && decoded.document.objectCount()==2U && decoded.document.sceneCount()==1U);
    assert(decoded.document.hasMesh(meshId) && decoded.document.hasObject(parent) && decoded.document.hasObject(child));
    assert(decoded.document.object(child)->parentId==parent);
    const auto* loaded=decoded.document.authoredMesh(meshId);
    assert(loaded && loaded->hasFace(face) && loaded->hasVertex(c) && loaded->hasVertex(postGap) && loaded->validateStrict());
    const auto* loadedWeights=loaded->attributes().values<float>("weight",vortex::AttributeDomain::Vertex);
    assert(loadedWeights && loadedWeights->size()==5U && (*loadedWeights)[2]==0.9F);

    auto corrupted=encoded.bytes;
    corrupted.back()^=0x5AU;
    assert(vortex::ProjectCodec::decode(corrupted).error==vortex::ProjectIoError::IntegrityMismatch);

    std::cout << "Vortex3D project format v0.1 smoke passed\n";
    return 0;
}
