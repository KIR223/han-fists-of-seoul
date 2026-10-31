#include "han/platform/input_map.h"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace han::platform {

void InputMap::bind(const std::string& action, Scancode key) {
    auto& keys = bindings_[action];
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
}

void InputMap::unbind(const std::string& action, Scancode key) {
    const auto it = bindings_.find(action);
    if (it == bindings_.end()) return;
    auto& keys = it->second;
    keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
}

void InputMap::clearBindings(const std::string& action) { bindings_.erase(action); }

std::string InputMap::rebind(const std::string& action, Scancode key) {
    std::string conflict;
    for (auto& [other, keys] : bindings_) {
        if (other == action) continue;
        const auto before = keys.size();
        keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        if (keys.size() != before) conflict = other;
    }
    bindings_[action] = {key};
    return conflict;
}

std::vector<InputMap::Scancode> InputMap::bindings(const std::string& action) const {
    const auto it = bindings_.find(action);
    return it == bindings_.end() ? std::vector<Scancode>{} : it->second;
}

void InputMap::beginFrame(double nowSeconds) {
    now_ = nowSeconds;
    pressedNow_.clear();
    releasedNow_.clear();
}

void InputMap::onKey(Scancode key, bool down) {
    if (down) {
        if (down_.insert(key).second) {  // автоповтор клавиши не считается новым нажатием
            pressedNow_.insert(key);
            lastPress_[key] = now_;
        }
    } else if (down_.erase(key) != 0) {
        releasedNow_.insert(key);
    }
}

template <class Pred>
bool InputMap::anyKey(const std::string& action, Pred pred) const {
    const auto it = bindings_.find(action);
    if (it == bindings_.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(), pred);
}

bool InputMap::pressed(const std::string& action) const {
    return anyKey(action, [this](Scancode k) { return pressedNow_.count(k) != 0; });
}
bool InputMap::held(const std::string& action) const {
    return anyKey(action, [this](Scancode k) { return down_.count(k) != 0; });
}
bool InputMap::released(const std::string& action) const {
    return anyKey(action, [this](Scancode k) { return releasedNow_.count(k) != 0; });
}

double InputMap::secondsSincePress(const std::string& action) const {
    const auto it = bindings_.find(action);
    if (it == bindings_.end()) return -1;
    double latest = -1;
    for (const Scancode k : it->second) {
        const auto p = lastPress_.find(k);
        if (p != lastPress_.end()) latest = std::max(latest, p->second);
    }
    return latest < 0 ? -1 : now_ - latest;
}

std::string InputMap::serialize() const {
    std::ostringstream out;
    for (const auto& [action, keys] : bindings_) {
        out << action << '=';
        for (std::size_t i = 0; i < keys.size(); ++i) out << (i ? "," : "") << keys[i];
        out << '\n';
    }
    return out.str();
}

int InputMap::deserialize(std::string_view text) {
    std::map<std::string, std::vector<Scancode>> loaded;
    std::size_t pos = 0;
    while (pos < text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        const auto eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        std::vector<Scancode> keys;
        bool ok = true;
        std::string_view rest = line.substr(eq + 1);
        while (!rest.empty() && ok) {
            const auto comma = rest.find(',');
            const std::string_view token = rest.substr(0, comma);
            Scancode value = 0;
            const auto res = std::from_chars(token.data(), token.data() + token.size(), value);
            ok = res.ec == std::errc{} && res.ptr == token.data() + token.size();
            if (ok) keys.push_back(value);
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        }
        if (ok) loaded[std::string(line.substr(0, eq))] = std::move(keys);
    }
    bindings_ = std::move(loaded);
    return static_cast<int>(bindings_.size());
}

}  // namespace han::platform
