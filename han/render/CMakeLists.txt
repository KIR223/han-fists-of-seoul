han_module(render DEPENDS han_platform)

# Vulkan-рендерер — если найден Vulkan SDK (загрузчик и заголовки). Демо дополнительно требует окно (SDL3).
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_sources(han_render PRIVATE vulkan/renderer.cpp vulkan/vk_util.cpp vulkan/post_process.cpp)
    target_link_libraries(han_render PUBLIC Vulkan::Vulkan)
    target_compile_definitions(han_render PUBLIC HAN_WITH_VULKAN=1)
    if(MSVC)
        # Сгенерированные SPIR-V-массивы большие; предупреждения о них не нужны.
        set_source_files_properties(vulkan/renderer.cpp PROPERTIES COMPILE_OPTIONS "/wd4838")
    endif()

    find_package(SDL3 CONFIG QUIET)
    if(SDL3_FOUND)
        add_executable(han_scene_demo apps/scene_demo.cpp)
        target_link_libraries(han_scene_demo PRIVATE han_render han_ui)  # han_ui даёт ImGui, если он найден

        # Демо города E10: данные OpenStreetMap, ячейки подгружаются стримингом (han_world).
        add_executable(han_city_demo apps/city_demo.cpp)
        target_link_libraries(han_city_demo PRIVATE han_render han_ui han_world)
        if(MSVC)
            target_compile_options(han_city_demo PRIVATE /W4 /permissive- /utf-8)
        endif()

        # Демо толпы E11: реальный район, прохожие по расписанию, реакции на драку и выстрелы.
        add_executable(han_crowd_demo apps/crowd_demo.cpp)
        target_link_libraries(han_crowd_demo PRIVATE han_render han_ui han_world han_ai)
        if(MSVC)
            target_compile_options(han_crowd_demo PRIVATE /W4 /permissive- /utf-8)
        endif()

        # Демо игрока E6: камера третьего лица, управление и паркур (game/player).
        add_executable(han_player_demo apps/player_demo.cpp)
        target_link_libraries(han_player_demo PRIVATE han_render han_ui han_physics han_player)
        if(MSVC)
            target_compile_options(han_player_demo PRIVATE /W4 /permissive- /utf-8)
        endif()

        # Демо физики E5: нужен ещё Jolt (vcpkg-функция physics). Это приложение, поэтому оно может зависеть и от рендера, и от физики.
        find_package(Jolt CONFIG QUIET)
        if(Jolt_FOUND)
            add_executable(han_physics_demo apps/physics_demo.cpp)
            target_link_libraries(han_physics_demo PRIVATE han_render han_ui han_physics han_audio)
            if(MSVC)
                target_compile_options(han_physics_demo PRIVATE /W4 /permissive- /utf-8)
            endif()
        endif()
    endif()
else()
    message(STATUS "Vulkan SDK не найден: рендерер не собирается")
endif()
