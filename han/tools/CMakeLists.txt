han_module(tools DEPENDS han_core)

add_executable(han_fetch apps/fetch_main.cpp)
target_link_libraries(han_fetch PRIVATE han_tools)

add_executable(han_cooker apps/cooker_main.cpp)
target_link_libraries(han_cooker PRIVATE han_tools)
