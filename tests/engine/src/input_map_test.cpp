#include <doctest/doctest.h>

#include "han/platform/input_map.h"

using han::platform::InputMap;

TEST_CASE("InputMap: нажато, удержание, отпущено") {
    InputMap m;
    m.bind("jab", 20);
    m.beginFrame(1.0);
    m.onKey(20, true);
    CHECK(m.pressed("jab"));
    CHECK(m.held("jab"));
    m.beginFrame(1.1);
    CHECK_FALSE(m.pressed("jab"));
    CHECK(m.held("jab"));
    m.onKey(20, false);
    CHECK(m.released("jab"));
    CHECK_FALSE(m.held("jab"));
}

TEST_CASE("InputMap: автоповтор клавиши не новое нажатие") {
    InputMap m;
    m.bind("jab", 20);
    m.beginFrame(0);
    m.onKey(20, true);
    m.beginFrame(0.1);
    m.onKey(20, true);
    CHECK_FALSE(m.pressed("jab"));
}

TEST_CASE("InputMap: буфер ввода") {
    InputMap m;
    m.bind("jab", 20);
    CHECK(m.secondsSincePress("jab") < 0);
    m.beginFrame(2.0);
    m.onKey(20, true);
    m.beginFrame(2.25);
    CHECK(m.secondsSincePress("jab") == doctest::Approx(0.25));
    CHECK(m.secondsSincePress("нет такого") < 0);
}

TEST_CASE("InputMap: переназначение и конфликт") {
    InputMap m;
    m.bind("jab", 20);
    m.bind("block", 21);
    CHECK(m.rebind("jab", 21) == "block");
    CHECK(m.bindings("jab") == std::vector<int>{21});
    CHECK(m.bindings("block").empty());
    CHECK(m.rebind("jab", 30).empty());
}

TEST_CASE("InputMap: сохранение и загрузка привязок") {
    InputMap a;
    a.bind("jab", 20);
    a.bind("jab", 21);
    a.bind("block", 7);
    const std::string text = a.serialize();
    InputMap b;
    CHECK(b.deserialize(text) == 2);
    CHECK(b.bindings("jab") == std::vector<int>{20, 21});
    CHECK(b.deserialize("мусор\nx=1,abc\nok=5\n=3\n") == 1);
    CHECK(b.bindings("ok") == std::vector<int>{5});
}
