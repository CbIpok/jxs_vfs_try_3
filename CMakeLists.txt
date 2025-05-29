cmake_minimum_required(VERSION 3.15)
project(VFWFFmpegWrapper VERSION 1.0 LANGUAGES CXX)

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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)   # auto-export everything

# === Автоматическое скачивание SVT-JPEG-XS в third_party ===
include(FetchContent)  # подключаем модуль FetchContent :contentReference[oaicite:0]{index=0}

FetchContent_Declare(
        svt_jpeg_xs
        GIT_REPOSITORY https://github.com/OpenVisualCloud/SVT-JPEG-XS.git  # репозиторий SVT-JPEG-XS :contentReference[oaicite:1]{index=1}
        GIT_TAG        main  # или укажите нужный тег/ветку
        SOURCE_DIR     ${CMAKE_SOURCE_DIR}/third_party/SVT-JPEG-XS
)
FetchContent_GetProperties(svt_jpeg_xs)
if(NOT svt_jpeg_xs_POPULATED)
    message(STATUS "Cloning SVT-JPEG-XS into ${svt_jpeg_xs_SOURCE_DIR}")
    FetchContent_Populate(svt_jpeg_xs)
endif()

# Подключаем как сабдиректорию (библиотека libSvtJpegxs)
add_subdirectory(
        ${CMAKE_SOURCE_DIR}/third_party/SVT-JPEG-XS
        ${CMAKE_BINARY_DIR}/third_party/SVT-JPEG-XS
        EXCLUDE_FROM_ALL
)

# === Сборка вашей обёртки и тестов ===

add_library(vfw_ffmpeg_wrapper SHARED
        src/vfw_passthrough_codec.cpp
)

# Внутренние заголовки вашего проекта
target_include_directories(vfw_ffmpeg_wrapper PRIVATE src)

# Заголовки SVT-JPEG-XS API (SvtJpegXsEnc.h, SvtJpegXsDec.h находятся в Source/API) :contentReference[oaicite:2]{index=2}
target_include_directories(vfw_ffmpeg_wrapper PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/SVT-JPEG-XS/Source/API
)

# Линкуем с библиотекой SvtJpegxs
target_link_libraries(vfw_ffmpeg_wrapper PRIVATE SvtJpegxs)

# Определения для Unicode
target_compile_definitions(vfw_ffmpeg_wrapper PRIVATE UNICODE _UNICODE)

# Старые Windows-библиотеки
target_link_libraries(vfw_ffmpeg_wrapper PRIVATE
        vfw32
        Shlwapi
)

set_target_properties(vfw_ffmpeg_wrapper PROPERTIES
        PREFIX ""               # убираем префикс "lib" на Windows
        OUTPUT_NAME "jxs_ffmpeg_vfw"
)

# Тестовые приложения
add_executable(test_vfw_enc testEncoder.cpp)
target_link_libraries(test_vfw_enc
        vfw32
        Shlwapi
        vfw_ffmpeg_wrapper
)

add_executable(test_vfw_dec testDecoder.cpp)
target_link_libraries(test_vfw_dec
        vfw32
        Shlwapi
        vfw_ffmpeg_wrapper
)
