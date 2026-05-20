cmake_minimum_required(VERSION 3.10)
project(uav_comm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # 必须加，用于 .so

# =========================
# Protobuf
# =========================
# find_package(Protobuf REQUIRED)

# proto 文件
# file(GLOB PROTO_FILES ${PROJECT_SOURCE_DIR}/message/*.proto)

# 生成 pb.cc / pb.h
# protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Zenoh-c
set(ZENOHC_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/zenoh/zenoh-c/include)
set(ZENOHC_LIB_DIR ${PROJECT_SOURCE_DIR}/zenoh/zenoh-c/lib)

# 头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/zenoh/include
    ${PROJECT_SOURCE_DIR}/zenoh/include/zenoh
    ${ZENOHC_INCLUDE_DIR}
    # ${Protobuf_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# 库源码：core + comm + src/uav_comm.cpp
file(GLOB_RECURSE LIB_SRCS
    ${PROJECT_SOURCE_DIR}/core/*.cpp
    ${PROJECT_SOURCE_DIR}/transport/zenoh/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/runtime/*.cpp
    # ${PROTO_SRCS}
)

# 编译动态库
add_library(uav_comm SHARED ${LIB_SRCS})
target_compile_definitions(uav_comm PRIVATE ZENOHCXX_ZENOHC)
target_link_directories(uav_comm PRIVATE ${ZENOHC_LIB_DIR})
target_link_libraries(uav_comm
    ${Protobuf_LIBRARIES}
    zenohc
    pthread
)

# =========================
# Proto Library
# =========================
# add_library(uav_proto SHARED ${PROTO_SRCS})

# target_include_directories(uav_proto PUBLIC
#     ${Protobuf_INCLUDE_DIRS}
#     ${CMAKE_CURRENT_BINARY_DIR}
# )

# target_link_libraries(uav_proto PUBLIC ${Protobuf_LIBRARIES})

# 安装
install(TARGETS uav_comm
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/uav_comm
    DESTINATION include
)

# 示例
macro(add_example name)
    add_executable(${name} examples/${name}.cpp)

    target_link_directories(${name} PRIVATE ${ZENOHC_LIB_DIR})

    target_link_libraries(${name}
        uav_comm
    )
    target_compile_definitions(${name} PRIVATE ZENOHCXX_ZENOHC)
endmacro()

# add_example(pub)
# add_example(sub)
# add_example(test_match_ok)
# add_example(test_qos_mismatch)
# add_example(test_topic_match)
# add_example(test_topic_mismatch)
# add_example(test_domain_isolation)
# add_example(test_perf)
# add_example(test_thread_safe)
# add_example(test_multi_topic)