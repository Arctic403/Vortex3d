#include "vortex/engine.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

vortex::EditableMesh makeQuad() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({-1.0F, -1.0F, 0.0F});
    const auto b = mesh.addVertex({ 1.0F, -1.0F, 0.0F});
    const auto c = mesh.addVertex({ 1.0F,  1.0F, 0.0F});
    const auto d = mesh.addVertex({-1.0F,  1.0F, 0.0F});
    assert(a && b && c && d);
    assert(mesh.addFace({a,b,c,d}));
    assert(mesh.validateStrict());
    return mesh;
}

void testGeometryQueriesAndInset() {
    auto mesh = makeQuad();
    const auto face = mesh.faceIds().front();
    const auto topology = vortex::GeometryOperations::faceTopology(mesh, face);
    assert(topology && topology->vertices.size() == 4U);
    assert(vortex::GeometryOperations::faceCentroid(mesh, face));
    assert(vortex::GeometryOperations::incidentEdges(mesh, topology->vertices.front()).size() == 2U);
    const auto inset = vortex::GeometryOperations::insetFace(mesh, face, 0.25F);
    assert(inset && inset->rimFaces.size() == 4U && mesh.faceCount() == 5U);
    assert(mesh.validateStrict());
}

void testEditorOperatorAndTool() {
    vortex::Document document;
    const auto meshId = document.createMesh("Quad", makeQuad());
    const auto objectId = document.createObject("Quad", meshId);
    assert(meshId && objectId);
    vortex::EditorHistory history;
    vortex::EditorContext context{document, history};
    assert(context.setActiveObject(objectId));
    context.setMode(vortex::EditorMode::Edit);
    context.setSelectionDomain(vortex::SelectionDomain::Face);
    const auto face = document.authoredMesh(meshId)->faceIds().front();
    context.selection().faces.insert(face);
    vortex::ExtrudeFaceOperator extrude{{0.0F,0.0F,1.0F}};
    const auto extruded = extrude.execute(context);
    assert(extruded && history.undoCount() == 1U);
    assert(context.selection().faces.size() == 1U);
    assert(history.undo(document));
    assert(history.redo(document));

    context.selection().clear();
    context.setSelectionDomain(vortex::SelectionDomain::Vertex);
    context.selection().vertices.insert(document.authoredMesh(meshId)->vertexIds().front());
    vortex::TranslateToolSession tool{context};
    tool.update({0.5F,0.0F,0.0F});
    assert(tool.active());
    assert(tool.commit());
    assert(!tool.active());
}

void testDependencyGraph() {
    vortex::DependencyGraph graph;
    const auto source = graph.addNode("source");
    const auto modifier = graph.addNode("modifier");
    const auto viewport = graph.addNode("viewport");
    assert(graph.addDependency(source, modifier));
    assert(graph.addDependency(modifier, viewport));
    assert(!graph.addDependency(viewport, source));
    graph.markAllClean();
    assert(graph.markDirty(source));
    const auto dirty = graph.dirtyEvaluationOrder();
    assert(dirty.size() == 3U && dirty.front() == source && dirty.back() == viewport);
}

void testProceduralAndViewport() {
    vortex::Document document;
    const auto meshId = document.createMesh("Quad", makeQuad());
    const auto* block = document.mesh(meshId);
    assert(block != nullptr);

    vortex::GeometryGraph graph;
    const auto transform = graph.addNode(vortex::GeometryTransformNode{{1.0F,0.0F,0.0F},{},{1.0F,1.0F,1.0F}});
    const auto twist = graph.addNode(vortex::GeometryTwistNode{0.25F,0.0F});
    const auto triangulate = graph.addNode(vortex::GeometryTriangulateNode{});
    assert(graph.connect(transform, twist));
    assert(graph.connect(twist, triangulate));
    assert(graph.setOutput(triangulate));
    const auto evaluated = graph.evaluate(*block);
    assert(evaluated && evaluated.mesh->faceCount() == 2U);
    const auto render = vortex::RenderExtractor::extract(*evaluated.mesh);
    assert(render && render.mesh->triangles.size() == 2U && render.mesh->vertices.size() == 4U);
}

void testRegistries() {
    vortex::OperatorRegistry operators;
    vortex::ModifierRegistry modifiers;
    vortex::GeometryNodeRegistry nodes;
    vortex::ProjectAdapterRegistry adapters;
    vortex::registerBuiltinExtensions(operators, modifiers, &nodes, &adapters);
    assert(operators.create("mesh.extrude_faces") != nullptr);
    assert(modifiers.create("simple_deform_twist") != nullptr);
    assert(operators.ids().size() >= 2U);
    assert(modifiers.ids().size() >= 4U);
    assert(nodes.create("triangulate").has_value());
    assert(adapters.exporter(".vortex") != nullptr);

    vortex::PropertySchema schema;
    assert(schema.add({"strength", vortex::PropertyType::Float, 1.0F}));
    vortex::PropertyBag properties{&schema};
    assert(properties.set("strength", 2.0F));
    assert(!properties.set("strength", std::string{"wrong type"}));
    const auto* value = properties.get("strength");
    assert(value != nullptr && std::get<float>(*value) == 2.0F);
}

} // namespace

int main() {
    testGeometryQueriesAndInset();
    testEditorOperatorAndTool();
    testDependencyGraph();
    testProceduralAndViewport();
    testRegistries();
    std::cout << "Vortex3D v0.2 architecture smoke passed\n";
    return 0;
}
