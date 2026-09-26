# SPDX-License-Identifier: Apache-2.0
#
# Generates C++ code from our .proto files into a static library, quotient_proto.
# No warning flags here: generated code is noisy and not ours.

add_library(quotient_proto STATIC
  envoy/service/ratelimit/v3/rls.proto
)

target_link_libraries(quotient_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

# Generated files go to build/<preset>/proto/..., mirroring the source tree, so
# code includes them as "envoy/service/ratelimit/v3/rls.grpc.pb.h".
target_include_directories(quotient_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# protoc needs the protobuf include dir to find google/protobuf/duration.proto.
get_target_property(PB_INCLUDE protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)

# 1) Messages: *.pb.h / *.pb.cc
protobuf_generate(
  TARGET quotient_proto
  LANGUAGE cpp
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${PB_INCLUDE}
)

# 2) gRPC service stubs: *.grpc.pb.h / *.grpc.pb.cc
protobuf_generate(
  TARGET quotient_proto
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${PB_INCLUDE}
)
