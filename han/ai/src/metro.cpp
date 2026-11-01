#include "han/ai/metro.h"

#include <algorithm>
#include <cmath>

#include "han/core/json.h"

namespace han::ai {
namespace {
constexpr double kDay = 86400.0;

// Время перегона длиной L с разгоном a до vmax и торможением a (трапеция или треугольник).
double legTime(double L, double vmax, double a) {
    const double da = vmax * vmax / (2.0 * a);
    if (L >= 2.0 * da) return 2.0 * vmax / a + (L - 2.0 * da) / vmax;
    return 2.0 * std::sqrt(L / a);
}
// Пройденный путь и скорость через tau секунд после начала перегона.
void legState(double L, double vmax, double a, double tau, double& s, double& v) {
    const double da = vmax * vmax / (2.0 * a);
    if (L >= 2.0 * da) {
        const double tAcc = vmax / a, tCruise = (L - 2.0 * da) / vmax;
        if (tau <= tAcc) {
            s = 0.5 * a * tau * tau;
            v = a * tau;
        } else if (tau <= tAcc + tCruise) {
            s = da + vmax * (tau - tAcc);
            v = vmax;
        } else {
            const double r = std::min(tau - tAcc - tCruise, tAcc);
            s = da + vmax * tCruise + vmax * r - 0.5 * a * r * r;
            v = vmax - a * r;
        }
    } else {
        const double half = std::sqrt(L / a);
        if (tau <= half) {
            s = 0.5 * a * tau * tau;
            v = a * tau;
        } else {
            const double r = std::min(tau - half, half);
            s = L / 2.0 + a * half * r - 0.5 * a * r * r;
            v = a * (half - r);
        }
    }
    s = std::clamp(s, 0.0, L);
    v = std::max(0.0, v);
}
}  // namespace

bool MetroLine::load(const std::string& file, std::string& error) {
    core::Json j;
    if (!core::loadJsonFile(file, j, error)) return false;
    id_ = j["id"].asString();
    nameKey_ = j["name_key"].asString();
    color_ = j["color"].asString();
    if (id_.empty() || nameKey_.rfind("world.metro.", 0) != 0 || color_.size() != 7) {
        error = file + ": нужны id, name_key вида world.metro.* и color #RRGGBB";
        return false;
    }
    maxSpeed_ = static_cast<float>(j["max_speed"].asNumber(22));
    accel_ = static_cast<float>(j["acceleration"].asNumber(1));
    dwell_ = static_cast<float>(j["dwell_seconds"].asNumber(25));
    if (maxSpeed_ < 5.f || maxSpeed_ > 40.f || accel_ < 0.3f || accel_ > 2.f || dwell_ < 5.f || dwell_ > 120.f) {
        error = file + ": неверные max_speed, acceleration или dwell_seconds";
        return false;
    }
    const core::Json& sv = j["service"];
    service_.firstHour = static_cast<float>(sv["first_hour"].asNumber(5.5));
    service_.lastHour = static_cast<float>(sv["last_hour"].asNumber(24.5));
    service_.headwayPeak = static_cast<float>(sv["headway_peak"].asNumber(150));
    service_.headwayDay = static_cast<float>(sv["headway_day"].asNumber(240));
    service_.headwayNight = static_cast<float>(sv["headway_night"].asNumber(420));
    for (const char* name : {"peak_hours", "night_hours"}) {
        auto& target = std::string(name) == "peak_hours" ? service_.peak : service_.night;
        for (const core::Json& p : sv[name].items()) target.emplace_back(static_cast<float>(p.at(0).asNumber()), static_cast<float>(p.at(1).asNumber()));
    }
    if (service_.headwayPeak < 60.f || service_.lastHour <= service_.firstHour) {
        error = file + ": неверное расписание";
        return false;
    }
    stations_.clear();
    for (const core::Json& s : j["stations"].items()) {
        MetroStation st;
        st.id = s["id"].asString();
        st.nameKey = s["name_key"].asString();
        if (st.id.empty() || s["pos"].size() != 2) {
            error = file + ": у станции нужны id и pos [x, z]";
            return false;
        }
        for (const MetroStation& o : stations_)
            if (o.id == st.id) {
                error = file + ": повторяющаяся станция " + st.id;
                return false;
            }
        st.pos = {static_cast<float>(s["pos"].at(0).asNumber()), static_cast<float>(s["pos"].at(1).asNumber())};
        stations_.push_back(std::move(st));
    }
    if (stations_.size() < 3) {
        error = file + ": нужно минимум три станции";
        return false;
    }
    build();
    return true;
}

void MetroLine::build() {
    const std::size_t n = stations_.size();
    cum_.assign(n + 1, 0.0);
    segTime_.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = stations_[i].pos, b = stations_[(i + 1) % n].pos;
        const double L = std::sqrt(static_cast<double>((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z)));
        cum_[i + 1] = cum_[i] + L;
        segTime_[i] = legTime(L, maxSpeed_, accel_);
    }
    loop_ = cum_[n];
    // Рейс начинается отправлением с первой станции (tau = 0); на каждой следующей — перегон, стоянка, отправление; круг замыкается прибытием на первую.
    double t = 0.0;
    stations_[0].arriveOffset = 0.0;
    stations_[0].leaveOffset = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) {
            stations_[i].arriveOffset = t;
            stations_[i].leaveOffset = t + dwell_;
            t = stations_[i].leaveOffset;
        }
        t += segTime_[i];
    }
    loopTime_ = t;
    // Расписание рейсов на сутки: интервал зависит от часа отправления.
    departures_.clear();
    double d = static_cast<double>(service_.firstHour) * 3600.0;
    const double last = static_cast<double>(service_.lastHour) * 3600.0;
    while (d <= last) {
        departures_.push_back(d);
        const float hour = static_cast<float>(std::fmod(d / 3600.0, 24.0));
        const float h = headwayAt(hour);
        d += h > 0.f ? static_cast<double>(h) : 600.0;
    }
}

float MetroLine::headwayAt(float hour) const {
    hour = std::fmod(hour + 48.f, 24.f);
    const bool open = hour >= service_.firstHour || hour <= service_.lastHour - 24.f;
    if (!open) return 0.f;
    const auto inAny = [&](const std::vector<std::pair<float, float>>& periods) {
        for (const auto& p : periods)
            if ((hour >= p.first && hour < p.second) || (hour + 24.f >= p.first && hour + 24.f < p.second)) return true;
        return false;
    };
    if (inAny(service_.peak)) return service_.headwayPeak;
    if (inAny(service_.night)) return service_.headwayNight;
    return service_.headwayDay;
}

void MetroLine::stateAtTime(double tau, double& s, float& v, bool& dwelling, int& next) const {
    const std::size_t n = stations_.size();
    dwelling = false;
    v = 0.f;
    if (tau <= 0.0) {
        s = 0.0;
        next = 0;
        dwelling = true;
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double leave = stations_[i].leaveOffset, arrive = stations_[i].arriveOffset;
        if (i > 0 && tau >= arrive && tau < leave) {  // стоянка
            s = cum_[i];
            next = static_cast<int>(i);
            dwelling = true;
            return;
        }
        if (tau >= leave && tau < leave + segTime_[i]) {
            const double L = cum_[i + 1] - cum_[i];
            double ds, dv;
            legState(L, maxSpeed_, accel_, tau - leave, ds, dv);
            s = cum_[i] + ds;
            v = static_cast<float>(dv);
            next = static_cast<int>((i + 1) % n);
            return;
        }
    }
    s = loop_;
    next = 0;
    dwelling = true;
}

Vec2 MetroLine::trackPoint(double s, float* heading) const {
    const std::size_t n = stations_.size();
    s = std::fmod(std::max(0.0, s), loop_);
    std::size_t i = static_cast<std::size_t>(std::upper_bound(cum_.begin(), cum_.end(), s) - cum_.begin());
    i = std::clamp<std::size_t>(i, 1, n);
    const Vec2 a = stations_[i - 1].pos, b = stations_[i % n].pos;
    const double L = cum_[i] - cum_[i - 1], t = L > 1e-6 ? (s - cum_[i - 1]) / L : 0.0;
    if (heading) *heading = std::atan2(b.x - a.x, -(b.z - a.z));
    return {static_cast<float>(a.x + (b.x - a.x) * t), static_cast<float>(a.z + (b.z - a.z) * t)};
}

std::vector<TrainState> MetroLine::trains(double timeOfDay) const {
    std::vector<TrainState> out;
    for (std::size_t k = 0; k < departures_.size(); ++k) {
        const double tau = timeOfDay - departures_[k];
        if (tau < 0.0 || tau > loopTime_) continue;
        TrainState t;
        t.index = static_cast<int>(k);
        t.departed = departures_[k];
        bool dwelling = false;
        stateAtTime(tau, t.distance, t.speed, dwelling, t.nextStation);
        t.phase = dwelling ? TrainPhase::Dwelling : TrainPhase::Running;
        t.pos = trackPoint(t.distance, &t.heading);
        out.push_back(t);
    }
    return out;
}

std::optional<double> MetroLine::nextArrival(int station, double now) const {
    if (station < 0 || static_cast<std::size_t>(station) >= stations_.size() || departures_.empty()) return std::nullopt;
    const double off = station == 0 ? loopTime_ : stations_[static_cast<std::size_t>(station)].arriveOffset;  // на первую станцию — конец круга
    const double first = std::floor(now / kDay) * kDay;
    std::optional<double> best;
    for (int day = -1; day <= 1; ++day)  // рейсы вчера (перешли за полночь) и завтра
        for (double d : departures_) {
            const double arrival = first + day * kDay + d + off;
            if (arrival >= now && (!best || arrival < *best)) best = arrival;
        }
    return best;
}

double MetroLine::rideSeconds(int from, int to) const {
    const std::size_t n = stations_.size();
    if (from < 0 || to < 0 || static_cast<std::size_t>(from) >= n || static_cast<std::size_t>(to) >= n || from == to) return 0.0;
    double t = 0.0;
    for (std::size_t i = static_cast<std::size_t>(from); i != static_cast<std::size_t>(to); i = (i + 1) % n) {
        t += segTime_[i];
        if ((i + 1) % n != static_cast<std::size_t>(to)) t += dwell_;
    }
    return t;
}

int MetroLine::nearestStation(Vec2 p, float* distance) const {
    int best = -1;
    float bestD = 1e30f;
    for (std::size_t i = 0; i < stations_.size(); ++i) {
        const float dx = stations_[i].pos.x - p.x, dz = stations_[i].pos.z - p.z, d = std::sqrt(dx * dx + dz * dz);
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    if (distance) *distance = bestD;
    return best;
}

}  // namespace han::ai
