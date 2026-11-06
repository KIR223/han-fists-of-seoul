#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "han/core/json.h"
#include "han/render/frame_metrics.h"

using namespace han::render;

TEST_CASE("frame_metrics: процентили методом ближайшего ранга") {
    std::vector<double> v;
    for (int i = 1; i <= 100; ++i) v.push_back(i);
    const auto p = summarize(v);
    CHECK(p.count == 100);
    CHECK(p.mean == doctest::Approx(50.5));
    CHECK(p.p50 == 50);
    CHECK(p.p95 == 95);
    CHECK(p.p99 == 99);
    CHECK(p.max == 100);
}

TEST_CASE("frame_metrics: пустой ряд и нечисловые значения не ломают сводку") {
    CHECK(summarize({}).count == 0);
    const auto p = summarize({1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 3.0});
    CHECK(p.count == 2);
    CHECK(p.max == 3.0);
    const auto one = summarize({7.0});
    CHECK(one.p50 == 7.0);
    CHECK(one.p99 == 7.0);
}

TEST_CASE("frame_metrics: прогрев отбрасывается, неизмеренные поля (-1) не искажают сводку") {
    MetricsRecorder recorder(2);
    for (int i = 0; i < 6; ++i) {
        FrameSample s;
        s.wallMs = 10.0 + i;      // кадры 0 и 1 — прогрев
        s.gpuMs = (i % 2) ? -1 : 5.0;
        recorder.add(s);
    }
    CHECK(recorder.samples().size() == 4);
    const auto wall = recorder.of(&FrameSample::wallMs);
    CHECK(wall.count == 4);
    CHECK(wall.max == 15.0);
    const auto gpu = recorder.of(&FrameSample::gpuMs);
    CHECK(gpu.count == 2);  // только измеренные кадры
    CHECK(gpu.mean == 5.0);
    CHECK(recorder.of(&FrameSample::drawCalls).count == 0);
}

TEST_CASE("frame_metrics: худшие кадры — по убыванию времени, с местом камеры") {
    MetricsRecorder recorder(1);
    const double wall[] = {50.0, 5.0, 30.0, 6.0, 40.0};  // первый кадр — прогрев и в сводку не входит
    for (int i = 0; i < 5; ++i) {
        FrameSample s;
        s.wallMs = wall[i];
        s.frame = i;
        s.cameraX = -100.0 + i;
        s.cameraZ = -5.0;
        recorder.add(s);
    }
    const auto worst = recorder.worst(2);
    REQUIRE(worst.size() == 2);
    CHECK(worst[0].frame == 4);
    CHECK(worst[1].frame == 2);
    CHECK(worst[0].cameraX == doctest::Approx(-96.0));
    han::core::Json json;
    std::string error;
    REQUIRE_MESSAGE(han::core::parseJson(metricsToJson(recorder, {}), json, error), error);
    CHECK(json["worst_frames"].at(0)["frame"].asNumber() == 4);
    CHECK(json["worst_frames"].at(0)["x"].asNumber() == doctest::Approx(-96.0));
}

TEST_CASE("frame_metrics: JSON читается нашим разбором, метаданные экранируются") {
    MetricsRecorder recorder(0);
    FrameSample s;
    s.wallMs = 20.0;
    s.drawCalls = 1234;
    recorder.add(s);
    const auto text = metricsToJson(recorder, {{"gpu", "Test \"GPU\" \ 1"}, {"hour", "13"}});
    han::core::Json json;
    std::string error;
    REQUIRE_MESSAGE(han::core::parseJson(text, json, error), error);
    CHECK(json["measured_frames"].asNumber() == 1);
    CHECK(json["wall_ms"]["p95"].asNumber() == doctest::Approx(20.0));
    CHECK(json["draw_calls"]["max"].asNumber() == doctest::Approx(1234.0));
    CHECK(json["meta"]["gpu"].asString() == "Test \"GPU\" \ 1");
}
