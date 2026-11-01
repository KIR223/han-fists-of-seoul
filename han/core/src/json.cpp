#include "han/core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace han::core {
namespace {
const Json kNull;
const std::string kEmpty;
constexpr int kMaxDepth = 128;

struct Parser {
    std::string_view text;
    std::size_t pos = 0;
    std::string error;

    bool fail(const std::string& why) {
        int line = 1, col = 1;
        for (std::size_t i = 0; i < pos && i < text.size(); ++i) {
            if (text[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        error = "JSON: " + why + " (строка " + std::to_string(line) + ", столбец " + std::to_string(col) + ")";
        return false;
    }
    void skip() {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) ++pos;
    }
    bool literal(std::string_view word) {
        if (text.compare(pos, word.size(), word) == 0) {
            pos += word.size();
            return true;
        }
        return false;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool hex4(unsigned& out) {
        if (pos + 4 > text.size()) return fail("обрыв в \\u-последовательности");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else {
                --pos;
                return fail("в \\u нужны 4 шестнадцатеричные цифры");
            }
        }
        return true;
    }
    bool parseString(std::string& out) {
        ++pos;  // открывающая кавычка
        out.clear();
        while (true) {
            if (pos >= text.size()) return fail("не закрыта строка");
            const char c = text[pos++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) {
                --pos;
                return fail("управляющий символ в строке (нужен \\n или \\u)");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos >= text.size()) return fail("обрыв после обратной косой черты");
            const char e = text[pos++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // старшая часть суррогатной пары: за ней обязана идти младшая
                        unsigned low = 0;
                        if (text.compare(pos, 2, "\\u") != 0) return fail("одиночный суррогат");
                        pos += 2;
                        if (!hex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return fail("неверная пара суррогатов");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("одиночный суррогат");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    --pos;
                    return fail(std::string("неизвестная escape-последовательность \\") + e);
            }
        }
    }
    bool parseValue(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("слишком глубокая вложенность");
        skip();
        if (pos >= text.size()) return fail("ожидалось значение");
        const char c = text[pos];
        if (c == '{') {
            ++pos;
            out = Json::object();
            skip();
            if (pos < text.size() && text[pos] == '}') {
                ++pos;
                return true;
            }
            while (true) {
                skip();
                if (pos >= text.size() || text[pos] != '"') return fail("ожидался ключ в кавычках");
                std::string key;
                if (!parseString(key)) return false;
                skip();
                if (pos >= text.size() || text[pos] != ':') return fail("ожидалось «:»");
                ++pos;
                Json value;
                if (!parseValue(value, depth + 1)) return false;
                if (out.has(key)) return fail("повторяющийся ключ «" + key + "»");
                out.set(std::move(key), std::move(value));
                skip();
                if (pos < text.size() && text[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == '}') {
                    ++pos;
                    return true;
                }
                return fail("ожидалось «,» или «}»");
            }
        }
        if (c == '[') {
            ++pos;
            out = Json::array();
            skip();
            if (pos < text.size() && text[pos] == ']') {
                ++pos;
                return true;
            }
            while (true) {
                Json value;
                if (!parseValue(value, depth + 1)) return false;
                out.push(std::move(value));
                skip();
                if (pos < text.size() && text[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == ']') {
                    ++pos;
                    return true;
                }
                return fail("ожидалось «,» или «]»");
            }
        }
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = Json::string(std::move(s));
            return true;
        }
        if (literal("true")) {
            out = Json::boolean(true);
            return true;
        }
        if (literal("false")) {
            out = Json::boolean(false);
            return true;
        }
        if (literal("null")) {
            out = Json();
            return true;
        }
        // Число: -?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?
        const std::size_t start = pos;
        if (pos < text.size() && text[pos] == '-') ++pos;
        if (pos >= text.size() || !(text[pos] >= '0' && text[pos] <= '9')) {
            pos = start;
            return fail("ожидалось значение");
        }
        if (text[pos] == '0') {
            ++pos;
            if (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') return fail("ведущие нули в числе недопустимы");
        } else {
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
        }
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            if (pos >= text.size() || !(text[pos] >= '0' && text[pos] <= '9')) return fail("после точки нужны цифры");
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
            if (pos >= text.size() || !(text[pos] >= '0' && text[pos] <= '9')) return fail("в порядке числа нужны цифры");
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
        }
        const double v = std::strtod(std::string(text.substr(start, pos - start)).c_str(), nullptr);
        if (!std::isfinite(v)) return fail("число вне диапазона");
        out = Json::number(v);
        return true;
    }
};

void escapeTo(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}
}  // namespace

Json Json::boolean(bool v) {
    Json j;
    j.type_ = Type::Bool;
    j.bool_ = v;
    return j;
}
Json Json::number(double v) {
    Json j;
    j.type_ = Type::Number;
    j.number_ = v;
    return j;
}
Json Json::string(std::string v) {
    Json j;
    j.type_ = Type::String;
    j.string_ = std::move(v);
    return j;
}
Json Json::array() {
    Json j;
    j.type_ = Type::Array;
    return j;
}
Json Json::object() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

const std::string& Json::asString() const { return isString() ? string_ : kEmpty; }
std::size_t Json::size() const { return isArray() ? array_.size() : (isObject() ? object_.size() : 0); }
const Json& Json::at(std::size_t index) const { return isArray() && index < array_.size() ? array_[index] : kNull; }
const Json& Json::operator[](std::string_view key) const {
    if (!isObject()) return kNull;
    const auto it = object_.find(key);
    return it == object_.end() ? kNull : it->second;
}
bool Json::has(std::string_view key) const { return isObject() && object_.find(key) != object_.end(); }
void Json::push(Json v) {
    if (type_ != Type::Array) *this = Json::array();
    array_.push_back(std::move(v));
}
void Json::set(std::string key, Json v) {
    if (type_ != Type::Object) *this = Json::object();
    object_[std::move(key)] = std::move(v);
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    const auto newline = [&](int d) {
        if (indent < 0) return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * d), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[40];
            if (number_ == std::floor(number_) && std::fabs(number_) < 1e15) std::snprintf(buf, sizeof(buf), "%.0f", number_);
            else std::snprintf(buf, sizeof(buf), "%.17g", number_);
            out += buf;
            break;
        }
        case Type::String: escapeTo(out, string_); break;
        case Type::Array:
            out.push_back('[');
            for (std::size_t i = 0; i < array_.size(); ++i) {
                if (i) out.push_back(',');
                newline(depth + 1);
                array_[i].dumpTo(out, indent, depth + 1);
            }
            if (!array_.empty()) newline(depth);
            out.push_back(']');
            break;
        case Type::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& [k, v] : object_) {
                if (!first) out.push_back(',');
                first = false;
                newline(depth + 1);
                escapeTo(out, k);
                out += indent < 0 ? ":" : ": ";
                v.dumpTo(out, indent, depth + 1);
            }
            if (!object_.empty()) newline(depth);
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

bool parseJson(std::string_view text, Json& out, std::string& error) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);  // BOM, который любят ставить редакторы Windows
    Parser p{text};
    Json value;
    if (!p.parseValue(value, 0)) {
        error = p.error;
        return false;
    }
    p.skip();
    if (p.pos != text.size()) {
        p.fail("лишние данные после значения");
        error = p.error;
        return false;
    }
    out = std::move(value);
    return true;
}

bool loadJsonFile(const std::string& path, Json& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "не удалось открыть " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!parseJson(buffer.str(), out, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}
}  // namespace han::core
