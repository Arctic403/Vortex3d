#pragma once

#include "vortex/mesh/attribute.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vortex {

enum class PropertyType : std::uint8_t {
    Bool,
    Int64,
    Float,
    String,
    Vec3,
};

using PropertyValue = std::variant<bool, std::int64_t, float, std::string, Vec3>;

struct PropertyDefinition final {
    std::string name;
    PropertyType type = PropertyType::Float;
    PropertyValue defaultValue = 0.0F;
};

class PropertySchema final {
public:
    [[nodiscard]] bool add(PropertyDefinition definition) {
        if (definition.name.empty() || definitions_.contains(definition.name)) {
            return false;
        }
        definitions_.emplace(definition.name, std::move(definition));
        return true;
    }

    [[nodiscard]] const PropertyDefinition* find(std::string_view name) const noexcept {
        const auto it = definitions_.find(std::string{name});
        return it == definitions_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }

private:
    std::unordered_map<std::string, PropertyDefinition> definitions_;
};

class PropertyBag final {
public:
    explicit PropertyBag(const PropertySchema* schema = nullptr) noexcept : schema_(schema) {}

    [[nodiscard]] bool set(std::string name, PropertyValue value) {
        if (schema_ != nullptr) {
            const PropertyDefinition* definition = schema_->find(name);
            if (definition == nullptr || definition->defaultValue.index() != value.index()) {
                return false;
            }
        }
        values_[std::move(name)] = std::move(value);
        return true;
    }

    [[nodiscard]] const PropertyValue* get(std::string_view name) const noexcept {
        const auto it = values_.find(std::string{name});
        if (it != values_.end()) {
            return &it->second;
        }
        if (schema_ != nullptr) {
            const PropertyDefinition* definition = schema_->find(name);
            return definition == nullptr ? nullptr : &definition->defaultValue;
        }
        return nullptr;
    }

    [[nodiscard]] const PropertySchema* schema() const noexcept { return schema_; }

private:
    const PropertySchema* schema_ = nullptr;
    std::unordered_map<std::string, PropertyValue> values_;
};

} // namespace vortex
