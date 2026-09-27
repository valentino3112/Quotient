# Embed the Lua scripts in the binary: their text is pasted into a generated
# header (build/<preset>/src/generated/repository/lua_scripts.h). Editing a .lua
# file re-runs this step on the next build.
set(QUOTIENT_LUA_DIR ${PROJECT_SOURCE_DIR}/scripts/lua)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${QUOTIENT_LUA_DIR}/gcra.lua ${QUOTIENT_LUA_DIR}/fixed_window.lua)
file(READ ${QUOTIENT_LUA_DIR}/gcra.lua QUOTIENT_GCRA_LUA)
file(READ ${QUOTIENT_LUA_DIR}/fixed_window.lua QUOTIENT_FIXED_WINDOW_LUA)
configure_file(repository/lua_scripts.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/repository/lua_scripts.h @ONLY)

add_executable(quotient
  main.cpp
  repository/pg_policy_repository.cpp
  repository/redis_rate_limit_backend.cpp
  service/policy_service.cpp
  service/policy_set.cpp
  util/config.cpp
  util/sha256.cpp
  web/rate_limit_grpc_service.cpp
)
target_include_directories(quotient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                            ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(quotient PRIVATE quotient_proto gRPC::grpc++_reflection
                                       OpenSSL::Crypto libpqxx::pqxx redis++::redis++_static
                                       spdlog::spdlog)
target_compile_options(quotient PRIVATE -Wall -Wextra -Wpedantic)
