#pragma once

#include "vortex/core/document.hpp"

#include <string_view>

namespace vortex {

class Command {
public:
    virtual ~Command() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool apply(Document& document) = 0;
};

class Transaction final {
public:
    explicit Transaction(Document& document);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    [[nodiscard]] bool execute(Command& command);
    [[nodiscard]] bool commit();
    void rollback();

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::uint64_t startRevision() const noexcept { return startRevision_; }

private:
    Document& document_;
    Document snapshot_;
    std::uint64_t startRevision_ = 0;
    bool active_ = true;
    bool failed_ = false;
};

} // namespace vortex
