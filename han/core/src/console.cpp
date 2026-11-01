#include "han/core/console.h"

namespace han::core {

void Console::registerCommand(std::string name, std::string help, Command fn) {
    commands_[std::move(name)] = Entry{std::move(help), std::move(fn)};
}

std::vector<std::string> Console::tokenize(std::string_view line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false, hasToken = false;
    for (const char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
            hasToken = true;
        } else if ((c == ' ' || c == '\t') && !inQuotes) {
            if (hasToken) tokens.push_back(std::move(current));
            current.clear();
            hasToken = false;
        } else {
            current.push_back(c);
            hasToken = true;
        }
    }
    if (hasToken) tokens.push_back(std::move(current));
    return tokens;
}

std::string Console::execute(std::string_view line) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return {};
    history_.emplace_back(line);
    const std::string name = tokens.front();
    tokens.erase(tokens.begin());
    if (name == "help") {
        std::string out;
        for (const auto& [n, e] : commands_) out += n + " — " + e.help + "\n";
        return out;
    }
    const auto it = commands_.find(name);
    if (it == commands_.end()) return "неизвестная команда: " + name;
    return it->second.fn(tokens);
}

std::vector<std::string> Console::commandNames() const {
    std::vector<std::string> names;
    for (const auto& [n, e] : commands_) names.push_back(n);
    return names;
}

}  // namespace han::core
