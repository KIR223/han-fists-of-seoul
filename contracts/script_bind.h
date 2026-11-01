// contracts/script_bind.h — поставщик: B (han/script), потребители: A.
// Зачем: A регистрирует функции и события своих систем в Lua, ничего не зная о sol2.
#pragma once
#include <functional>
#include <string_view>

namespace han::contracts {
struct IScriptBinder {
    // Функция видна в Lua как han.<ns>.<name>; аргументы и результат — числа.
    virtual void bindFunction(std::string_view ns, std::string_view name, std::function<double(double, double)> fn) = 0;
    virtual void bindEvent(std::string_view name) = 0;  // событие, на которое можно подписаться из Lua
    virtual void fireEvent(std::string_view name, double arg) = 0;
    virtual ~IScriptBinder() = default;
};
}  // namespace han::contracts
// Пример: binder.bindFunction("world", "time_of_day", [](double, double) { return 21.5; });
