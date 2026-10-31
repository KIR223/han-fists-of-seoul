find_package(doctest CONFIG QUIET)
if(NOT doctest_FOUND)
    message(WARNING "doctest не найден (нужен vcpkg-манифест, функция tests): тесты engine пропущены")
    return()
endif()

add_executable(han_tests_engine src/modules_test.cpp src/core_test.cpp src/tools_test.cpp src/input_map_test.cpp src/render_cpu_test.cpp src/assets_test.cpp)
target_link_libraries(han_tests_engine PRIVATE doctest::doctest
    han_core han_platform han_render han_physics han_audio han_ui han_world han_ai han_editor han_tools
    han_police han_gangs han_vehicles)
target_compile_definitions(han_tests_engine PRIVATE HAN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME engine COMMAND han_tests_engine)
