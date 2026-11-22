#include "util/Json.hpp"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

namespace nc {
namespace {

void appendUtf8(std::string& out, std::uint32_t codePoint) {
    if (codePoint <= 0x7Fu) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (codePoint >> 6u)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (codePoint >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (codePoint >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
}

}

class JsonParser {
public:
    explicit JsonParser(std::string_view text)
        : text_(text) {}

    Json parseDocument() {
        skipWhitespace();
        Json value = parseValue();
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("trailing characters after JSON value");
        }
        return value;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    int depth_ = 0;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("JSON error at byte " + std::to_string(pos_) + ": " + message);
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    char take() {
        if (atEnd()) {
            fail("unexpected end of input");
        }
        return text_[pos_++];
    }

    void skipWhitespace() {
        while (!atEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    void expectLiteral(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) != literal) {
            fail("expected " + std::string(literal));
        }
        pos_ += literal.size();
    }

    Json parseValue() {
        if (++depth_ > 256) {
            fail("nesting too deep");
        }
        skipWhitespace();
        if (atEnd()) {
            fail("unexpected end of input");
        }

        Json result;
        const char c = peek();
        if (c == '{') {
            result = parseObject();
        } else if (c == '[') {
            result = parseArray();
        } else if (c == '"') {
            result.type_ = Json::Type::String;
            result.string_ = parseString();
        } else if (c == 't') {
            expectLiteral("true");
            result.type_ = Json::Type::Bool;
            result.bool_ = true;
        } else if (c == 'f') {
            expectLiteral("false");
            result.type_ = Json::Type::Bool;
            result.bool_ = false;
        } else if (c == 'n') {
            expectLiteral("null");
            result.type_ = Json::Type::Null;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            result.type_ = Json::Type::Number;
            result.number_ = parseNumber();
        } else {
            fail(std::string("unexpected character '") + c + "'");
        }
        --depth_;
        return result;
    }

    Json parseObject() {
        Json result;
        result.type_ = Json::Type::Object;
        take();
        skipWhitespace();
        if (peek() == '}') {
            take();
            return result;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                fail("expected object key string");
            }
            std::string key = parseString();
            skipWhitespace();
            if (take() != ':') {
                fail("expected ':' after object key");
            }
            Json value = parseValue();
            result.object_[std::move(key)] = std::move(value);
            skipWhitespace();
            const char c = take();
            if (c == ',') {
                continue;
            }
            if (c == '}') {
                break;
            }
            fail("expected ',' or '}' in object");
        }
        return result;
    }

    Json parseArray() {
        Json result;
        result.type_ = Json::Type::Array;
        take();
        skipWhitespace();
        if (peek() == ']') {
            take();
            return result;
        }
        while (true) {
            result.array_.push_back(parseValue());
            skipWhitespace();
            const char c = take();
            if (c == ',') {
                continue;
            }
            if (c == ']') {
                break;
            }
            fail("expected ',' or ']' in array");
        }
        return result;
    }

    std::uint32_t parseHex4() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = take();
            value <<= 4u;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("invalid \\u escape");
            }
        }
        return value;
    }

    std::string parseString() {
        take();
        std::string out;
        while (true) {
            if (atEnd()) {
                fail("unterminated string");
            }
            const char c = take();
            if (c == '"') {
                break;
            }
            if (c == '\\') {
                const char escape = take();
                switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codePoint = parseHex4();
                    if (codePoint >= 0xD800u && codePoint <= 0xDBFFu) {
                        if (take() != '\\' || take() != 'u') {
                            fail("expected low surrogate");
                        }
                        const std::uint32_t low = parseHex4();
                        if (low < 0xDC00u || low > 0xDFFFu) {
                            fail("invalid low surrogate");
                        }
                        codePoint = 0x10000u + ((codePoint - 0xD800u) << 10u) + (low - 0xDC00u);
                    }
                    appendUtf8(out, codePoint);
                    break;
                }
                default:
                    fail("invalid escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    double parseNumber() {
        const std::size_t start = pos_;
        while (!atEnd()) {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e'
                || c == 'E') {
                ++pos_;
            } else {
                break;
            }
        }
        const std::string token(text_.substr(start, pos_ - start));
        errno = 0;
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == token.c_str() || *end != '\0' || errno == ERANGE) {
            fail("invalid number '" + token + "'");
        }
        return value;
    }
};

Json Json::parse(std::string_view text) {
    JsonParser parser(text);
    return parser.parseDocument();
}

const Json& Json::null() {
    static const Json value;
    return value;
}

bool Json::asBool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

double Json::asNumber(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

float Json::asFloat(float fallback) const {
    return type_ == Type::Number ? static_cast<float>(number_) : fallback;
}

int Json::asInt(int fallback) const {
    return type_ == Type::Number ? static_cast<int>(number_) : fallback;
}

const std::string& Json::asString() const {
    static const std::string empty;
    return type_ == Type::String ? string_ : empty;
}

std::size_t Json::size() const {
    if (type_ == Type::Array) {
        return array_.size();
    }
    if (type_ == Type::Object) {
        return object_.size();
    }
    return 0;
}

bool Json::contains(std::string_view key) const {
    return type_ == Type::Object && object_.find(std::string(key)) != object_.end();
}

const Json& Json::operator[](std::string_view key) const {
    if (type_ != Type::Object) {
        return null();
    }
    const auto it = object_.find(std::string(key));
    return it == object_.end() ? null() : it->second;
}

const Json& Json::at(std::size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) {
        return null();
    }
    return array_[index];
}

const std::map<std::string, Json>& Json::object() const {
    static const std::map<std::string, Json> empty;
    return type_ == Type::Object ? object_ : empty;
}

const std::vector<Json>& Json::array() const {
    static const std::vector<Json> empty;
    return type_ == Type::Array ? array_ : empty;
}

}
