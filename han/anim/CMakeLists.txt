# A1: ozz использует стандартные имена targets (из установленного пакета или superbuild).
if(NOT TARGET ozz_animation_offline)
    find_path(HAN_OZZ_INCLUDE_DIR ozz/animation/runtime/skeleton.h)
    find_library(HAN_OZZ_BASE ozz_base)
    find_library(HAN_OZZ_RUNTIME ozz_animation)
    find_library(HAN_OZZ_OFFLINE ozz_animation_offline)
    if(HAN_OZZ_INCLUDE_DIR AND HAN_OZZ_BASE AND HAN_OZZ_RUNTIME AND HAN_OZZ_OFFLINE)
        add_library(ozz_animation_offline INTERFACE IMPORTED)
        set_target_properties(ozz_animation_offline PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${HAN_OZZ_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${HAN_OZZ_OFFLINE};${HAN_OZZ_RUNTIME};${HAN_OZZ_BASE}")
    else()
        option(HAN_FETCH_OZZ "Скачать закреплённый ozz 0.16 для A1" ON)
        if(NOT HAN_FETCH_OZZ)
            message(STATUS "ozz-animation не найден, HAN_FETCH_OZZ=OFF: A1 выключен")
            return()
        endif()
        include(FetchContent)
        set(ozz_build_tools OFF CACHE BOOL "")
        set(ozz_build_samples OFF CACHE BOOL "")
        set(ozz_build_howtos OFF CACHE BOOL "")
        set(ozz_build_tests OFF CACHE BOOL "")
        set(ozz_build_data OFF CACHE BOOL "")
        set(ozz_build_postfix OFF CACHE BOOL "")
        set(ozz_build_msvc_rt_dll ON CACHE BOOL "Использовать DLL runtime как x64-windows-static-md")
        FetchContent_Declare(han_ozz
            URL https://codeload.github.com/guillaumeblanc/ozz-animation/zip/6cbdc790123aa4731d82e255df187b3a8a808256
            URL_HASH SHA256=eea54703c3c80d81f2a6c43e8e893892494302eff3c8715ee1815de484aebe0e
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(han_ozz)
    endif()
endif()
han_module(anim DEPENDS han_core han_render ozz_animation_offline)
target_compile_definitions(han_anim PUBLIC HAN_WITH_ANIM=1)
