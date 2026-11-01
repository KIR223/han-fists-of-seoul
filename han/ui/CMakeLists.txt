han_module(ui DEPENDS han_platform han_render)

# Отладочный интерфейс на ImGui — если найдены imgui (vcpkg-функция ui), SDL3 и Vulkan.
find_package(imgui CONFIG QUIET)
find_package(SDL3 CONFIG QUIET)
find_package(Vulkan QUIET)
if(imgui_FOUND AND SDL3_FOUND AND Vulkan_FOUND)
    target_sources(han_ui PRIVATE imgui/imgui_layer.cpp)
    target_link_libraries(han_ui PUBLIC imgui::imgui SDL3::SDL3 Vulkan::Vulkan)
    target_compile_definitions(han_ui PUBLIC HAN_WITH_IMGUI=1)
else()
    message(STATUS "imgui, SDL3 или Vulkan не найдены: отладочный интерфейс ImGui не собирается")
endif()

# Игровой интерфейс на RmlUi (E8): рисуется через ImGui, поэтому нужны оба.
find_package(RmlUi CONFIG QUIET)
if(RmlUi_FOUND AND imgui_FOUND AND SDL3_FOUND AND Vulkan_FOUND)
    target_sources(han_ui PRIVATE rml/rml_layer.cpp)
    target_link_libraries(han_ui PUBLIC RmlUi::RmlUi)
    target_compile_definitions(han_ui PUBLIC HAN_WITH_RMLUI=1)
else()
    message(STATUS "RmlUi не найден: игровой интерфейс (E8) не собирается")
endif()
