cmake_minimum_required(VERSION 3.16)

project("HONServer")

include_directories(
        ${PROJECT_SOURCE_DIR}/include/
)

set(SRC_LIST src/server.c src/table.c lib/rs.c lib/fec.c)
add_executable(HONServer ${SRC_LIST})