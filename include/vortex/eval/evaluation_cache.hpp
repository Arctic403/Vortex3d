#pragma once

#include "vortex/eval/evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace vortex {

struct CachedEvaluationResult final {
    std::shared_ptr<const EvaluatedMesh> mesh;
    MeshEvaluationError error = MeshEvaluationError::None;
    ModifierApplyError modifierError = ModifierApplyError::None;
    NormalGenerationError normalError = NormalGenerationError::None;
    std::optional<std::size_t> modifierIndex;
    bool cacheHit = false;
    bool retainedByCache = false;

    [[nodiscard]] bool ok() const noexcept { return mesh != nullptr && error == MeshEvaluationError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

// Single-threaded v0.1 evaluation cache. The cache owns retained evaluated snapshots through
// shared_ptr because readers such as renderer/export code may legitimately outlive an eviction.
// The byte budget covers only ownership retained by this cache; callers are responsible for not
// pinning an unbounded number of snapshots externally.
class EvaluationCache final {
public:
    explicit EvaluationCache(
        std::size_t budgetBytes = std::size_t{16} * std::size_t{1024} * std::size_t{1024})
        : budgetBytes_(budgetBytes) {}

    [[nodiscard]] CachedEvaluationResult evaluate(
        const MeshBlock& source,
        std::span<const MeshModifier* const> modifiers = {});

    void clear() noexcept;
    void eraseMesh(MeshId sourceMeshId) noexcept;
    void setBudgetBytes(std::size_t budgetBytes) noexcept;

    [[nodiscard]] std::size_t budgetBytes() const noexcept { return budgetBytes_; }
    [[nodiscard]] std::size_t retainedBytes() const noexcept { return retainedBytes_; }
    [[nodiscard]] std::size_t entryCount() const noexcept { return entries_.size(); }
    [[nodiscard]] std::uint64_t hitCount() const noexcept { return hitCount_; }
    [[nodiscard]] std::uint64_t missCount() const noexcept { return missCount_; }
    [[nodiscard]] std::uint64_t evictionCount() const noexcept { return evictionCount_; }

private:
    struct Entry final {
        EvaluationCacheKey key;
        std::shared_ptr<const EvaluatedMesh> mesh;
        std::size_t retainedBytes = 0;
        std::uint64_t lastUse = 0;
    };

    [[nodiscard]] std::optional<std::size_t> findIndex(const EvaluationCacheKey& key) const noexcept;
    [[nodiscard]] std::uint64_t nextUse() noexcept;
    void enforceBudget() noexcept;
    void eraseIndex(std::size_t index) noexcept;

    std::size_t budgetBytes_ = 0;
    std::size_t retainedBytes_ = 0;
    std::uint64_t useSerial_ = 0;
    std::uint64_t hitCount_ = 0;
    std::uint64_t missCount_ = 0;
    std::uint64_t evictionCount_ = 0;
    std::vector<Entry> entries_;
};

} // namespace vortex
