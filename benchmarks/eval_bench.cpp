#include "vortex/core/document.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/eval/normals.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
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
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

struct Options final {
    std::size_t scale = 10'000;
    bool smoke = false;
    std::string outputPath;
};

[[nodiscard]] bool parseSize(const std::string_view text, std::size_t& value) {
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed == 0U ||
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
            throw std::runtime_error("usage: vortex_eval_bench [--smoke] [--scale N] [--output path]");
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

[[nodiscard]] vortex::EditableMesh makeSmoothQuadStrip(const std::size_t faceCount) {
    vortex::EditableMesh mesh;
    vortex::VertexId bottom = mesh.addVertex({0.0F, 0.0F, 0.0F});
    vortex::VertexId top = mesh.addVertex({0.0F, 1.0F, 0.0F});

    for (std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
        const float x = static_cast<float>(faceIndex + 1U);
        const float z = static_cast<float>(faceIndex % 11U) * 0.001F;
        const vortex::VertexId nextBottom = mesh.addVertex({x, 0.0F, z});
        const vortex::VertexId nextTop = mesh.addVertex({x, 1.0F, z});
        const vortex::FaceId face = mesh.addFace({bottom, nextBottom, nextTop, top});
        if (!face) {
            throw std::runtime_error("failed to build normal benchmark strip");
        }
        bottom = nextBottom;
        top = nextTop;
    }

    if (!mesh.attributes().create<bool>("sharp_face", vortex::AttributeDomain::Face, false)) {
        throw std::runtime_error("failed to create smooth-face benchmark attribute");
    }
    if (!mesh.validate()) {
        throw std::runtime_error("normal benchmark fixture is invalid");
    }
    return mesh;
}

void writeJson(
    std::ostream& output,
    const Options& options,
    const std::size_t faceCount,
    const std::size_t cornerCount,
    const double milliseconds,
    const std::size_t retainedBytes,
    const std::uint64_t checksum) {
    output << std::fixed << std::setprecision(3);
    output << "{\n";
    output << "  \"requested_scale\": " << options.scale << ",\n";
    output << "  \"pointer_bits\": " << sizeof(void*) * 8U << ",\n";
    output << "  \"benchmark\": {\n";
    output << "    \"name\": \"derived_normals_smooth_strip\",\n";
    output << "    \"faces\": " << faceCount << ",\n";
    output << "    \"corners\": " << cornerCount << ",\n";
    output << "    \"milliseconds\": " << milliseconds << ",\n";
    output << "    \"evaluated_retained_bytes\": " << retainedBytes << ",\n";
    output << "    \"checksum\": " << checksum << ",\n";
    output << "    \"capped\": " << (cornerCount < options.scale ? "true" : "false") << "\n";
    output << "  }\n";
    output << "}\n";
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        const std::size_t requestedFaces = std::max<std::size_t>(1U, (options.scale + 3U) / 4U);
        const std::size_t faceCount = requestedFaces;

        vortex::Document document;
        const vortex::MeshId meshId = document.createMesh("Normal Benchmark", makeSmoothQuadStrip(faceCount));
        const vortex::MeshBlock* source = document.mesh(meshId);
        if (source == nullptr) {
            throw std::runtime_error("failed to create normal benchmark MeshBlock");
        }

        const vortex::MeshEvaluationResult baseline = vortex::MeshEvaluator::evaluate(*source);
        if (!baseline || !baseline.mesh.has_value()) {
            throw std::runtime_error("failed to create normal benchmark evaluated mesh");
        }

        vortex::EvaluatedMesh evaluated = baseline.mesh.value();
        vortex::NormalGenerationResult result;
        const double milliseconds = timeMilliseconds([&] {
            result = vortex::DerivedNormalsGenerator::generate(evaluated);
        });
        if (!result) {
            throw std::runtime_error("derived normal benchmark failed");
        }

        const auto* normals = evaluated.attributes().values<vortex::Vec3>("normal", vortex::AttributeDomain::Corner);
        if (normals == nullptr || normals->size() != evaluated.cornerCount()) {
            throw std::runtime_error("derived normal benchmark produced no corner normals");
        }

        std::uint64_t checksum = 0U;
        for (const vortex::Vec3 normal : *normals) {
            checksum += static_cast<std::uint64_t>(std::abs(normal.z) * 1'000'000.0F);
        }

        if (options.outputPath.empty()) {
            writeJson(
                std::cout,
                options,
                evaluated.faceCount(),
                evaluated.cornerCount(),
                milliseconds,
                evaluated.estimatedRetainedBytes(),
                checksum);
        } else {
            std::ofstream output(options.outputPath);
            if (!output) {
                throw std::runtime_error("failed to open benchmark output");
            }
            writeJson(
                output,
                options,
                evaluated.faceCount(),
                evaluated.cornerCount(),
                milliseconds,
                evaluated.estimatedRetainedBytes(),
                checksum);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vortex_eval_bench: " << error.what() << '\n';
        return 1;
    }
}
