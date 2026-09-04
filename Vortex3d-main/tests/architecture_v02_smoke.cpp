#include "vortex/engine.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

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

vortex::EditableMesh makeCube() {
    vortex::EditableMesh mesh;
    const auto v0 = mesh.addVertex({-1.0F, -1.0F, -1.0F});
    const auto v1 = mesh.addVertex({1.0F, -1.0F, -1.0F});
    const auto v2 = mesh.addVertex({1.0F, 1.0F, -1.0F});
    const auto v3 = mesh.addVertex({-1.0F, 1.0F, -1.0F});
    const auto v4 = mesh.addVertex({-1.0F, -1.0F, 1.0F});
    const auto v5 = mesh.addVertex({1.0F, -1.0F, 1.0F});
    const auto v6 = mesh.addVertex({1.0F, 1.0F, 1.0F});
    const auto v7 = mesh.addVertex({-1.0F, 1.0F, 1.0F});
    assert(v0 && v1 && v2 && v3 && v4 && v5 && v6 && v7);
    assert(mesh.addFace({v0, v3, v2, v1}));
    assert(mesh.addFace({v4, v5, v6, v7}));
    assert(mesh.addFace({v0, v1, v5, v4}));
    assert(mesh.addFace({v1, v2, v6, v5}));
    assert(mesh.addFace({v2, v3, v7, v6}));
    assert(mesh.addFace({v3, v0, v4, v7}));
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
    assert(graph.dependenciesOf(viewport) == std::vector<vortex::DependencyNodeId>{modifier});

    graph.markAllClean();
    assert(graph.markDirty(source));
    const auto dirty = graph.dirtyEvaluationOrder();
    assert(dirty.size() == 3U && dirty.front() == source && dirty.back() == viewport);

    graph.markClean(viewport);
    assert(graph.node(viewport) != nullptr && !graph.node(viewport)->dirty);
    assert(graph.markDirty(source));
    assert(graph.node(viewport)->dirty);

    graph.markAllClean();
    assert(graph.removeDependency(modifier, viewport));
    assert(graph.node(viewport)->dirty);
    graph.markAllClean();
    assert(graph.addDependency(modifier, viewport));
    assert(graph.node(viewport)->dirty);
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

    vortex::GeometryGraph isolatedGraph;
    (void)isolatedGraph.addNode(vortex::GeometryTransformNode{{100.0F,0.0F,0.0F},{},{1.0F,1.0F,1.0F}});
    const auto selected = isolatedGraph.addNode(vortex::GeometryTransformNode{{1.0F,0.0F,0.0F},{},{1.0F,1.0F,1.0F}});
    assert(isolatedGraph.setOutput(selected));
    const auto isolatedEvaluation = isolatedGraph.evaluate(*block);
    assert(isolatedEvaluation);
    const auto isolatedPosition = isolatedEvaluation.mesh->position(0U);
    assert(isolatedPosition && std::abs(isolatedPosition->x) < 1.0e-5F);

    vortex::GeometryGraph linearOnly;
    const auto first = linearOnly.addNode(vortex::GeometryTransformNode{});
    const auto second = linearOnly.addNode(vortex::GeometryMirrorNode{});
    const auto third = linearOnly.addNode(vortex::GeometryTriangulateNode{});
    assert(linearOnly.connect(first, second));
    assert(!linearOnly.connect(first, third));
    assert(!linearOnly.connect(third, second));

    vortex::Document cubeDocument;
    const auto cubeMeshId = cubeDocument.createMesh("Cube", makeCube());
    const auto* cubeBlock = cubeDocument.mesh(cubeMeshId);
    assert(cubeBlock != nullptr);
    const auto cubeEvaluation = vortex::MeshEvaluator::evaluate(*cubeBlock);
    assert(cubeEvaluation);
    const auto cubeRender = vortex::RenderExtractor::extract(*cubeEvaluation.mesh);
    assert(cubeRender);
    assert(cubeRender.mesh->triangles.size() == 12U);
    // Flat cube faces must retain split Corner normals instead of being averaged back
    // onto the eight authored vertices.
    assert(cubeRender.mesh->vertices.size() == 24U);
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
