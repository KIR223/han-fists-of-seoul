#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string_view>

#include "han/ai/ai.h"
#include "han/audio/audio.h"
#include "han/core/core.h"
#include "han/editor/editor.h"
#include "han/gangs/gangs.h"
#include "han/physics/physics.h"
#include "han/platform/platform.h"
#include "han/police/police.h"
#include "han/render/render.h"
#include "han/tools/tools.h"
#include "han/ui/ui.h"
#include "han/vehicles/vehicles.h"
#include "han/world/world.h"

TEST_CASE("все модули зоны A собрались и слинковались") {
    CHECK(std::string_view(han::core::moduleName()) == "core");
    CHECK(std::string_view(han::platform::moduleName()) == "platform");
    CHECK(std::string_view(han::render::moduleName()) == "render");
    CHECK(std::string_view(han::physics::moduleName()) == "physics");
    CHECK(std::string_view(han::audio::moduleName()) == "audio");
    CHECK(std::string_view(han::ui::moduleName()) == "ui");
    CHECK(std::string_view(han::world::moduleName()) == "world");
    CHECK(std::string_view(han::ai::moduleName()) == "ai");
    CHECK(std::string_view(han::editor::moduleName()) == "editor");
    CHECK(std::string_view(han::tools::moduleName()) == "tools");
    CHECK(std::string_view(han::police::moduleName()) == "police");
    CHECK(std::string_view(han::gangs::moduleName()) == "gangs");
    CHECK(std::string_view(han::vehicles::moduleName()) == "vehicles");
}
