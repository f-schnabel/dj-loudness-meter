cmake_minimum_required(VERSION 3.24)
project(DjLoudnessMeter LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(meter STATIC src/meter.c)
target_include_directories(meter PUBLIC src)
target_compile_options(meter PRIVATE /W4 /WX)

add_executable(DjLoudnessMeter WIN32
  src/main.c src/app.c src/audio.c src/settings.c src/system_metrics.c src/resource.rc)
set(EBUR128_RELEASE_LIBRARY "${CMAKE_SOURCE_DIR}/artifacts/vcpkg_installed/x64-windows-static/lib/ebur128.lib")
set(EBUR128_DEBUG_LIBRARY "${CMAKE_SOURCE_DIR}/artifacts/vcpkg_installed/x64-windows-static/debug/lib/ebur128.lib")
if(NOT EXISTS "${EBUR128_RELEASE_LIBRARY}" OR NOT EXISTS "${EBUR128_DEBUG_LIBRARY}")
  message(FATAL_ERROR "Static libebur128 not found. Run scripts/Build-Native.ps1.")
endif()
target_include_directories(DjLoudnessMeter PRIVATE src)
target_compile_definitions(DjLoudnessMeter PRIVATE UNICODE _UNICODE COBJMACROS WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(DjLoudnessMeter PRIVATE /W4 /WX /utf-8)
target_link_options(DjLoudnessMeter PRIVATE "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/app.manifest")
target_link_libraries(DjLoudnessMeter PRIVATE meter
  optimized "${EBUR128_RELEASE_LIBRARY}" debug "${EBUR128_DEBUG_LIBRARY}"
  ole32 oleaut32 uuid propsys comctl32 avrt pdh dwmapi uxtheme advapi32)

add_executable(native_tests tests/native_tests.c)
target_include_directories(native_tests PRIVATE src)
target_compile_options(native_tests PRIVATE /W4 /WX)
target_link_libraries(native_tests PRIVATE meter)

enable_testing()
add_test(NAME native_tests COMMAND native_tests)
