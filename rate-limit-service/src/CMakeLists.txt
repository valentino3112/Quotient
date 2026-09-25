add_executable(quotient main.cpp)
target_link_libraries(quotient PRIVATE spdlog::spdlog)
target_compile_options(quotient PRIVATE -Wall -Wextra -Wpedantic)