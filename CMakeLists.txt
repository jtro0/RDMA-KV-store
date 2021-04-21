# Merged CMake from https://github.com/animeshtrivedi/rdma-example

cmake_minimum_required(VERSION 3.15)
project(RDMA_KV_store C)

set(PROJECT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(SERVER_SOURCE ${PROJECT_SOURCE_DIR}/server)
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Ofast -ffast-math -funroll-loops -march=native -Og -ggdb -Wall -Wextra -fsanitize=address \
		 -fno-omit-frame-pointer -pthread")

find_library(IBVERBS_LIBRARY ibverbs HINTS /home/jtroo/Downloads/rdma-core/build/lib)
find_library(RDMACM_LIBRARY rdmacm HINTS /home/jtroo/Downloads/rdma-core/build/lib)

link_libraries(pthread ${IBVERBS_LIBRARY} ${RDMACM_LIBRARY} -ldl)

include_directories("${PROJECT_SOURCE_DIR}")
include_directories("/usr/include/rdma")
include_directories("/usr/include/infiniband")

add_executable(RDMA_KV_store ${SERVER_SOURCE}/kvstore.c ${SERVER_SOURCE}/kvstore.h ${SERVER_SOURCE}/hash.c ${SERVER_SOURCE}/hash.h ${PROJECT_SOURCE_DIR}/common.h  ${PROJECT_SOURCE_DIR}/common.c src/server/server_utils.c src/server/server_utils.h src/server/request_dispatcher.c src/server/request_dispatcher.h src/server/parser.c src/server/parser.h)