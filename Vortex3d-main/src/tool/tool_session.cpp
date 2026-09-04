#include "vortex/tool/tool_session.hpp"

namespace vortex {

OperatorResult TranslateToolSession::commit() {
    if (!active_ || context_ == nullptr) {
        return {OperatorStatus::Cancelled, {}};
    }
    TranslateSelectionOperator operation{previewOffset_};
    OperatorResult result = operation.execute(*context_);
    if (result.ok()) {
        active_ = false;
    }
    return result;
}

} // namespace vortex
