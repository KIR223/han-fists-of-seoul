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
    endif()
else()
    message(STATUS "Vulkan SDK не найден: рендерер не собирается")
endif()
