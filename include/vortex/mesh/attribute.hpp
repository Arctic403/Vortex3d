#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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
    std::string name;
    AttributeDomain domain = AttributeDomain::Vertex;
    AttributeType type = AttributeType::Float;
    AttributeScalar defaultValue = 0.0F;
    AttributeStorage storage = std::vector<float>{};
};

class AttributeSet final {
public:
    template <typename T>
    [[nodiscard]] bool create(std::string name, const AttributeDomain domain, const T defaultValue = T{}) {
        static_assert(
            std::is_same_v<T, bool> || std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> ||
            std::is_same_v<T, float> || std::is_same_v<T, Vec2> || std::is_same_v<T, Vec3> || std::is_same_v<T, Vec4>,
            "Unsupported Vortex3D attribute type");

        if (layers_.contains(name)) {
            return false;
        }

        const std::size_t size = domainSizes_[domainIndex(domain)];
        AttributeLayer layer;
        layer.name = std::move(name);
        layer.domain = domain;
        layer.type = AttributeTypeOf<T>::value;
        layer.defaultValue = defaultValue;
        layer.storage = std::vector<T>(size, defaultValue);
        layers_.emplace(layer.name, std::move(layer));
        return true;
    }

    [[nodiscard]] bool contains(const std::string& name) const noexcept { return layers_.contains(name); }

    [[nodiscard]] const AttributeLayer* layer(const std::string& name) const noexcept {
        const auto it = layers_.find(name);
        return it == layers_.end() ? nullptr : &it->second;
    }

    template <typename T>
    [[nodiscard]] std::vector<T>* values(const std::string& name) noexcept {
        const auto it = layers_.find(name);
        if (it == layers_.end() || it->second.type != AttributeTypeOf<T>::value) {
            return nullptr;
        }
        return std::get_if<std::vector<T>>(&it->second.storage);
    }

    template <typename T>
    [[nodiscard]] const std::vector<T>* values(const std::string& name) const noexcept {
        const auto it = layers_.find(name);
        if (it == layers_.end() || it->second.type != AttributeTypeOf<T>::value) {
            return nullptr;
        }
        return std::get_if<std::vector<T>>(&it->second.storage);
    }

    void setDomainSize(const AttributeDomain domain, const std::size_t size) {
        domainSizes_[domainIndex(domain)] = size;
        for (auto& [name, layer] : layers_) {
            (void)name;
            if (layer.domain != domain) {
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

    [[nodiscard]] std::size_t domainSize(const AttributeDomain domain) const noexcept {
        return domainSizes_[domainIndex(domain)];
    }

    [[nodiscard]] bool validateSizes() const noexcept {
        for (const auto& [name, layer] : layers_) {
            (void)name;
            const std::size_t expected = domainSize(layer.domain);
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
    std::unordered_map<std::string, AttributeLayer> layers_;
};

} // namespace vortex
