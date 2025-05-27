cmake_minimum_required(VERSION 3.15)
project(VFWFFmpegWrapper VERSION 1.0 LANGUAGES CXX)


# ——————————————————————————————
# 2) Включаем подробные debug-символы для MSVC
# ——————————————————————————————
if(MSVC)
    # /Zi генерирует PDB, /DEBUG при линковке
    add_compile_options(
            $<$<CONFIG:Debug>:/Zi>
            $<$<CONFIG:Debug>:/Od>      # отключаем оптимизации
    )
    add_link_options(
            $<$<CONFIG:Debug>:/DEBUG>
    )
endif()
# Для MinGW/CLang под Ninja/Make: CMakeProfile в CLion в Debug и так
# добавит -g, -O0 автоматически.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)   # <—— auto-export everything

add_library(vfw_ffmpeg_wrapper SHARED
        src/vfw_passthrough_codec.cpp
)
target_include_directories(vfw_ffmpeg_wrapper PRIVATE src)
if(WIN32)
    target_compile_definitions(vfw_ffmpeg_wrapper PRIVATE UNICODE _UNICODE)
    target_link_libraries(vfw_ffmpeg_wrapper vfw32 Shlwapi)
endif()
set_target_properties(vfw_ffmpeg_wrapper PROPERTIES
        PREFIX ""
        OUTPUT_NAME "jxs_ffmpeg_vfw"
)

add_executable(test_vfw_enc testEncoder.cpp)
target_link_libraries(test_vfw_enc vfw32 Shlwapi vfw_ffmpeg_wrapper)

add_executable(test_vfw_dec testDecoder.cpp)
target_link_libraries(test_vfw_dec vfw32 Shlwapi vfw_ffmpeg_wrapper)