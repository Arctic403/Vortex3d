#include "vortex/eval/evaluation_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace vortex {

std::optional<std::size_t> EvaluationCache::findIndex(const EvaluationCacheKey& key) const noexcept {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

std::uint64_t EvaluationCache::nextUse() noexcept {
    if (useSerial_ == std::numeric_limits<std::uint64_t>::max()) {
        // A 64-bit wrap is not practically reachable in an editor session. Resetting the
        // old entries to one deterministic age bucket keeps behavior defined if it occurs.
        for (Entry& entry : entries_) {
            entry.lastUse = 0;
        }
        useSerial_ = 0;
    }
    ++useSerial_;
    return useSerial_;
}

void EvaluationCache::eraseIndex(const std::size_t index) noexcept {
    if (index >= entries_.size()) {
        return;
    }

    const std::size_t bytes = entries_[index].retainedBytes;
    retainedBytes_ = bytes <= retainedBytes_ ? retainedBytes_ - bytes : 0;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
}

void EvaluationCache::enforceBudget() noexcept {
    while (retainedBytes_ > budgetBytes_ && !entries_.empty()) {
        std::size_t oldestIndex = 0;
        for (std::size_t index = 1; index < entries_.size(); ++index) {
            if (entries_[index].lastUse < entries_[oldestIndex].lastUse) {
                oldestIndex = index;
            }
        }
        eraseIndex(oldestIndex);
        ++evictionCount_;
    }
}

CachedEvaluationResult EvaluationCache::evaluate(
    const MeshBlock& source,
    const std::span<const MeshModifier* const> modifiers) {
    CachedEvaluationResult result;

    const MeshEvaluationKeyResult keyResult = MeshEvaluator::cacheKeyFor(source, modifiers);
    if (!keyResult || !keyResult.key.has_value()) {
        result.error = keyResult.error;
        result.modifierIndex = keyResult.modifierIndex;
        return result;
    }
    const EvaluationCacheKey key = keyResult.key.value();

    if (const auto existingIndex = findIndex(key)) {
        Entry& entry = entries_[*existingIndex];
        entry.lastUse = nextUse();
        ++hitCount_;
        result.mesh = entry.mesh;
        result.cacheHit = true;
        result.retainedByCache = true;
        return result;
    }

    ++missCount_;
    MeshEvaluationResult evaluated = MeshEvaluator::evaluate(source, modifiers);
    if (!evaluated || !evaluated.mesh.has_value()) {
        result.error = evaluated.error;
        result.modifierError = evaluated.modifierError;
        result.normalError = evaluated.normalError;
        result.modifierIndex = evaluated.modifierIndex;
        return result;
    }

    auto snapshot = std::make_shared<EvaluatedMesh>(std::move(evaluated.mesh.value()));
    result.mesh = snapshot;

    const std::size_t bytes = snapshot->estimatedRetainedBytes();
    if (budgetBytes_ == 0U || bytes > budgetBytes_) {
        return result;
    }

    while (!entries_.empty() && retainedBytes_ > budgetBytes_ - bytes) {
        std::size_t oldestIndex = 0;
        for (std::size_t index = 1; index < entries_.size(); ++index) {
            if (entries_[index].lastUse < entries_[oldestIndex].lastUse) {
                oldestIndex = index;
            }
        }
        eraseIndex(oldestIndex);
        ++evictionCount_;
    }

    entries_.push_back(Entry{key, snapshot, bytes, nextUse()});
    retainedBytes_ += bytes;
    result.retainedByCache = true;
    return result;
}

void EvaluationCache::clear() noexcept {
    entries_.clear();
    retainedBytes_ = 0;
}

void EvaluationCache::eraseMesh(
    const RuntimeDocumentId sourceDocumentRuntimeId,
    const MeshId sourceMeshId) noexcept {
    std::size_t index = 0;
    while (index < entries_.size()) {
        if (entries_[index].key.sourceDocumentRuntimeId == sourceDocumentRuntimeId &&
            entries_[index].key.sourceMeshId == sourceMeshId) {
            eraseIndex(index);
            continue;
        }
        ++index;
    }
}

void EvaluationCache::setBudgetBytes(const std::size_t budgetBytes) noexcept {
    budgetBytes_ = budgetBytes;
    enforceBudget();
}

} // namespace vortex
