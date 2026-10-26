cmake_minimum_required(VERSION 3.20)
project(QLaunch LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Link the CRT statically (/MT) so the exe needs no VC++ runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The presets put the exe in out/bin/<preset> (x64-release, x86-release...) and intermediate files
# in out/temp/<preset> (see CMakePresets.json); without a preset it goes to out/bin/<config>
if(QL_OUTPUT_DIR)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${QL_OUTPUT_DIR}")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/bin/$<CONFIG>")
endif()

file(GLOB_RECURSE QL_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(QLaunch WIN32 ${QL_SOURCES} res/app.rc)
# The resource compiler doesn't report the files app.rc pulls in, so tell CMake to rebuild
# the resources when the icon or the manifest changes
set_source_files_properties(res/app.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/res/app.ico;${CMAKE_SOURCE_DIR}/res/app.manifest")

target_include_directories(QLaunch PRIVATE src res)
target_compile_definitions(QLaunch PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0601 _HAS_EXCEPTIONS=0)
target_link_libraries(QLaunch PRIVATE
    user32 gdi32 msimg32 shell32 ole32 comctl32 comdlg32 advapi32 shlwapi wininet imm32)

if(MSVC)
    # No exceptions, no RTTI, read sources as UTF-8
    target_compile_options(QLaunch PRIVATE /utf-8 /W4 /EHs-c- /GR- /permissive-
        $<$<CONFIG:Release,MinSizeRel>:/O1 /GL /Gy /GS->)
    # The manifest is embedded by app.rc; don't let the linker generate one
    # wininet only downloads website icons: load it on first use, not at every start
    target_link_libraries(QLaunch PRIVATE delayimp)
    target_link_options(QLaunch PRIVATE /MANIFEST:NO /DELAYLOAD:wininet.dll
        $<$<CONFIG:Release,MinSizeRel>:/LTCG /OPT:REF /OPT:ICF /DEBUG:NONE>)
else()
    # MinGW: mainly used to check that the code compiles outside Windows
    target_compile_options(QLaunch PRIVATE -municode -Wall -fno-exceptions -fno-rtti
        $<$<CONFIG:Release,MinSizeRel>:-Os -ffunction-sections -fdata-sections>)
    target_link_options(QLaunch PRIVATE -municode -static
        $<$<CONFIG:Release,MinSizeRel>:-s -Wl,--gc-sections>)
endif()

# Copy the language files, skins and icons next to the exe so debug runs work as-is
add_custom_command(TARGET QLaunch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/dist $<TARGET_FILE_DIR:QLaunch>)
