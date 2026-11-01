// Тесты метро (G17): данные линии 2, расписание по часам, поезда как функция времени, прибытия, непрерывность движения.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

#include "han/ai/metro.h"
#include "han/ui/localization.h"

using han::ai::MetroLine;
using han::ai::TrainPhase;

namespace {
const MetroLine& line2() {
    static MetroLine l = [] {
        MetroLine x;
        std::string error;
        REQUIRE_MESSAGE(x.load((std::filesystem::path(HAN_REPO_DIR) / "data/world/metro/line2.json").string(), error), error);
        return x;
    }();
    return l;
}
}  // namespace

TEST_CASE("metro: линия 2 — 41 станция, кольцо около 47 км, названия на трёх языках, ближайшая к центру Мапо — Хапчон") {
    const MetroLine& l = line2();
    CHECK(l.stations().size() == 41);
    CHECK(l.loopLength() > 44000.0);
    CHECK(l.loopLength() < 50000.0);
    CHECK(l.color() == "#00A84D");
    han::ui::Localization loc;
    std::string error;
    REQUIRE_MESSAGE(loc.loadTable((std::filesystem::path(HAN_REPO_DIR) / "contracts/loc_requests/A.csv").string(), error), error);
    std::set<std::string> ids;
    for (const auto& s : l.stations()) {
        CHECK(ids.insert(s.id).second);
        for (const char* lang : {"ru", "ko", "en"}) CHECK_MESSAGE(loc.has(s.nameKey, lang), s.id << ": нет названия на " << lang);
    }
    CHECK(loc.has(l.nameKey(), "ko"));
    float d = 0.f;
    const int nearest = l.nearestStation({0.f, 0.f}, &d);
    CHECK(l.stations()[static_cast<std::size_t>(nearest)].id == "hapjeong");
    CHECK(d < 1200.f);
}

TEST_CASE("metro: интервалы по часам — пик короче дневного, ночью длиннее, закрыто с 0:30 до 5:30") {
    const MetroLine& l = line2();
    CHECK(l.headwayAt(8.0f) == doctest::Approx(150.f));
    CHECK(l.headwayAt(18.0f) == doctest::Approx(150.f));
    CHECK(l.headwayAt(13.0f) == doctest::Approx(240.f));
    CHECK(l.headwayAt(23.0f) == doctest::Approx(420.f));
    CHECK(l.headwayAt(3.0f) == doctest::Approx(0.f));
    CHECK(l.headwayAt(5.0f) == doctest::Approx(0.f));
    CHECK(l.headwayAt(0.25f) > 0.f);  // после полуночи рейсы ещё идут
    CHECK(l.departures().size() > 150);
    CHECK(l.departures().front() == doctest::Approx(5.5 * 3600.0));
    for (std::size_t i = 1; i < l.departures().size(); ++i) CHECK(l.departures()[i] > l.departures()[i - 1]);
}

TEST_CASE("metro: поезда — на пике их заметно больше, чем ночью; расставлены по кольцу без наложений; до открытия нет") {
    const MetroLine& l = line2();
    const auto peak = l.trains(8.0 * 3600.0);
    const auto day = l.trains(13.0 * 3600.0);
    const auto night = l.trains(23.0 * 3600.0);
    CHECK(peak.size() > day.size());
    CHECK(day.size() > night.size());
    CHECK(peak.size() > 20);
    CHECK(l.trains(3.0 * 3600.0).empty());
    CHECK(l.loopSeconds() > 3600.0);
    CHECK(l.loopSeconds() < 6000.0);  // полный круг 1–1,7 часа со стоянками
    std::vector<double> d;
    for (const auto& t : peak) d.push_back(t.distance);
    std::sort(d.begin(), d.end());
    for (std::size_t i = 1; i < d.size(); ++i) CHECK(d[i] - d[i - 1] > 100.0);
}

TEST_CASE("metro: движение непрерывно — за секунду поезд не проходит больше максимальной скорости, стоит на станции, скорость в пределах") {
    const MetroLine& l = line2();
    double prev = -1.0;
    bool sawDwell = false, sawRun = false;
    float maxSpeed = 0.f;
    const double start = 8.0 * 3600.0;
    int tracked = -1;
    {
        auto now = l.trains(start);  // следим за поездом, которому ещё ехать больше получаса
        std::sort(now.begin(), now.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; });
        REQUIRE(now.size() > 4);
        tracked = now[1].index;
    }
    for (double t = start; t < start + 1800.0; t += 1.0) {
        for (const auto& tr : l.trains(t)) {
            if (tr.index != tracked) continue;
            if (prev >= 0.0) CHECK(tr.distance - prev <= 22.5);
            prev = tr.distance;
            sawDwell |= tr.phase == TrainPhase::Dwelling;
            sawRun |= tr.phase == TrainPhase::Running;
            maxSpeed = std::max(maxSpeed, tr.speed);
        }
    }
    CHECK(sawDwell);
    CHECK(sawRun);
    CHECK(maxSpeed > 15.f);
    CHECK(maxSpeed <= 22.01f);
}

TEST_CASE("metro: прибытие на станцию — не раньше запроса, поезда идут с интервалом расписания, ночью следующий — утренний") {
    const MetroLine& l = line2();
    int hongik = -1;
    for (std::size_t i = 0; i < l.stations().size(); ++i)
        if (l.stations()[i].id == "hongik_univ") hongik = static_cast<int>(i);
    REQUIRE(hongik >= 0);
    const double now = 8.0 * 3600.0 + 10.0;
    const auto a = l.nextArrival(hongik, now);
    REQUIRE(a.has_value());
    CHECK(*a >= now);
    CHECK(*a - now < 160.0);
    const auto b = l.nextArrival(hongik, *a + 1.0);
    REQUIRE(b.has_value());
    CHECK(*b - *a == doctest::Approx(150.0).epsilon(0.02));  // в час пик каждые 150 секунд
    const auto morning = l.nextArrival(hongik, 3.0 * 3600.0);
    REQUIRE(morning.has_value());
    CHECK(*morning > 5.5 * 3600.0);
    // Поезд, который «прибудет», действительно стоит на этой станции в это время
    bool found = false;
    for (const auto& t : l.trains(*a + 5.0))
        if (t.phase == TrainPhase::Dwelling && t.nextStation == hongik) found = true;
    CHECK(found);
}

TEST_CASE("metro: время в пути между станциями растёт с расстоянием, едем по кругу в одну сторону; плохие данные отклоняются") {
    const MetroLine& l = line2();
    CHECK(l.rideSeconds(0, 1) > 30.0);
    CHECK(l.rideSeconds(0, 5) > l.rideSeconds(0, 2));
    CHECK(l.rideSeconds(5, 0) > 0.0);
    CHECK(l.rideSeconds(3, 3) == doctest::Approx(0.0));
    CHECK(l.rideSeconds(-1, 3) == doctest::Approx(0.0));
    // Туда и обратно по одному направлению в сумме около полного круга
    CHECK(l.rideSeconds(2, 20) + l.rideSeconds(20, 2) == doctest::Approx(l.loopSeconds()).epsilon(0.05));
    MetroLine bad;
    std::string error;
    CHECK_FALSE(bad.load("нет_файла.json", error));
    CHECK_FALSE(l.nextArrival(999, 0.0).has_value());
}
