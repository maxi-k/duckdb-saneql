cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME saneql)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

# provide version macros
add_definitions(-DDUCKDB_MAJOR_VERSION=${DUCKDB_MAJOR_VERSION})
add_definitions(-DDUCKDB_MINOR_VERSION=${DUCKDB_MINOR_VERSION})
add_definitions(-DDUCKDB_PATCH_VERSION=${DUCKDB_PATCH_VERSION})

# include saneql
include_directories(SYSTEM ${CMAKE_CURRENT_LIST_DIR}/saneql)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/saneql)
install(
  TARGETS saneql_static
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
)

# saneql extension
include_directories(src/include)

set(EXTENSION_SOURCES src/saneql_extension.cpp)
set(PARAMETERS "-warnings")

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} saneql_static)
target_link_libraries(${LOADABLE_EXTENSION_NAME} saneql_static)

# set correct c++ standars for saneql and saneql-duckdb
set_property(TARGET saneql_static PROPERTY CXX_STANDARD 20)
set_property(TARGET ${EXTENSION_NAME} PROPERTY CXX_STANDARD 20)
set_property(TARGET ${LOADABLE_EXTENSION_NAME} PROPERTY CXX_STANDARD 20)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")


message(STATUS "[duckdb-saneql] settings")
message(STATUS "     CMAKE_CXX_COMPILER         = ${CMAKE_CXX_COMPILER}")
message(STATUS "     CMAKE_C_COMPILER           = ${CMAKE_C_COMPILER}")
message(STATUS "     CMAKE_CXX_FLAGS            = ${CMAKE_CXX_FLAGS}")
message(STATUS "     CMAKE_CXX_FLAGS_RELEASE    = ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "     CMAKE_BUILD_TYPE           = ${CMAKE_BUILD_TYPE}")
message(STATUS "     CMAKE_CURRENT_SOURCE_DIR   = ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "     CMAKE_CURRENT_BINARY_DIR   = ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "     INSTALL_LIB_DIR            = ${INSTALL_LIB_DIR}")
message(STATUS "     DUCKDB_VERSION             = ${DUCKDB_VERSION}")
message(STATUS "     DUCKDB_MAJOR_VERSION       = ${DUCKDB_MAJOR_VERSION}")
message(STATUS "     DUCKDB_MINOR_VERSION       = ${DUCKDB_MINOR_VERSION}")
message(STATUS "     DUCKDB_PATCH_VERSION       = ${DUCKDB_PATCH_VERSION}")
message(STATUS "     DUCKDB_EXPORT_SET          = ${DUCKDB_EXPORT_SET}")
