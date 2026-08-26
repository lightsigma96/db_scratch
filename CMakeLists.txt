cmake_minimum_required(VERSION 3.16)
project(db_scratch VERSION 1.0 LANGUAGES CXX)

set(CMAKE_BUILD_TYPE Debug)

# -------------------------
# Common
# -------------------------

set(COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage_manager/headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/query_manager/headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/catalog_manager/headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transaction_manager
    ${CMAKE_CURRENT_SOURCE_DIR}/proto
)

find_package(Protobuf CONFIG REQUIRED)

# -------------------------
# DB Daemon
# -------------------------

add_executable(db_daemon
    daemon.cpp

    proto/client_server_common.pb.cc

    src/server_helpers.cpp

    src/storage_manager/access_methods.cpp
    src/storage_manager/buffer_manager.cpp
    src/storage_manager/disk_operator.cpp
    src/storage_manager/heap_writer.cpp
    src/storage_manager/index_writer.cpp
    src/storage_manager/insert.cpp

    src/query_manager/lexer.cpp
    src/query_manager/parser.cpp
    src/query_manager/planner.cpp

    src/catalog_manager/schema_manager.cpp

    src/transaction_manager/transaction_manager.cpp
)

target_include_directories(db_daemon PRIVATE
    ${COMMON_INCLUDE_DIRS}
)

target_compile_features(db_daemon PRIVATE cxx_std_17)

# -------------------------
# TUI Client
# -------------------------

add_executable(db_client
    client.cpp

    src/server_helpers.cpp
    proto/client_server_common.pb.cc

    tui/base_elements.cpp
    tui/components.cpp
    tui/input_handler.cpp
    tui/renderer.cpp
)

target_include_directories(db_client PRIVATE
    ${COMMON_INCLUDE_DIRS}
)

target_compile_features(db_client PRIVATE cxx_std_17)

# -------------------------
# Warnings
# -------------------------

if(MSVC)
    target_compile_options(db_daemon PRIVATE /W4)
    target_compile_options(db_client PRIVATE /W4)
else()
    target_compile_options(db_daemon PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(db_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -------------------------
# Protobuf
# -------------------------

target_link_libraries(db_daemon PRIVATE protobuf::libprotobuf)
target_link_libraries(db_client PRIVATE protobuf::libprotobuf)
