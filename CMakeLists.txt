cmake_minimum_required(VERSION 3.0)

project(ZXcas C)

set(AUDIOOUT_SRCS sound/pulse.c)
set(AUDIOOUT_LIBS pulse-simple)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(zxcas
	main.c
	sound.c
	formats/tap.c
	formats/tzx.c
	${AUDIOOUT_SRCS}
)

target_link_libraries(zxcas m ${AUDIOOUT_LIBS})
