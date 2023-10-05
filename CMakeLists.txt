cmake_minimum_required(VERSION 3.0)

project(PC60cas C)

set(AUDIOOUT_SRCS sound/pulse.c)
set(AUDIOOUT_LIBS pulse-simple)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(pc60cas
	main.c
	sound.c
	formats/zx_tap.c
	formats/zx_tzx.c
	formats/pc60_cas.c
	formats/bk_bin.c
	${AUDIOOUT_SRCS}
)

target_link_libraries(pc60cas m ${AUDIOOUT_LIBS})
