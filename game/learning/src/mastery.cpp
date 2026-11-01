#include "han/learning/mastery.h"

#include <algorithm>

namespace han::learning {
using core::Json;

const char* stageName(MoveStage s) {
    switch (s) {
        case MoveStage::Unknown: return "unknown";
        case MoveStage::Seen: return "seen";
        case MoveStage::Remembered: return "remembered";
        case MoveStage::Learned: return "learned";
        case MoveStage::Mastered: return "mastered";
    }
    return "unknown";
}

bool MoveMastery::observe(const std::string& moveId, float observationSkill) {
    MoveProgress& p = progress_[moveId];
    if (p.remembered) return false;
    if (++p.observations >= cfg_.observationsToRemember) {
        p.remembered = true;
        // Внимательный игрок запоминает с «заделом» опыта: до половины порога разучивания при Наблюдательности 10.
        p.xp += cfg_.learnXp * cfg_.observationBonus * std::clamp(observationSkill, 0.f, 10.f) / 10.f;
        return true;
    }
    return false;
}

void MoveMastery::grant(const std::string& moveId) {
    MoveProgress& p = progress_[moveId];
    p.remembered = true;
    p.xp = std::max(p.xp, cfg_.learnXp);
}

float MoveMastery::train(const std::string& moveId, int ratingIndex) {
    const auto it = progress_.find(moveId);
    if (it == progress_.end() || !it->second.remembered) return 0.f;  // тренировать можно только запомненное
    const int r = std::clamp(ratingIndex, 0, 4);
    const float gain = std::min(cfg_.trainingXpByRating[r], std::max(0.f, cfg_.dailyTrainingCap - trainedToday_));
    it->second.xp += gain;
    trainedToday_ += gain;
    return gain;
}

void MoveMastery::fightUse(const std::string& moveId, bool hit) {
    const auto it = progress_.find(moveId);
    if (it == progress_.end() || stage(moveId) < MoveStage::Learned) return;
    it->second.xp += hit ? cfg_.fightXpHit : cfg_.fightXpUse;
}

float MoveMastery::xp(const std::string& moveId) const {
    const auto it = progress_.find(moveId);
    return it == progress_.end() ? 0.f : it->second.xp;
}

int MoveMastery::stars(const std::string& moveId) const {
    if (stage(moveId) < MoveStage::Learned) return 0;
    const float x = xp(moveId);
    int n = 0;
    for (float need : cfg_.starXp)
        if (x >= need) ++n;
    return n;
}

MoveStage MoveMastery::stage(const std::string& moveId) const {
    const auto it = progress_.find(moveId);
    if (it == progress_.end()) return MoveStage::Unknown;
    const MoveProgress& p = it->second;
    if (!p.remembered) return p.observations > 0 ? MoveStage::Seen : MoveStage::Unknown;
    if (p.xp < cfg_.learnXp) return MoveStage::Remembered;
    const float top = cfg_.starXp.empty() ? 1e30f : cfg_.starXp.back();
    return p.xp >= top ? MoveStage::Mastered : MoveStage::Learned;
}

Json MoveMastery::toJson() const {
    Json root = Json::object();
    Json moves = Json::object();
    for (const auto& [id, p] : progress_) {
        Json m = Json::object();
        m.set("observations", Json::number(p.observations));
        m.set("xp", Json::number(p.xp));
        m.set("remembered", Json::boolean(p.remembered));
        moves.set(id, std::move(m));
    }
    root.set("moves", std::move(moves));
    root.set("trained_today", Json::number(trainedToday_));
    return root;
}

bool MoveMastery::fromJson(const Json& json, std::string& error) {
    if (!json.isObject() || !json["moves"].isObject()) {
        error = "прогресс приёмов: нет объекта moves";
        return false;
    }
    std::map<std::string, MoveProgress> loaded;
    for (const auto& [id, m] : json["moves"].members()) {
        MoveProgress p;
        p.observations = static_cast<int>(m["observations"].asNumber(0));
        p.xp = static_cast<float>(m["xp"].asNumber(0));
        p.remembered = m["remembered"].asBool(false);
        if (p.observations < 0 || p.xp < 0.f) {
            error = "прогресс приёмов: отрицательные значения у " + id;
            return false;
        }
        loaded[id] = p;
    }
    progress_ = std::move(loaded);
    trainedToday_ = static_cast<float>(json["trained_today"].asNumber(0));
    return true;
}

bool parseLearningConfig(const Json& json, LearningConfig& out, std::string& error) {
    if (!json.isObject()) {
        error = "обучение: ожидался объект";
        return false;
    }
    LearningConfig c;
    c.observationsToRemember = static_cast<int>(json["observations_to_remember"].asNumber(3));
    c.learnXp = static_cast<float>(json["learn_xp"].asNumber(40));
    c.fightXpUse = static_cast<float>(json["fight_xp_use"].asNumber(1));
    c.fightXpHit = static_cast<float>(json["fight_xp_hit"].asNumber(2.5));
    c.observationBonus = static_cast<float>(json["observation_bonus"].asNumber(0.5));
    c.dailyTrainingCap = static_cast<float>(json["daily_training_cap"].asNumber(120));
    const Json& stars = json["star_xp"];
    if (stars.isArray()) {
        c.starXp.clear();
        for (std::size_t i = 0; i < stars.size(); ++i) c.starXp.push_back(static_cast<float>(stars.at(i).asNumber()));
    }
    const Json& t = json["training_xp"];
    if (t.isArray() && t.size() == 5)
        for (std::size_t i = 0; i < 5; ++i) c.trainingXpByRating[i] = static_cast<float>(t.at(i).asNumber());
    if (c.observationsToRemember < 1 || c.learnXp <= 0.f || c.dailyTrainingCap < 0.f || c.starXp.size() != 3) {
        error = "обучение: неверные пороги (нужны 3 порога звёзд, learn_xp > 0)";
        return false;
    }
    if (!std::is_sorted(c.starXp.begin(), c.starXp.end()) || c.starXp.front() < c.learnXp) {
        error = "обучение: пороги звёзд должны возрастать и быть не ниже learn_xp";
        return false;
    }
    out = std::move(c);
    return true;
}
}  // namespace han::learning
