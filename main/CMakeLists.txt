set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

idf_component_register(
    SRCS            "main.c"
                    "synth.c"
                    "midi_input.c"
                    "display.cpp"
    INCLUDE_DIRS    "${CMAKE_SOURCE_DIR}/gfx/src"
                    "${CMAKE_SOURCE_DIR}/ili9341"
)
