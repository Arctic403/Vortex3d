#pragma once

#include "vortex/editor/context.hpp"
#include "vortex/mesh/command.hpp"

#include <memory>
#include <string_view>

namespace vortex {

enum class OperatorStatus : std::uint8_t {
    Finished,
    Cancelled,
    InvalidContext,
    Failed,
};

struct OperatorResult final {
    OperatorStatus status = OperatorStatus::Failed;
    MeshCommandResult meshResult;

    [[nodiscard]] bool ok() const noexcept { return status == OperatorStatus::Finished; }
    explicit operator bool() const noexcept { return ok(); }
};

class Operator {
public:
    virtual ~Operator() = default;
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual OperatorResult execute(EditorContext& context) = 0;
};

class ExtrudeFaceOperator final : public Operator {
public:
    explicit ExtrudeFaceOperator(Vec3 offset) noexcept : offset_(offset) {}
    [[nodiscard]] std::string_view id() const noexcept override { return "mesh.extrude_faces"; }
    [[nodiscard]] OperatorResult execute(EditorContext& context) override;

private:
    Vec3 offset_;
};

class TranslateSelectionOperator final : public Operator {
public:
    explicit TranslateSelectionOperator(Vec3 offset) noexcept : offset_(offset) {}
    [[nodiscard]] std::string_view id() const noexcept override { return "mesh.translate_selection"; }
    [[nodiscard]] OperatorResult execute(EditorContext& context) override;

private:
    Vec3 offset_;
};

} // namespace vortex
