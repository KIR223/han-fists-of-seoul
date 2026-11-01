#pragma once
// Локализация интерфейса (E8): ключ → текст на ru, ko, en. Читает CSV двух видов:
//   1) таблица заявок contracts/loc_requests/*.csv: «key,ru,ko,en,context» (по одной строке на ключ);
//   2) файлы языка data/loc/<язык>/<зона>.csv: «key,text» (по одному файлу на зону и язык).
// Порядок подстановки: текущий язык → английский → русский → сам ключ (так видно, что перевода нет). ru — основной язык проекта.
#include <map>
#include <string>
#include <vector>

namespace han::ui {

class Localization {
public:
    // Таблица «key,ru,ko,en[,context]». Ошибки строк копятся в error (остальные строки загружаются); false — файла нет или нет заголовка.
    bool loadTable(const std::string& file, std::string& error);
    // Файл языка «key,text»: lang — "ru", "ko" или "en".
    bool loadLanguageFile(const std::string& lang, const std::string& file, std::string& error);
    // Все *.csv из data/loc/<lang>/ (по алфавиту).
    bool loadLanguageDir(const std::string& lang, const std::string& dir, std::string& error);

    void setLanguage(const std::string& lang) { lang_ = lang; }
    const std::string& language() const { return lang_; }
    static std::vector<std::string> languages() { return {"ru", "ko", "en"}; }

    // Текст по ключу в текущем языке с запасными вариантами; если нет нигде — сам ключ.
    const std::string& get(const std::string& key) const;
    bool has(const std::string& key, const std::string& lang) const;
    // Подстановка {0}, {1}… в текст ключа.
    std::string format(const std::string& key, const std::vector<std::string>& args) const;
    // Ключи, у которых нет текста на каком-то из трёх языков (для проверки перед релизом).
    std::vector<std::string> incomplete() const;
    std::size_t size() const { return texts_.size(); }

private:
    std::map<std::string, std::map<std::string, std::string>> texts_;  // key → lang → text
    std::string lang_ = "ru";
};

// Разбор одной строки CSV с кавычками («"a,b"», «""» внутри кавычек — одна кавычка). Пустая строка и строка-комментарий («#») дают пустой результат.
std::vector<std::string> parseCsvLine(const std::string& line);

}  // namespace han::ui
// Пример: Localization loc; std::string e; loc.loadTable("contracts/loc_requests/B.csv", e); loc.setLanguage("ko"); auto s = loc.get("move.boxing.jab");
