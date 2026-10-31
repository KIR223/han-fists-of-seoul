han_module(core)
find_package(Threads REQUIRED)
target_link_libraries(han_core PUBLIC Threads::Threads)
