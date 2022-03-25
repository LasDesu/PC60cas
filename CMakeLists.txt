cmake_minimum_required(VERSION 3.0)

project(PC60cas C)

add_executable(pc60cas
	main.c
	cas.c
	sound.c
	tape.h
)

target_link_libraries(pc60cas m)
