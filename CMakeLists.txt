cmake_minimum_required(VERSION 3.20)
project(StlPreviewExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

add_library(stlpreview SHARED
    src/dllmain.cpp
    src/stl_mesh.cpp
    src/renderer.cpp
    src/thumbnail_provider.cpp
    src/preview_handler.cpp
    src/registration.cpp
    src/com_util.cpp
    src/exports.def
)
target_include_directories(stlpreview PRIVATE src)
target_link_libraries(stlpreview PRIVATE
    d3d11 dxgi d3dcompiler ole32 oleaut32 uuid shlwapi advapi32 user32 gdi32
)
set_target_properties(stlpreview PROPERTIES
    OUTPUT_NAME "STLPreview"
    PREFIX ""
)

set_target_properties(stlpreview PROPERTIES ENABLE_EXPORTS OFF)

add_executable(STLPreviewHost WIN32
    src/host/main.cpp
    src/com_util.cpp
)
target_include_directories(STLPreviewHost PRIVATE src)
target_link_libraries(STLPreviewHost PRIVATE ole32 oleaut32 user32 d3d11)
target_compile_definitions(STLPreviewHost PRIVATE STLPREVIEW_HOST_EXE)

add_executable(stlprev-setup
    src/setup/main.cpp
    src/registration.cpp
    src/com_util.cpp
)
target_include_directories(stlprev-setup PRIVATE src)
target_link_libraries(stlprev-setup PRIVATE ole32 advapi32 d3d11 shlwapi)

add_executable(stlprev-test
    tools/test_main.cpp
    src/stl_mesh.cpp
    src/renderer.cpp
    src/com_util.cpp
)
target_include_directories(stlprev-test PRIVATE src)
target_link_libraries(stlprev-test PRIVATE
    d3d11 dxgi d3dcompiler ole32 oleaut32 uuid shlwapi user32 gdi32 advapi32
)

find_program(MAKENSIS_EXECUTABLE
    NAMES makensis
    HINTS "$ENV{SCOOP}/apps/nsis/current" "$ENV{USERPROFILE}/scoop/apps/nsis/current"
)
if(MAKENSIS_EXECUTABLE)
    add_custom_target(installer
        COMMAND "${MAKENSIS_EXECUTABLE}" /V2
                "/DSOURCE_DIR=${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
                "/DOUT_DIR=${CMAKE_SOURCE_DIR}/dist"
                "${CMAKE_SOURCE_DIR}/installer/STLPreview.nsi"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Сборка установщика STLPreview-Setup.exe"
        USES_TERMINAL
    )
    add_dependencies(installer stlpreview STLPreviewHost)
endif()
