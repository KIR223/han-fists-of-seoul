han_module(ai DEPENDS han_world)

# Аудит нарушений трафика и толпы на реальной карте (этап 04): han_city_audit --osm файл.osm --focus x z --minutes 30
add_executable(han_city_audit apps/city_audit.cpp)
target_link_libraries(han_city_audit PRIVATE han_ai)
if(MSVC)
    target_compile_options(han_city_audit PRIVATE /W4 /permissive- /utf-8)
endif()
