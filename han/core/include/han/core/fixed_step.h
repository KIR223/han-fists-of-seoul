#pragma once
// Фиксированный шаг симуляции (E1): физика и бой идут с постоянной частотой, рендер — с любой.
namespace han::core {

class FixedStepClock {
public:
    explicit FixedStepClock(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 5)
        : step_(stepSeconds), maxSteps_(maxStepsPerFrame) {}

    // Добавляет реальное время кадра и возвращает, сколько шагов симуляции выполнить.
    // Если кадр слишком долгий, лишнее время отбрасывается («спираль смерти» не нужна).
    int advance(double frameSeconds) {
        if (frameSeconds < 0) frameSeconds = 0;
        accumulator_ += frameSeconds;
        int steps = 0;
        while (accumulator_ >= step_ && steps < maxSteps_) {
            accumulator_ -= step_;
            ++steps;
        }
        if (steps == maxSteps_ && accumulator_ >= step_) accumulator_ = 0;
        return steps;
    }

    double step() const { return step_; }
    // Доля до следующего шага (0..1) для интерполяции отрисовки.
    double alpha() const { return accumulator_ / step_; }

private:
    double step_;
    int maxSteps_;
    double accumulator_ = 0;
};

}  // namespace han::core
// Пример: for (int i = clock.advance(dt); i > 0; --i) simulate(clock.step()); render(clock.alpha());
