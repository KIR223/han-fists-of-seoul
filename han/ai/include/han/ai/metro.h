// han/ai/metro.h — метро (G17): кольцевая линия 2 с реальными станциями. Поезда — чистая функция времени по расписанию:
// положение любого поезда и время прибытия считаются по секундам суток, поэтому сохранение, быстрое перемещение и телефон («поезд через N минут»)
// не требуют хранить состояние. Данные — data/world/metro/*.json; названия станций — ключи локализации world.metro.*.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "han/ai/navgrid.h"

namespace han::ai {

struct MetroStation {
    std::string id, nameKey;
    Vec2 pos;
    double arriveOffset = 0.0;  // секунд от отправления поезда с первой станции до прибытия сюда (заполняется при загрузке)
    double leaveOffset = 0.0;   // до отправления отсюда
};

struct ServiceHours {
    float firstHour = 5.5f, lastHour = 24.5f;     // выпуск первого и последнего поезда с первой станции (часы суток; > 24 — после полуночи)
    float headwayPeak = 150.f, headwayDay = 240.f, headwayNight = 420.f;  // интервал, секунд
    std::vector<std::pair<float, float>> peak, night;                    // периоды часов
};

enum class TrainPhase : unsigned char { Running, Dwelling };

struct TrainState {
    int index = 0;                 // номер рейса за сутки
    double departed = 0.0;         // секунд суток, когда рейс вышел с первой станции
    double distance = 0.0;         // пройдено по кольцу, м
    Vec2 pos;
    float heading = 0.f;           // радианы: 0 — на север (-Z), положительные — к востоку (+X)
    float speed = 0.f;
    TrainPhase phase = TrainPhase::Running;
    int nextStation = 0;           // куда едет (или где стоит)
};

class MetroLine {
public:
    // Читает файл линии. false — ошибка в error.
    bool load(const std::string& file, std::string& error);

    const std::string& id() const { return id_; }
    const std::string& nameKey() const { return nameKey_; }
    const std::string& color() const { return color_; }
    const std::vector<MetroStation>& stations() const { return stations_; }
    double loopLength() const { return loop_; }
    double loopSeconds() const { return loopTime_; }   // полный круг со стоянками
    const ServiceHours& service() const { return service_; }

    // Интервал движения в этот час суток, секунд; 0 — линия не работает.
    float headwayAt(float hour) const;
    // Рейсы за сутки (секунды суток отправления с первой станции).
    const std::vector<double>& departures() const { return departures_; }
    // Поезда на линии в момент timeOfDay (секунды суток, можно > 86400 для рейсов, перешедших за полночь).
    std::vector<TrainState> trains(double timeOfDay) const;
    // Ближайшее прибытие поезда на станцию не раньше now (секунды суток; ищем и на следующие сутки). nullopt — линия не работает.
    std::optional<double> nextArrival(int station, double now) const;
    // Время в пути между станциями по ходу поезда, секунд (со стоянками на промежуточных).
    double rideSeconds(int from, int to) const;
    // Ближайшая станция к точке и расстояние до неё.
    int nearestStation(Vec2 p, float* distance = nullptr) const;
    // Точка на кольце на расстоянии s от первой станции.
    Vec2 trackPoint(double s, float* heading = nullptr) const;

private:
    void build();
    void stateAtTime(double tau, double& s, float& v, bool& dwelling, int& next) const;

    std::string id_, nameKey_, color_;
    std::vector<MetroStation> stations_;
    std::vector<double> cum_;       // накопленные расстояния до станций по кольцу
    std::vector<double> segTime_;   // время перегона (без стоянки)
    double loop_ = 0.0, loopTime_ = 0.0;
    float maxSpeed_ = 22.f, accel_ = 1.f, dwell_ = 25.f;
    ServiceHours service_;
    std::vector<double> departures_;
};

}  // namespace han::ai
// Пример: MetroLine line; line.load("data/world/metro/line2.json", err); auto t = line.nextArrival(line.nearestStation(player), now);
