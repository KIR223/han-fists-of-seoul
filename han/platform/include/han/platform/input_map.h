#pragma once
// Карта действий (E1): игра слушает действия, а не клавиши; привязки меняются игроком и сохраняются в текст.
// Не зависит от SDL: коды клавиш — просто числа (для SDL3 это SDL_Scancode). Основа для contracts/input_api.h.
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace han::platform {

class InputMap {
public:
    using Scancode = int;

    // Привязки. У одного действия может быть несколько клавиш.
    void bind(const std::string& action, Scancode key);
    void unbind(const std::string& action, Scancode key);
    void clearBindings(const std::string& action);
    // Переназначение: вся прежняя привязка действия заменяется одной клавишей.
    // Если клавиша уже занята другим действием, оно теряет её и возвращается имя этого действия ("" — конфликта нет).
    std::string rebind(const std::string& action, Scancode key);
    std::vector<Scancode> bindings(const std::string& action) const;

    // Кадр ввода: beginFrame сбрасывает «нажато/отпущено в этом кадре», затем приходят события.
    void beginFrame(double nowSeconds);
    void onKey(Scancode key, bool down);

    bool pressed(const std::string& action) const;
    bool held(const std::string& action) const;
    bool released(const std::string& action) const;
    // Секунд с последнего нажатия действия (буфер ввода боя); -1 — не нажималось.
    double secondsSincePress(const std::string& action) const;

    // Текст "действие=код,код" по строке на действие; для файла настроек.
    std::string serialize() const;
    // Заменяет привязки; повреждённые строки пропускает. Возвращает число прочитанных действий.
    int deserialize(std::string_view text);

private:
    template <class Pred>
    bool anyKey(const std::string& action, Pred pred) const;

    std::map<std::string, std::vector<Scancode>> bindings_;
    std::set<Scancode> down_, pressedNow_, releasedNow_;
    std::map<Scancode, double> lastPress_;
    double now_ = 0;
};

}  // namespace han::platform
// Пример: map.bind("punch_light", 20); map.beginFrame(t); map.onKey(20, true); if (map.pressed("punch_light")) ...
