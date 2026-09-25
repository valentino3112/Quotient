cmake_minimum_required(VERSION 3.25)
project(quotient VERSION 0.1.0 LANGUAGES CXX)
 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
 
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

#In the future maybe!
#find_package(absl CONFIG REQUIRED)
#find_package(prometheus-cpp CONFIG REQUIRED)
 
set(QUOTIENT_SANITIZER "" CACHE STRING "e.g. address,undefined or thread")
if(QUOTIENT_SANITIZER)
  add_compile_options(-fsanitize=${QUOTIENT_SANITIZER} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${QUOTIENT_SANITIZER})
endif()
 

add_subdirectory(src)      # apply -Wall -Wextra -Werror to your own targets only

#add_subdirectory(proto)
#enable_testing()
#add_subdirectory(tests)
#add_subdirectory(bench)
