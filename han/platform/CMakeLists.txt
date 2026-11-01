han_module(platform DEPENDS han_core)

# Окно на SDL3 — только если библиотека найдена (vcpkg-функция window); карта действий от неё не зависит.
find_package(SDL3 CONFIG QUIET)
if(SDL3_FOUND)
    target_sources(han_platform PRIVATE sdl/window.cpp)
    target_link_libraries(han_platform PUBLIC SDL3::SDL3)
    target_compile_definitions(han_platform PUBLIC HAN_WITH_SDL=1)

    add_executable(han_window_demo apps/window_demo.cpp)
    target_link_libraries(han_window_demo PRIVATE han_platform)
    set_target_properties(han_window_demo PROPERTIES WIN32_EXECUTABLE OFF)
else()
    message(STATUS "SDL3 не найден: окно (han::platform::Window) не собирается")
endif()
