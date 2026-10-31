han_module(tools DEPENDS han_core han_render)

target_compile_definitions(han_tools PRIVATE HAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Импорт glTF — если найден fastgltf (vcpkg-функция assets); оптимизация под кэш GPU — если найден meshoptimizer.
find_package(fastgltf CONFIG QUIET)
if(fastgltf_FOUND)
    target_sources(han_tools PRIVATE gltf/gltf_import.cpp)
    target_link_libraries(han_tools PUBLIC fastgltf::fastgltf)
    target_compile_definitions(han_tools PUBLIC HAN_WITH_GLTF=1)
else()
    message(STATUS "fastgltf не найден: импорт glTF в han_cooker не собирается")
endif()

find_package(meshoptimizer CONFIG QUIET)
if(meshoptimizer_FOUND)
    target_link_libraries(han_tools PUBLIC meshoptimizer::meshoptimizer)
    target_compile_definitions(han_tools PUBLIC HAN_WITH_MESHOPT=1)
else()
    message(STATUS "meshoptimizer не найден: оптимизация под кэш GPU отключена (сварка вершин работает)")
endif()

add_executable(han_fetch apps/fetch_main.cpp)
target_link_libraries(han_fetch PRIVATE han_tools)

add_executable(han_cooker apps/cooker_main.cpp)
target_link_libraries(han_cooker PRIVATE han_tools)
