cmake_minimum_required(VERSION 3.12)

project(katrix
				VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require C++ standard to be supported")
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "compile as PIC by default")

set(SOURCE_FILES
	src/main.cpp
	src/server.cpp
)

add_library(matrix_client SHARED IMPORTED)
set_property(TARGET matrix_client PROPERTY IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/mtxclient/build/libmatrix_clientd.so")

add_executable(katrix_bot ${SOURCE_FILES})
add_library(katrix STATIC ${SOURCE_FILES})

find_package(fmt CONFIG REQUIRED)
set(KLOGGER "${CMAKE_SOURCE_DIR}/third_party/klogger/build/libklog.a")

include(FetchContent)
FetchContent_Declare(
	coeurl
	GIT_REPOSITORY https://nheko.im/Nheko-Reborn/coeurl.git
	GIT_TAG        v0.3.0
	)
FetchContent_MakeAvailable(coeurl)

include_directories(katrix
	#third_party/coeurl/include/coeurl/
	third_party/mtxclient/include
	third_party/kutils/include
	third_party/kproto/include
	third_party/klogger/src
	src
)

include_directories(katrix_bot
	#third_party/coeurl/include/coeurl/
	third_party/mtxclient/include
	third_party/kutils/include
	third_party/klogger/src
	src
)

target_link_libraries(katrix     PUBLIC coeurl::coeurl matrix_client -lbsd zmq fmt::fmt ${KLOGGER})
target_link_libraries(katrix_bot PUBLIC coeurl::coeurl matrix_client -lbsd zmq fmt::fmt ${KLOGGER})

