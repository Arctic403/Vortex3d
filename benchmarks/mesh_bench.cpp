#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options final {
    std::size_t scale = 10'000;
    bool smoke = false;
    std::string outputPath;
};

struct Result final {
    std::string name;
    std::size_t elements = 0;
    double milliseconds = 0.0;
    std::uint64_t checksum = 0;
    bool capped = false;
};

[[nodiscard]] bool parseSize(const std::string_view text, std::size_t& value) {
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed == 0 ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

[[nodiscard]] Options parseOptions(const int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--smoke") {
            options.smoke = true;
            options.scale = 1'000;
        } else if (argument == "--scale" && index + 1 < argc) {
            ++index;
            if (!parseSize(argv[index], options.scale)) {
                throw std::runtime_error("invalid --scale value");
            }
        } else if (argument == "--output" && index + 1 < argc) {
            options.outputPath = argv[++index];
        } else {
            throw std::runtime_error("usage: vortex_mesh_bench [--smoke] [--scale N] [--output path]");
        }
    }
    return options;
}

template <typename Function>
[[nodiscard]] double timeMilliseconds(Function&& function) {
    const auto start = Clock::now();
    std::forward<Function>(function)();
    const auto finish = Clock::now();
    return std::chrono::duration<double, std::milli>(finish - start).count();
}

[[nodiscard]] vortex::EditableMesh makeVertexCloud(const std::size_t count) {
    vortex::EditableMesh mesh;
    for (std::size_t index = 0; index < count; ++index) {
        const float x = static_cast<float>(index % 1024U);
        const float y = static_cast<float>((index / 1024U) % 1024U);
        (void)mesh.addVertex({x, y, 0.0F});
    }
    return mesh;
}

[[nodiscard]] vortex::EditableMesh makeEdgeStrip(const std::size_t edgeCount) {
    vortex::EditableMesh mesh;
    std::vector<vortex::VertexId> vertices;
    vertices.reserve(edgeCount + 1U);
    for (std::size_t index = 0; index <= edgeCount; ++index) {
        vertices.push_back(mesh.addVertex({static_cast<float>(index), 0.0F, 0.0F}));
    }
    for (std::size_t index = 0; index < edgeCount; ++index) {
        (void)mesh.addEdge(vertices[index], vertices[index + 1U]);
    }
    return mesh;
}

[[nodiscard]] vortex::EditableMesh makeTriangleSet(const std::size_t faceCount) {
    vortex::EditableMesh mesh;
    for (std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
        const float base = static_cast<float>(faceIndex * 2U);
        const auto a = mesh.addVertex({base, 0.0F, 0.0F});
        const auto b = mesh.addVertex({base + 1.0F, 0.0F, 0.0F});
        const auto c = mesh.addVertex({base, 1.0F, 0.0F});
        (void)mesh.addFace({a, b, c});
    }
    return mesh;
}

[[nodiscard]] std::size_t topologyCaseElements(const Options& options) {
    const std::size_t cap = options.smoke ? 100U : 2'000U;
    return std::min(std::max<std::size_t>(1U, options.scale / 10U), cap);
}

[[nodiscard]] std::size_t edgeCaseElements(const Options& options) {
    return options.scale;
}

[[nodiscard]] std::vector<Result> runBenchmarks(const Options& options) {
    std::vector<Result> results;
    results.reserve(13);

    {
        vortex::EditableMesh mesh;
        const double elapsed = timeMilliseconds([&] {
            for (std::size_t index = 0; index < options.scale; ++index) {
                (void)mesh.addVertex({static_cast<float>(index), 0.0F, 0.0F});
            }
        });
        results.push_back({"vertex_create", options.scale, elapsed, mesh.vertexCount(), false});
    }

    const std::size_t edgeCount = edgeCaseElements(options);
    {
        vortex::EditableMesh mesh;
        std::vector<vortex::VertexId> vertices;
        vertices.reserve(edgeCount + 1U);
        for (std::size_t index = 0; index <= edgeCount; ++index) {
            vertices.push_back(mesh.addVertex({static_cast<float>(index), 0.0F, 0.0F}));
        }
        const double elapsed = timeMilliseconds([&] {
            for (std::size_t index = 0; index < edgeCount; ++index) {
                (void)mesh.addEdge(vertices[index], vertices[index + 1U]);
            }
        });
        results.push_back({"edge_create", edgeCount, elapsed, mesh.edgeCount(), false});
    }


    {
        const std::size_t side = std::max<std::size_t>(
            1U,
            static_cast<std::size_t>(std::sqrt(static_cast<double>(options.scale))));
        vortex::EditableMesh gridMesh;
        std::vector<vortex::VertexId> gridVertices;
        gridVertices.reserve((side + 1U) * (side + 1U));
        for (std::size_t y = 0; y <= side; ++y) {
            for (std::size_t x = 0; x <= side; ++x) {
                gridVertices.push_back(gridMesh.addVertex({
                    static_cast<float>(x),
                    static_cast<float>(y),
                    0.0F}));
            }
        }
        const auto vertexAt = [&](const std::size_t x, const std::size_t y) {
            return gridVertices[y * (side + 1U) + x];
        };
        const double elapsed = timeMilliseconds([&] {
            for (std::size_t y = 0; y < side; ++y) {
                for (std::size_t x = 0; x < side; ++x) {
                    (void)gridMesh.addFace({
                        vertexAt(x, y),
                        vertexAt(x + 1U, y),
                        vertexAt(x + 1U, y + 1U),
                        vertexAt(x, y + 1U)});
                }
            }
        });
        results.push_back({
            "quad_grid_face_create",
            side * side,
            elapsed,
            static_cast<std::uint64_t>(gridMesh.edgeCount()),
            false});
    }

    const std::size_t faceCount = topologyCaseElements(options);
    vortex::EditableMesh topologyMesh;
    {
        std::vector<std::array<vortex::VertexId, 3>> triangles;
        triangles.reserve(faceCount);
        for (std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
            const float base = static_cast<float>(faceIndex * 2U);
            triangles.push_back({
                topologyMesh.addVertex({base, 0.0F, 0.0F}),
                topologyMesh.addVertex({base + 1.0F, 0.0F, 0.0F}),
                topologyMesh.addVertex({base, 1.0F, 0.0F})});
        }
        const double elapsed = timeMilliseconds([&] {
            for (const auto& triangle : triangles) {
                (void)topologyMesh.addFace({triangle[0], triangle[1], triangle[2]});
            }
        });
        results.push_back({
            "face_create",
            faceCount,
            elapsed,
            topologyMesh.faceCount(),
            faceCount * 10U < options.scale});
    }

    {
        std::uint64_t checksum = 0;
        const double elapsed = timeMilliseconds([&] {
            for (const vortex::FaceId faceId : topologyMesh.faceIds()) {
                const vortex::MeshFace* face = topologyMesh.face(faceId);
                if (face == nullptr) {
                    continue;
                }
                vortex::CornerId cursor = face->firstCorner;
                for (std::uint32_t cornerIndex = 0; cornerIndex < face->cornerCount; ++cornerIndex) {
                    const vortex::MeshCorner* corner = topologyMesh.corner(cursor);
                    if (corner == nullptr) {
                        break;
                    }
                    checksum += corner->vertexId.value();
                    cursor = corner->next;
                }
            }
        });
        results.push_back({"topology_traversal", topologyMesh.cornerCount(), elapsed, checksum, faceCount * 10U < options.scale});
    }

    {
        vortex::EditableMesh splitMesh = makeEdgeStrip(edgeCount);
        const auto edges = splitMesh.edgeIds();
        const vortex::EdgeId target = edges[edges.size() / 2U];
        std::uint64_t checksum = 0;
        const double elapsed = timeMilliseconds([&] {
            const auto split = splitMesh.splitEdge(target, 0.5F);
            if (split) {
                checksum = split->newVertex.value() ^ split->newEdge.value();
            }
        });
        results.push_back({"edge_split", edgeCount, elapsed, checksum, false});
    }

    {
        vortex::EditableMesh extrudeMesh = makeTriangleSet(faceCount);
        const vortex::FaceId target = extrudeMesh.faceIds().front();
        std::uint64_t checksum = 0;
        const double elapsed = timeMilliseconds([&] {
            const auto extrusion = extrudeMesh.extrudeFace(target, {0.0F, 0.0F, 1.0F});
            if (extrusion) {
                checksum = extrusion->capFace.value();
            }
        });
        results.push_back({"face_extrude", faceCount, elapsed, checksum, faceCount * 10U < options.scale});
    }

    vortex::EditableMesh valueMesh = makeVertexCloud(options.scale);
    (void)valueMesh.attributes().create<float>("bench_weight", vortex::AttributeDomain::Vertex, 0.0F);

    {
        std::uint64_t checksum = 0;
        const std::size_t iterations = std::max<std::size_t>(options.scale, 10'000U);
        const double elapsed = timeMilliseconds([&] {
            for (std::size_t index = 0; index < iterations; ++index) {
                const auto* values = valueMesh.attributes().values<float>("bench_weight", vortex::AttributeDomain::Vertex);
                checksum += values == nullptr ? 0U : values->size();
            }
        });
        results.push_back({"attribute_lookup", iterations, elapsed, checksum, false});
    }

    {
        auto* values = valueMesh.attributes().values<float>("bench_weight", vortex::AttributeDomain::Vertex);
        std::uint64_t checksum = 0;
        const double elapsed = timeMilliseconds([&] {
            if (values != nullptr) {
                for (std::size_t index = 0; index < values->size(); ++index) {
                    (*values)[index] = static_cast<float>(index % 17U);
                }
                checksum = values->size();
            }
        });
        results.push_back({"attribute_mutation", options.scale, elapsed, checksum, false});
    }

    {
        std::uint64_t checksum = 0;
        const double elapsed = timeMilliseconds([&] {
            for (const vortex::VertexId vertexId : valueMesh.vertexIds()) {
                const auto current = valueMesh.position(vertexId);
                if (current) {
                    (void)valueMesh.setPosition(vertexId, {current->x + 1.0F, current->y, current->z});
                    checksum += vertexId.value();
                }
            }
        });
        results.push_back({"vertex_move", options.scale, elapsed, checksum, false});
    }

    const std::size_t historyVertices = std::min<std::size_t>(options.scale, options.smoke ? 250U : 100'000U);
    {
        vortex::EditableMesh historyMesh = makeVertexCloud(historyVertices);
        std::vector<vortex::VertexPositionTarget> targets;
        targets.reserve(historyVertices);
        for (const vortex::VertexId vertexId : historyMesh.vertexIds()) {
            const auto position = historyMesh.position(vertexId);
            if (position) {
                targets.push_back({vertexId, {position->x, position->y + 1.0F, position->z}});
            }
        }
        vortex::MeshHistory history(std::size_t{256} * std::size_t{1024} * std::size_t{1024});
        vortex::MoveVerticesCommand move{std::move(targets)};
        if (!history.execute(historyMesh, move)) {
            throw std::runtime_error("history setup failed");
        }

        const double undoMs = timeMilliseconds([&] {
            if (!history.undo(historyMesh)) {
                throw std::runtime_error("undo benchmark failed");
            }
        });
        results.push_back({"undo", historyVertices, undoMs, history.undoCount(), historyVertices < options.scale});

        const double redoMs = timeMilliseconds([&] {
            if (!history.redo(historyMesh)) {
                throw std::runtime_error("redo benchmark failed");
            }
        });
        results.push_back({"redo", historyVertices, redoMs, history.redoCount(), historyVertices < options.scale});
    }

    {
        bool valid = false;
        const double elapsed = timeMilliseconds([&] { valid = static_cast<bool>(topologyMesh.validate()); });
        results.push_back({"validation", topologyMesh.cornerCount(), elapsed, valid ? 1U : 0U, faceCount * 10U < options.scale});
    }

    return results;
}

void writeJson(std::ostream& output, const Options& options, const std::vector<Result>& results) {
    using VertexMap = std::unordered_map<vortex::VertexId, vortex::MeshVertex, vortex::IdHash<vortex::VertexId>>;
    using EdgeMap = std::unordered_map<vortex::EdgeId, vortex::MeshEdge, vortex::IdHash<vortex::EdgeId>>;
    using FaceMap = std::unordered_map<vortex::FaceId, vortex::MeshFace, vortex::IdHash<vortex::FaceId>>;
    using CornerMap = std::unordered_map<vortex::CornerId, vortex::MeshCorner, vortex::IdHash<vortex::CornerId>>;

    output << std::fixed << std::setprecision(3);
    output << "{\n";
    output << "  \"requested_scale\": " << options.scale << ",\n";
    output << "  \"pointer_bits\": " << sizeof(void*) * 8U << ",\n";
    output << "  \"layout_bytes\": {\n";
    output << "    \"vertex\": " << sizeof(vortex::MeshVertex) << ",\n";
    output << "    \"edge\": " << sizeof(vortex::MeshEdge) << ",\n";
    output << "    \"face\": " << sizeof(vortex::MeshFace) << ",\n";
    output << "    \"corner\": " << sizeof(vortex::MeshCorner) << ",\n";
    output << "    \"attribute_layer\": " << sizeof(vortex::AttributeLayer) << ",\n";
    output << "    \"vertex_hash_payload\": " << sizeof(VertexMap::value_type) << ",\n";
    output << "    \"edge_hash_payload\": " << sizeof(EdgeMap::value_type) << ",\n";
    output << "    \"face_hash_payload\": " << sizeof(FaceMap::value_type) << ",\n";
    output << "    \"corner_hash_payload\": " << sizeof(CornerMap::value_type) << ",\n";
    output << "    \"bucket_pointer\": " << sizeof(void*) << "\n";
    output << "  },\n";
    output << "  \"benchmarks\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const Result& result = results[index];
        output << "    {\"name\": \"" << result.name << "\", \"elements\": " << result.elements
               << ", \"milliseconds\": " << result.milliseconds << ", \"checksum\": " << result.checksum
               << ", \"capped\": " << (result.capped ? "true" : "false") << "}";
        output << (index + 1U == results.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"notes\": [\n";
    output << "    \"Edge creation and shared-edge quad-grid creation run at requested scale using the maintained undirected-edge acceleration index.\",\n";
    output << "    \"Hash payload sizes exclude allocator/node bookkeeping; use them with benchmark timing before redesigning storage.\"\n";
    output << "  ]\n";
    output << "}\n";
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        const std::vector<Result> results = runBenchmarks(options);

        if (options.outputPath.empty()) {
            writeJson(std::cout, options, results);
        } else {
            std::ofstream output{options.outputPath};
            if (!output) {
                std::cerr << "Unable to open benchmark output: " << options.outputPath << '\n';
                return 2;
            }
            writeJson(output, options, results);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark error: " << error.what() << '\n';
        return 1;
    }
}
