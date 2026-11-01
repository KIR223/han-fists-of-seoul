han_module(world DEPENDS han_physics)

# Запекание города в ячейки .hcell (E9/E16): han_citycook --osm файл.osm --out папка
add_executable(han_citycook apps/citycook.cpp)
target_link_libraries(han_citycook PRIVATE han_world)
if(MSVC)
    target_compile_options(han_citycook PRIVATE /W4 /permissive- /utf-8)
endif()
