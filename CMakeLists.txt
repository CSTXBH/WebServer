cmake_minimum_required(VERSION 3.0)

project(WEBSERVER)

set(CMAKE_CXX_FLAGS "-std=c++11") 
set(CMAKE_BUILD_TYPE "Debug")

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/threadpool)
include_directories(${CMAKE_SOURCE_DIR}/timer)
include_directories(${CMAKE_SOURCE_DIR}/mysql_conn)
include_directories(${CMAKE_SOURCE_DIR}/http_conn)

add_executable(server main.cpp http_conn/http_conn.cpp mysql_conn/mysql_conn.cpp)
target_link_libraries(server pthread mysqlclient)
