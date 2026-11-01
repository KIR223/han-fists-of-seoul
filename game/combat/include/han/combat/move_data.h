// han/combat/move_data.h — приёмы (MoveData, G1): формат contracts/schemas/move.schema.json, данные — data/combat/moves/*.json.
// Все числа боя лежат в данных; код только читает их и проверяет.
#pragma once
#include <string>
#include <vector>

#include "han/core/json.h"

namespace han::combat {
enum class Trajectory : unsigned char { Straight, ArcLeft, ArcRight, Up, Down, Spin, Low, Push };
enum class Height : unsigned char { Head, Torso, Legs };
enum class Movement : unsigned char { Static, StepIn, Retreat, Sidestep, Pivot, Circle, Run };

struct MoveData {
    std::string id, style, nameKey, type, limb, anim, sound;
    int unlockLevel = 1;
    Trajectory trajectory = Trajectory::Straight;
    Height height = Height::Head;
    Movement movement = Movement::Static;
    int startup = 1, active = 1, recovery = 1;  // кадры при 60 Гц
    int cancelFrom = 0, cancelTo = 0;           // кадры от начала приёма, когда можно отменить в следующий
    float range = 1.f;                          // от центра атакующего до края цели, м
    float damage = 0, stamina = 0, balance = 0, focus = 0;
    int onBlockFrames = 0, onHitFrames = 0;     // преимущество атакующего по кадрам
    bool armor = false, unblockable = false, power = false;
    float impulse = 0;
    int hitstopMs = 0;
    float shake = 0;

    int totalFrames() const { return startup + active + recovery; }
    bool inActiveFrames(int frame) const { return frame >= startup && frame < startup + active; }
};

const char* trajectoryName(Trajectory t);
const char* heightName(Height h);
const char* movementName(Movement m);
bool trajectoryFromName(const std::string& name, Trajectory& out);
bool heightFromName(const std::string& name, Height& out);
bool movementFromName(const std::string& name, Movement& out);

// Разбор объекта JSON по схеме с проверкой смысла (окно отмены внутри приёма, дальность, числа). false — ошибка в error.
bool parseMove(const core::Json& json, MoveData& out, std::string& error);

class MoveLibrary {
public:
    bool add(MoveData move, std::string& error);  // false — повторяющийся id
    // Читает все *.json из папки по алфавиту; ошибки отдельных файлов собираются в error, остальные файлы грузятся.
    bool loadDirectory(const std::string& dir, std::string& error);
    const MoveData* byId(const std::string& id) const;
    const std::vector<MoveData>& all() const { return moves_; }
    std::size_t size() const { return moves_.size(); }

private:
    std::vector<MoveData> moves_;
};
}  // namespace han::combat
// Пример: MoveLibrary lib; std::string err; lib.loadDirectory("data/combat/moves", err); const MoveData* jab = lib.byId("boxing_jab");
