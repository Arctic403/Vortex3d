#pragma once

#include "vortex/editor/operator.hpp"

namespace vortex {

class TranslateToolSession final {
public:
    explicit TranslateToolSession(EditorContext& context) noexcept : context_(&context) {}

    void update(Vec3 offset) noexcept { previewOffset_ = offset; }
    [[nodiscard]] Vec3 previewOffset() const noexcept { return previewOffset_; }
    [[nodiscard]] bool active() const noexcept { return active_; }
    void cancel() noexcept { active_ = false; previewOffset_ = {}; }
    [[nodiscard]] OperatorResult commit();

private:
    EditorContext* context_ = nullptr;
    Vec3 previewOffset_{};
    bool active_ = true;
};

} // namespace vortex
