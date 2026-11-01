// han/core/json.h — небольшой разбор JSON для данных игры (E11 и далее): архетипы, настройки, таблицы.
// Строгий RFC 8259: без комментариев и висячих запятых; числа — double; \uXXXX (в том числе суррогатные пары) → UTF-8.
// Глубина вложенности ограничена (защита от переполнения стека на испорченных файлах).
#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace han::core {
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json boolean(bool v);
    static Json number(double v);
    static Json string(std::string v);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Значение или запасное, если тип другой.
    bool asBool(bool fallback = false) const { return isBool() ? bool_ : fallback; }
    double asNumber(double fallback = 0) const { return isNumber() ? number_ : fallback; }
    const std::string& asString() const;  // пустая строка, если не строка
    std::size_t size() const;             // элементов массива или ключей объекта
    const Json& at(std::size_t index) const;      // нулевое значение, если нет
    const Json& operator[](std::string_view key) const;  // нулевое значение, если нет
    bool has(std::string_view key) const;
    const std::vector<Json>& items() const { return array_; }
    const std::map<std::string, Json, std::less<>>& members() const { return object_; }

    void push(Json v);                   // в массив
    void set(std::string key, Json v);   // в объект

    // Текст JSON; indent < 0 — в одну строку.
    std::string dump(int indent = -1) const;

private:
    void dumpTo(std::string& out, int indent, int depth) const;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json, std::less<>> object_;
};

// false — ошибка разбора, в error — описание со строкой и столбцом.
bool parseJson(std::string_view text, Json& out, std::string& error);
bool loadJsonFile(const std::string& path, Json& out, std::string& error);
}  // namespace han::core
// Пример: Json j; std::string err; parseJson(R"({"age":[18,60]})", j, err); double lo = j["age"].at(0).asNumber();
