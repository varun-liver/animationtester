// Minimal recursive-descent JSON parser, scoped to exactly what the
// animationlib animation-JSON schema needs (objects, arrays, numbers,
// strings, bool/null). Not a general-purpose JSON library.
#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    // Preserves insertion order (needed since bone/keyframe-time maps are
    // read back in document order for interpolation).
    std::vector<std::pair<std::string, JsonValue>> objectValue;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isNumber() const { return type == Type::Number; }

    const JsonValue* find(const std::string& key) const {
        for (auto& kv : objectValue) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    double asNumber(double fallback = 0.0) const {
        return type == Type::Number ? numberValue : fallback;
    }

    bool asBool(bool fallback = false) const {
        return type == Type::Bool ? boolValue : fallback;
    }

    const std::string& asString() const { return stringValue; }
};

namespace json_mini_detail {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue v = parseValue();
        skipWhitespace();
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    char peek() const {
        if (pos_ >= s_.size()) throw std::runtime_error("json_mini: unexpected end of input");
        return s_[pos_];
    }

    char next() { return s_[pos_++]; }

    void skipWhitespace() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/') {
                while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
            } else {
                break;
            }
        }
    }

    void expect(char c) {
        if (pos_ >= s_.size() || s_[pos_] != c) {
            throw std::runtime_error(std::string("json_mini: expected '") + c + "' at offset " + std::to_string(pos_));
        }
        ++pos_;
    }

    JsonValue parseValue() {
        skipWhitespace();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        return parseNumber();
    }

    JsonValue parseObject() {
        JsonValue v;
        v.type = JsonValue::Type::Object;
        expect('{');
        skipWhitespace();
        if (peek() == '}') { ++pos_; return v; }
        while (true) {
            skipWhitespace();
            JsonValue key = parseString();
            skipWhitespace();
            expect(':');
            JsonValue val = parseValue();
            v.objectValue.emplace_back(key.stringValue, std::move(val));
            skipWhitespace();
            char c = next();
            if (c == ',') continue;
            if (c == '}') break;
            throw std::runtime_error("json_mini: expected ',' or '}' in object");
        }
        return v;
    }

    JsonValue parseArray() {
        JsonValue v;
        v.type = JsonValue::Type::Array;
        expect('[');
        skipWhitespace();
        if (peek() == ']') { ++pos_; return v; }
        while (true) {
            v.arrayValue.push_back(parseValue());
            skipWhitespace();
            char c = next();
            if (c == ',') continue;
            if (c == ']') break;
            throw std::runtime_error("json_mini: expected ',' or ']' in array");
        }
        return v;
    }

    JsonValue parseString() {
        JsonValue v;
        v.type = JsonValue::Type::String;
        expect('"');
        std::string out;
        while (true) {
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                char esc = next();
                switch (esc) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': pos_ += 4; out += '?'; break; // unicode escapes unused by this schema
                    default: out += esc; break;
                }
            } else {
                out += c;
            }
        }
        v.stringValue = std::move(out);
        return v;
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-' || peek() == '+') ++pos_;
        while (pos_ < s_.size() && (isdigit((unsigned char)s_[pos_]) || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '-' || s_[pos_] == '+')) {
            ++pos_;
        }
        JsonValue v;
        v.type = JsonValue::Type::Number;
        v.numberValue = std::stod(s_.substr(start, pos_ - start));
        return v;
    }

    JsonValue parseBool() {
        JsonValue v;
        v.type = JsonValue::Type::Bool;
        if (s_.compare(pos_, 4, "true") == 0) { v.boolValue = true; pos_ += 4; }
        else if (s_.compare(pos_, 5, "false") == 0) { v.boolValue = false; pos_ += 5; }
        else throw std::runtime_error("json_mini: invalid literal at offset " + std::to_string(pos_));
        return v;
    }

    JsonValue parseNull() {
        if (s_.compare(pos_, 4, "null") != 0) throw std::runtime_error("json_mini: invalid literal at offset " + std::to_string(pos_));
        pos_ += 4;
        JsonValue v;
        v.type = JsonValue::Type::Null;
        return v;
    }
};

} // namespace json_mini_detail

inline JsonValue parseJson(const std::string& text) {
    json_mini_detail::Parser parser(text);
    return parser.parse();
}
