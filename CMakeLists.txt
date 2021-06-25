cmake_minimum_required(VERSION 3.15)
project(RDMA_KV_store C)

set(PROJECT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Ofast -ffast-math -funroll-loops -march=native -Og -ggdb -Wall -Wextra -fsanitize=address \
		 -fno-omit-frame-pointer -pthread")

find_library(IBVERBS_LIBRARY ibverbs HINTS /home/jtroo/Downloads/rdma-core/build/lib) # Hard coded for SoftiWARP
find_library(RDMACM_LIBRARY rdmacm HINTS /home/jtroo/Downloads/rdma-core/build/lib)

link_libraries(pthread ${IBVERBS_LIBRARY} ${RDMACM_LIBRARY} -ldl)

include_directories("${PROJECT_SOURCE_DIR}")
include_directories("/usr/include/rdma")
include_directories("/usr/include/infiniband")

file(GLOB SERVER_SOURCE "src/server/*.h" "src/server/*.c")
file(GLOB CLIENT_SOURCE "src/client/*_client_utils.h" "src/client/*_client_utils.c" "src/client/client.c" "src/client/client.h")
file(GLOB BENCHMARK_SOURCE "src/benchmarking/*.h" "src/benchmarking/*.c")

add_executable(kv_server ${SERVER_SOURCE} src/common.h src/common.c src/rdma_common.c src/rdma_common.h src/server/ud_server_utils.c src/server/ud_server_utils.h src/server/uc_server_utils.c src/server/uc_server_utils.h)
add_executable(benchmark ${BENCHMARK_SOURCE} ${CLIENT_SOURCE} src/common.h src/common.c src/rdma_common.c src/rdma_common.h)

set_property(TARGET kv_server PROPERTY C_STANDARD 99)
set_property(TARGET benchmark PROPERTY C_STANDARD 99)

