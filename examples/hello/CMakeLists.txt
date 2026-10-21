add_executable(teserra_hello main.cpp)
target_link_libraries(teserra_hello PRIVATE Teserra::Core)
teserra_set_warnings(teserra_hello)
