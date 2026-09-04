#include "vortex/core/command.hpp"

namespace vortex {

Transaction::Transaction(Document& document)
    : document_(document), snapshot_(document), startRevision_(document.revision()) {}

Transaction::~Transaction() {
    if (active_) {
        rollback();
    }
}

bool Transaction::execute(Command& command) {
    if (!active_ || failed_) {
        return false;
    }

    if (!command.apply(document_)) {
        failed_ = true;
        rollback();
        return false;
    }

    return true;
}

bool Transaction::commit() {
    if (!active_ || failed_) {
        return false;
    }

    active_ = false;
    return true;
}

void Transaction::rollback() {
    if (!active_) {
        return;
    }

    document_ = snapshot_;
    active_ = false;
}

} // namespace vortex
