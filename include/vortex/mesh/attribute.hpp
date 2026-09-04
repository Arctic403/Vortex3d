#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vortex {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;
    constexpr auto operator<=>(const Vec2&) const noexcept = default;
};

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    constexpr auto operator<=>(const Vec3&) const noexcept = default;
};

struct Vec4 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
    constexpr auto operator<=>(const Vec4&) const noexcept = default;
};

enum class AttributeDomain : std::uint8_t {
    Vertex = 0,
    Edge = 1,
    Face = 2,
    Corner = 3,
};

enum class AttributeType : std::uint8_t {
    Bool,
    Int32,
    UInt32,
    Float,
    Vec2,
    Vec3,
    Vec4,
};

struct AttributeKey final {
    std::string name;
    AttributeDomain domain = AttributeDomain::Vertex;

    [[nodiscard]] bool operator==(const AttributeKey&) const noexcept = default;
};

struct AttributeKeyHash final {
    [[nodiscard]] std::size_t operator()(const AttributeKey& key) const noexcept {
        const std::size_t nameHash = std::hash<std::string>{}(key.name);
        const std::size_t domainHash = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.domain));
        return nameHash ^ (domainHash + 0x9e3779b9U + (nameHash << 6U) + (nameHash >> 2U));
    }
};

template <typename T>
struct AttributeTypeOf;

template <> struct AttributeTypeOf<bool> { static constexpr AttributeType value = AttributeType::Bool; };
template <> struct AttributeTypeOf<std::int32_t> { static constexpr AttributeType value = AttributeType::Int32; };
template <> struct AttributeTypeOf<std::uint32_t> { static constexpr AttributeType value = AttributeType::UInt32; };
template <> struct AttributeTypeOf<float> { static constexpr AttributeType value = AttributeType::Float; };
template <> struct AttributeTypeOf<Vec2> { static constexpr AttributeType value = AttributeType::Vec2; };
template <> struct AttributeTypeOf<Vec3> { static constexpr AttributeType value = AttributeType::Vec3; };
template <> struct AttributeTypeOf<Vec4> { static constexpr AttributeType value = AttributeType::Vec4; };

using AttributeScalar = std::variant<bool, std::int32_t, std::uint32_t, float, Vec2, Vec3, Vec4>;
using AttributeStorage = std::variant<
    std::vector<bool>,
    std::vector<std::int32_t>,
    std::vector<std::uint32_t>,
    std::vector<float>,
    std::vector<Vec2>,
    std::vector<Vec3>,
    std::vector<Vec4>>;

struct AttributeLayer final {
    AttributeKey key;
    AttributeType type = AttributeType::Float;
    AttributeScalar defaultValue = 0.0F;
    AttributeStorage storage = std::vector<float>{};
};

struct AttributeRowValue final {
    AttributeKey key;
    AttributeType type = AttributeType::Float;
    AttributeScalar value = 0.0F;
};

struct AttributeRow final {
    AttributeDomain domain = AttributeDomain::Vertex;
    std::vector<AttributeRowValue> values;
};

class AttributeSet final {
public:
    template <typename T>
    [[nodiscard]] bool create(std::string name, const AttributeDomain domain, const T defaultValue = T{}) {
        static_assert(
            std::is_same_v<T, bool> || std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> ||
            std::is_same_v<T, float> || std::is_same_v<T, Vec2> || std::is_same_v<T, Vec3> || std::is_same_v<T, Vec4>,
            "Unsupported Vortex3D attribute type");

        AttributeKey key{std::move(name), domain};
        if (layers_.contains(key)) {
            return false;
        }

        const std::size_t size = domainSizes_[domainIndex(domain)];
        AttributeLayer layer;
        layer.key = key;
        layer.type = AttributeTypeOf<T>::value;
        layer.defaultValue = defaultValue;
        layer.storage = std::vector<T>(size, defaultValue);
        layers_.emplace(std::move(key), std::move(layer));
        return true;
    }

    [[nodiscard]] bool contains(const std::string_view name, const AttributeDomain domain) const noexcept {
        return layers_.contains(AttributeKey{std::string{name}, domain});
    }

    [[nodiscard]] const AttributeLayer* layer(const std::string_view name, const AttributeDomain domain) const noexcept {
        const auto it = layers_.find(AttributeKey{std::string{name}, domain});
        return it == layers_.end() ? nullptr : &it->second;
    }

    template <typename T>
    [[nodiscard]] std::vector<T>* values(const std::string_view name, const AttributeDomain domain) noexcept {
        const auto it = layers_.find(AttributeKey{std::string{name}, domain});
        if (it == layers_.end() || it->second.type != AttributeTypeOf<T>::value) {
            return nullptr;
        }
        return std::get_if<std::vector<T>>(&it->second.storage);
    }

    template <typename T>
    [[nodiscard]] const std::vector<T>* values(const std::string_view name, const AttributeDomain domain) const noexcept {
        const auto it = layers_.find(AttributeKey{std::string{name}, domain});
        if (it == layers_.end() || it->second.type != AttributeTypeOf<T>::value) {
            return nullptr;
        }
        return std::get_if<std::vector<T>>(&it->second.storage);
    }

    void setDomainSize(const AttributeDomain domain, const std::size_t size) {
        domainSizes_[domainIndex(domain)] = size;
        for (auto& [key, layer] : layers_) {
            (void)key;
            if (layer.key.domain != domain) {
                continue;
            }
            std::visit([&](auto& storage) {
                using Vector = std::decay_t<decltype(storage)>;
                using Value = typename Vector::value_type;
                if (const auto* defaultValue = std::get_if<Value>(&layer.defaultValue)) {
                    storage.resize(size, *defaultValue);
                }
            }, layer.storage);
        }
    }

    [[nodiscard]] std::optional<AttributeRow> captureDomainIndex(
        const AttributeDomain domain,
        const std::size_t index) const {
        if (index >= domainSize(domain)) {
            return std::nullopt;
        }

        AttributeRow row;
        row.domain = domain;
        for (const auto& [key, layer] : layers_) {
            (void)key;
            if (layer.key.domain != domain) {
                continue;
            }

            AttributeRowValue rowValue;
            rowValue.key = layer.key;
            rowValue.type = layer.type;
            std::visit([&](const auto& storage) {
                using Vector = std::decay_t<decltype(storage)>;
                using Value = typename Vector::value_type;
                if constexpr (std::is_same_v<Value, bool>) {
                    rowValue.value = static_cast<bool>(storage[index]);
                } else {
                    rowValue.value = storage[index];
                }
            }, layer.storage);
            row.values.push_back(std::move(rowValue));
        }
        return row;
    }

    [[nodiscard]] bool insertDomainIndex(
        const AttributeDomain domain,
        const std::size_t index,
        const AttributeRow& row) {
        const std::size_t currentSize = domainSize(domain);
        if (row.domain != domain || index > currentSize) {
            return false;
        }

        for (auto& [key, layer] : layers_) {
            (void)key;
            if (layer.key.domain != domain) {
                continue;
            }

            const AttributeRowValue* stored = nullptr;
            for (const AttributeRowValue& candidate : row.values) {
                if (candidate.key == layer.key && candidate.type == layer.type) {
                    stored = &candidate;
                    break;
                }
            }

            std::visit([&](auto& storage) {
                using Vector = std::decay_t<decltype(storage)>;
                using Value = typename Vector::value_type;

                Value value{};
                if (const auto* defaultValue = std::get_if<Value>(&layer.defaultValue)) {
                    value = *defaultValue;
                }
                if (stored != nullptr) {
                    if (const auto* captured = std::get_if<Value>(&stored->value)) {
                        value = *captured;
                    }
                }
                storage.insert(storage.begin() + static_cast<std::ptrdiff_t>(index), value);
            }, layer.storage);
        }

        domainSizes_[domainIndex(domain)] = currentSize + 1;
        return true;
    }

    [[nodiscard]] bool appendDomainRow(const AttributeDomain domain, const AttributeRow& row) {
        return insertDomainIndex(domain, domainSize(domain), row);
    }

    [[nodiscard]] bool eraseDomainIndex(const AttributeDomain domain, const std::size_t index) {
        const std::size_t currentSize = domainSize(domain);
        if (index >= currentSize) {
            return false;
        }

        for (auto& [key, layer] : layers_) {
            (void)key;
            if (layer.key.domain != domain) {
                continue;
            }
            std::visit([index](auto& storage) {
                storage.erase(storage.begin() + static_cast<std::ptrdiff_t>(index));
            }, layer.storage);
        }

        domainSizes_[domainIndex(domain)] = currentSize - 1;
        return true;
    }

    [[nodiscard]] bool copyDomainIndex(
        const AttributeDomain domain,
        const std::size_t sourceIndex,
        const std::size_t destinationIndex) {
        const std::size_t size = domainSize(domain);
        if (sourceIndex >= size || destinationIndex >= size) {
            return false;
        }

        for (auto& [key, layer] : layers_) {
            (void)key;
            if (layer.key.domain != domain) {
                continue;
            }
            std::visit([sourceIndex, destinationIndex](auto& storage) {
                storage[destinationIndex] = storage[sourceIndex];
            }, layer.storage);
        }
        return true;
    }

    [[nodiscard]] bool interpolateDomainIndex(
        const AttributeDomain domain,
        const std::size_t firstIndex,
        const std::size_t secondIndex,
        const std::size_t destinationIndex,
        const float factor) {
        const std::size_t size = domainSize(domain);
        if (firstIndex >= size || secondIndex >= size || destinationIndex >= size) {
            return false;
        }

        const float t = std::clamp(factor, 0.0F, 1.0F);
        for (auto& [key, layer] : layers_) {
            (void)key;
            if (layer.key.domain != domain) {
                continue;
            }

            std::visit([firstIndex, secondIndex, destinationIndex, t](auto& storage) {
                using Vector = std::decay_t<decltype(storage)>;
                using Value = typename Vector::value_type;

                if constexpr (std::is_same_v<Value, bool> || std::is_integral_v<Value>) {
                    storage[destinationIndex] = t < 0.5F ? storage[firstIndex] : storage[secondIndex];
                } else if constexpr (std::is_same_v<Value, float>) {
                    storage[destinationIndex] = storage[firstIndex] + (storage[secondIndex] - storage[firstIndex]) * t;
                } else if constexpr (std::is_same_v<Value, Vec2>) {
                    const Vec2 a = storage[firstIndex];
                    const Vec2 b = storage[secondIndex];
                    storage[destinationIndex] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
                } else if constexpr (std::is_same_v<Value, Vec3>) {
                    const Vec3 a = storage[firstIndex];
                    const Vec3 b = storage[secondIndex];
                    storage[destinationIndex] = {
                        a.x + (b.x - a.x) * t,
                        a.y + (b.y - a.y) * t,
                        a.z + (b.z - a.z) * t};
                } else if constexpr (std::is_same_v<Value, Vec4>) {
                    const Vec4 a = storage[firstIndex];
                    const Vec4 b = storage[secondIndex];
                    storage[destinationIndex] = {
                        a.x + (b.x - a.x) * t,
                        a.y + (b.y - a.y) * t,
                        a.z + (b.z - a.z) * t,
                        a.w + (b.w - a.w) * t};
                }
            }, layer.storage);
        }
        return true;
    }

    [[nodiscard]] std::size_t domainSize(const AttributeDomain domain) const noexcept {
        return domainSizes_[domainIndex(domain)];
    }

    [[nodiscard]] bool validateSizes() const noexcept {
        for (const auto& [key, layer] : layers_) {
            (void)key;
            const std::size_t expected = domainSize(layer.key.domain);
            const bool matches = std::visit([expected](const auto& storage) { return storage.size() == expected; }, layer.storage);
            if (!matches) {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] static constexpr std::size_t domainIndex(const AttributeDomain domain) noexcept {
        return static_cast<std::size_t>(domain);
    }

    std::array<std::size_t, 4> domainSizes_{};
    std::unordered_map<AttributeKey, AttributeLayer, AttributeKeyHash> layers_;
};

} // namespace vortex
