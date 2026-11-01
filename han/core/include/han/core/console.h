#pragma once
// Консоль (E1): реестр команд "имя арг1 арг2 ..."; вывод в окно/ImGui подключается позже через историю.
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace han::core {

class Console {
public:
    // Команда получает аргументы (без имени) и возвращает текст ответа.
    using Command = std::function<std::string(const std::vector<std::string>& args)>;

    void registerCommand(std::string name, std::string help, Command fn);
    // Выполняет строку; неизвестная команда даёт сообщение об ошибке, пустая строка — пустой ответ.
    std::string execute(std::string_view line);
    std::vector<std::string> history() const { return history_; }
    std::vector<std::string> commandNames() const;

    static std::vector<std::string> tokenize(std::string_view line);  // поддерживает "аргументы в кавычках"

private:
    struct Entry {
        std::string help;
        Command fn;
    };
    std::map<std::string, Entry> commands_;
    std::vector<std::string> history_;
};

}  // namespace han::core
// Пример: console.registerCommand("quit", "выход", [](auto&) { return "bye"; }); console.execute("quit");
