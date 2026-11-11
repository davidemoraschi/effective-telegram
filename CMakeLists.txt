cmake_minimum_required(VERSION 3.20)

project(sqlite_mcp LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SQLITE_MCP_USE_SYSTEM_SQLITE "Use system SQLite instead of vendor amalgamation" OFF)

set(SQLITE_MCP_COMMON_SOURCES
	src/main.c
	src/mcp.c
	src/json.c
	src/sqlite_ops.c
	src/worker.c
)

if(SQLITE_MCP_USE_SYSTEM_SQLITE)
	find_package(SQLite3 REQUIRED)
	set(SQLITE_MCP_SQLITE_DEFINES SQLITE_MCP_USE_SYSTEM_SQLITE)
	set(SQLITE_MCP_SQLITE_LINK_LIBS SQLite::SQLite3)
else()
	if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3.c" OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3.h")
		message(FATAL_ERROR "Missing vendor/sqlite3.c or vendor/sqlite3.h")
	endif()
	set(SQLITE_MCP_SQLITE_SOURCES vendor/sqlite3.c)
endif()

find_package(Threads REQUIRED)

add_executable(sqlite-mcp
	${SQLITE_MCP_COMMON_SOURCES}
	${SQLITE_MCP_SQLITE_SOURCES}
)

target_include_directories(sqlite-mcp PRIVATE
	src
	vendor
)

target_compile_definitions(sqlite-mcp PRIVATE
	${SQLITE_MCP_SQLITE_DEFINES}
)

target_link_libraries(sqlite-mcp PRIVATE
	Threads::Threads
	${SQLITE_MCP_SQLITE_LINK_LIBS}
)

add_executable(sqlite-mcp-tool-tester
	src/tool_tester.c
)

target_link_libraries(sqlite-mcp-tool-tester PRIVATE
	Threads::Threads
)

set(SQLITE_MCP_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set_target_properties(sqlite-mcp sqlite-mcp-tool-tester PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${SQLITE_MCP_RUNTIME_OUTPUT_DIR}"
)

if(MSVC)
	target_compile_options(sqlite-mcp PRIVATE /W4)
	target_compile_options(sqlite-mcp-tool-tester PRIVATE /W4)
	target_compile_definitions(sqlite-mcp PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_definitions(sqlite-mcp-tool-tester PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(sqlite-mcp PRIVATE -Wall -Wextra -Wpedantic)
	target_compile_options(sqlite-mcp-tool-tester PRIVATE -Wall -Wextra -Wpedantic)
endif()
