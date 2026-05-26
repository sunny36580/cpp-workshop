# Set namespace
set_root_namespace("Logger")



# module
add_subdirectory(module/executor_module)
add_subdirectory(module/executor_co_module)
add_subdirectory(module/executor_co_loop_module)
add_subdirectory(module/real_time_module)

# pkg
add_subdirectory(pkg/executor_pkg)

# install
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(CUR_INSTALL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/install/linux)
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  set(CUR_INSTALL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/install/win)
else()
  message(FATAL_ERROR "Unsupport os")
endif()

install(
  DIRECTORY ${CUR_INSTALL_SOURCE_DIR}/bin
  DESTINATION ./
  USE_SOURCE_PERMISSIONS)

# build all
add_custom_target(
  Logger_build_all ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CUR_INSTALL_SOURCE_DIR}/bin ${CMAKE_BINARY_DIR}
  DEPENDS aimrt::runtime::main
          Logger::executor_pkg)


