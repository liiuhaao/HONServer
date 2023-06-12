cmake_minimum_required(VERSION 3.16)

SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

project("HONServer")

include_directories(
        ${PROJECT_SOURCE_DIR}/include/
)

set(TARGET HONServer)
set(SRC_LIST src/server.c src/fec.c lib/rs.c ./lib/threadpool)
add_executable(${TARGET} ${SRC_LIST})
target_link_libraries(${TARGET} pthread)