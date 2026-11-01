#include <doctest/doctest.h>

#include "contracts/anim_api.h"
#include "contracts/combat_events.h"
#include "contracts/save_format.h"
#include "contracts/script_bind.h"
#include "contracts/stubs/asset_handles_stub.h"
#include "contracts/stubs/audio_api_stub.h"
#include "contracts/stubs/character_controller_stub.h"
#include "contracts/stubs/combat_world_stub.h"
#include "contracts/stubs/input_api_stub.h"
#include "contracts/stubs/physics_query_stub.h"
#include "contracts/stubs/skinning_stub.h"
#include "contracts/stubs/ui_api_stub.h"
#include "contracts/stubs/world_services_stub.h"

using namespace han::contracts;
using namespace han::contracts::stubs;

TEST_CASE("заглушки контрактов возвращают безопасные значения") {
    PhysicsQueryStub physics;
    CHECK_FALSE(physics.raycast({}, {1, 0, 0}).hit);

    CharacterControllerStub characters;
    const EntityId id = characters.create({}, {1, 2, 3});
    CHECK(id != kNoEntity);
    CHECK(characters.state(id).grounded);
    CHECK(characters.state(id).position.y == 2.f);

    WorldServicesStub world;
    CHECK_FALSE(world.isSacred({}));
    Vec3 path[4];
    CHECK(world.findPath({}, {5, 0, 5}, path, 4) == 2);
    CHECK(world.findPath({}, {5, 0, 5}, path, 1) == 0);

    CHECK_FALSE(CombatWorldStub{}.isSacredAt({}));
    CHECK(InputStub{}.bufferedAge(1) < 0.f);

    AssetsStub assets;
    const AssetHandle h = assets.load(AssetKind::Mesh, "props/street_lamp");
    CHECK(static_cast<bool>(h));
    CHECK(assets.isReady(h));
    CHECK_FALSE(assets.isReady({}));
}

TEST_CASE("контракты v1 компилируются целиком") {
    CHECK(kSaveFormatVersion == 1);
    CHECK(kNoEntity == 0);
}
