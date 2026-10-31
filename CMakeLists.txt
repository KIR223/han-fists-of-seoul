cmake_minimum_required(VERSION 3.25)
project(HanFistsOfSeoul VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(HanModule)

enable_testing()

# Автопоиск модулей: каждая зона кладёт свой CMakeLists.txt в свою папку, корень править не нужно.
# Порядок: движок и игра, затем тесты.
foreach(pattern IN ITEMS "han/*" "game/*" "tests/*")
    file(GLOB module_dirs LIST_DIRECTORIES true RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/${pattern})
    list(SORT module_dirs)
    foreach(dir IN LISTS module_dirs)
        if(EXISTS ${CMAKE_SOURCE_DIR}/${dir}/CMakeLists.txt)
            message(STATUS "Модуль: ${dir}")
            add_subdirectory(${dir})
        endif()
    endforeach()
endforeach()
