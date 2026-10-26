# Packs QLaunch.exe and the assets into a zip ready to upload to a GitHub release.
# Run after each build by CMakeLists.txt:
#   cmake -DCONFIG=<config> -DEXE=<exe> -DASSETS=<assets dir> -DSTAGE=<temp dir> -DZIP=<zip> -P package.cmake
# Debug builds are not packed.

if(NOT CONFIG MATCHES "^(Release|MinSizeRel)$")
    return()
endif()

# Lay out the files in an empty folder first, so files a test run left next to the exe (settings, buttons,
# icon cache) stay out of the zip
file(REMOVE_RECURSE "${STAGE}")
file(COPY "${ASSETS}/" DESTINATION "${STAGE}")
file(COPY "${EXE}" DESTINATION "${STAGE}")

file(GLOB entries RELATIVE "${STAGE}" "${STAGE}/*")
get_filename_component(dir "${ZIP}" DIRECTORY)
file(MAKE_DIRECTORY "${dir}")
file(REMOVE "${ZIP}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${ZIP}" --format=zip ${entries}
                WORKING_DIRECTORY "${STAGE}" RESULT_VARIABLE failed)
if(failed)
    message(FATAL_ERROR "Could not create ${ZIP}")
endif()
message(STATUS "Release package: ${ZIP}")
