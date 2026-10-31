# han_module(<имя> [DEPENDS <цели>...]) — статическая библиотека han_<имя> из src/*.cpp, заголовки в include/.
# Правило слоёв: core <- platform <- render/physics/audio/...; движок не зависит от игры.
function(han_module name)
    cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
    set(target han_${name})
    add_library(${target} STATIC ${sources})
    add_library(han::${name} ALIAS ${target})
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR})
    target_link_libraries(${target} PUBLIC ${ARG_DEPENDS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endif()
endfunction()
