han_module(combat DEPENDS han_core)

# Консольный симулятор боя: combat_sim сценарий.json (запуск из корня репозитория)
add_executable(combat_sim tools/combat_sim.cpp)
target_link_libraries(combat_sim PRIVATE han_combat han_core)
if(MSVC)
    target_compile_options(combat_sim PRIVATE /W4 /permissive- /utf-8)
endif()
