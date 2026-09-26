add_executable(quotient
  main.cpp
  service/policy_set.cpp
  util/config.cpp
  util/sha256.cpp
  web/rate_limit_grpc_service.cpp
)
target_include_directories(quotient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quotient PRIVATE quotient_proto gRPC::grpc++_reflection
                                       OpenSSL::Crypto spdlog::spdlog)
target_compile_options(quotient PRIVATE -Wall -Wextra -Wpedantic)
