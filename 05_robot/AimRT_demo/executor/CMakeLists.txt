cmake_minimum_required(VERSION 3.24)

project(Logger LANGUAGES C CXX)

# Set cmake path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Some option
option(LOGGER_EXAMPLE "Build example" OFF)
option(LOGGER_SIMULATION "Build simulation" OFF)
option(LOGGER_TEST_CAMERA "Build test_camera" OFF)

# Some necessary settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(INSTALL_CONFIG_NAME ${PROJECT_NAME}-config)

# Master project configuration
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  set(CMAKE_C_VISIBILITY_PRESET hidden)
  set(CMAKE_C_VISIBILITY_INLINES_HIDDEN ON)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  set(CMAKE_CXX_VISIBILITY_INLINES_HIDDEN ON)
  set(BUILD_SHARED_LIBS OFF)

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(LOGGER_BUILD_TESTS)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
    endif()
  endif()

  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/utf-8 /wd4819)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
endif()

if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()

# Include cmake module
include(NamespaceTool)

if(LOGGER_BUILD_TESTS)
  include(GetGTest)
  enable_testing()
endif()

set(YAML_CPP_INSTALL ON)
# include(GetAimRT)

# 指向你的AimRT安装目录
list(APPEND CMAKE_PREFIX_PATH "/opt/AimRT")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0)
  add_library(std::coroutines INTERFACE IMPORTED)
endif()

# 关键：手动find_package所有AimRT需要的依赖
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(unifex REQUIRED)
find_package(Protobuf REQUIRED)
find_package(asio REQUIRED)
find_package(TBB REQUIRED)
find_package(ros2_plugin_proto REQUIRED)
find_package(aimrt_msgs REQUIRED)

# 关闭所有自动下载依赖的开关
option(AIMRT_BUILD_WITH_FMT "Build with fmt" OFF)
option(AIMRT_BUILD_WITH_PROTOBUF "Build with protobuf" OFF)
option(AIMRT_BUILD_WITH_UNIFEX "Build with unifex" OFF)
option(AIMRT_BUILD_WITH_YAMLCPP "Build with yaml-cpp" OFF)
option(AIMRT_BUILD_WITH_JSONCPP "Build with jsoncpp" OFF)
option(AIMRT_BUILD_WITH_TBB "Build with TBB" OFF)
option(AIMRT_BUILD_WITH_ASIO "Build with asio" OFF)
option(AIMRT_BUILD_WITH_ICEORYX "Build with iceoryx" OFF)

# 直接find_package找到已安装的AimRT
find_package(aimrt REQUIRED)

# find_package(rclcpp REQUIRED)

# Add subdirectory
add_subdirectory(src)

# Install
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cmake
  DESTINATION ./
  FILES_MATCHING
  PATTERN "*.cmake")
install(EXPORT ${INSTALL_CONFIG_NAME} DESTINATION lib/cmake/${PROJECT_NAME})
