#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace nc {

class Json {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Json() = default;

    static Json parse(std::string_view text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    float asFloat(float fallback = 0.0f) const;
    int asInt(int fallback = 0) const;
    const std::string& asString() const;

    std::size_t size() const;
    bool contains(std::string_view key) const;
    const Json& operator[](std::string_view key) const;
    const Json& at(std::size_t index) const;
    const std::map<std::string, Json>& object() const;
    const std::vector<Json>& array() const;

    static const Json& null();

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;

    friend class JsonParser;
};

}
