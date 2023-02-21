cmake_minimum_required(VERSION 3.16)

project("HONServer")

include_directories(
        ${PROJECT_SOURCE_DIR}/include/
)

set(TARGET HONServer)
set(SRC_LIST src/server.c src/table.c lib/rs.c)
add_executable(${TARGET} ${SRC_LIST})
target_link_libraries(${TARGET} pthread)