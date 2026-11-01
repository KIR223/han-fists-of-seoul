// contracts/input_api.h — поставщик: A, потребители: B.
// Зачем: игра слушает действия, а не клавиши; переназначение и режимы управления — на стороне A.
#pragma once
#include <cstdint>

namespace han::contracts {
enum class ControlMode : std::uint8_t { Easy, Hard };
using ActionId = std::uint32_t;  // хэш имени действия из data (например "punch_light")

struct IInput {
    virtual bool pressed(ActionId a) const = 0;  // нажато в этом кадре
    virtual bool held(ActionId a) const = 0;
    virtual bool released(ActionId a) const = 0;
    virtual float axis(ActionId a) const = 0;         // -1..1
    virtual float bufferedAge(ActionId a) const = 0;  // секунд с нажатия, <0 — не нажималось (буфер ввода боя)
    virtual void setControlMode(ControlMode m) = 0;
    virtual ~IInput() = default;
};
}  // namespace han::contracts
// Пример: if (input.pressed(kPunchLight)) fighter.queue(Move::Jab);
