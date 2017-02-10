cmake_minimum_required(VERSION 3.5)
project(masaj)

set (CMAKE_CXX_STANDARD 11)

add_definitions(-DMG_ENABLE_IPV6)

set(MASAJ_LINKLIBS)


if(WIN32)
	add_compile_options(/EHsc)
	list(APPEND MASAJ_LINKLIBS ws2_32)
endif(WIN32)


set(src_masaj JsonNode.cpp JsonHttpServer.cpp JsonHttpClient.cpp)
add_library(mongoose mongoose/mongoose.c frozen/frozen.c)


add_library(masaj ${src_masaj})

target_include_directories (masaj PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(masaj pclog mongoose ${MASAJ_LINKLIBS})
