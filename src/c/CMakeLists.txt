option(FORMAT_SOURCES "execute clang-format" ON)
option(USE_COMPILER_RT "use compiler-rt runtime" OFF)
add_subdirectory(3rd/ovbase)
add_subdirectory(3rd/ovutil)

set(YYJSON_DISABLE_NON_STANDARD ON CACHE INTERNAL "")
add_subdirectory(3rd/yyjson)

add_subdirectory(3rd/opus)
target_compile_options(opus PRIVATE -O2)

set(INSTALL_DOCS OFF CACHE INTERNAL "")
set(OP_DISABLE_HTTP ON CACHE INTERNAL "")
set(OP_DISABLE_EXAMPLES ON CACHE INTERNAL "")
set(OP_DISABLE_DOCS ON CACHE INTERNAL "")
add_subdirectory(3rd/ogg)
target_compile_options(ogg PRIVATE -O2)

file(GLOB_RECURSE libopusenc_sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "3rd/libopusenc/src/*.c")
add_library(libopusenc STATIC
  ${libopusenc_sources}
)
target_include_directories(libopusenc
PUBLIC
  3rd/libopusenc/include
)
target_compile_definitions(libopusenc PRIVATE
OUTSIDE_SPEEX
RANDOM_PREFIX=speex
PACKAGE_NAME="libopusenc"
PACKAGE_VERSION="unknown"
)
target_compile_options(libopusenc PRIVATE -O2)
target_link_libraries(libopusenc PUBLIC opus)

file(GLOB_RECURSE opusfile_sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "3rd/opusfile/src/*.c")
add_library(opusfile STATIC
${opusfile_sources}
)
target_include_directories(opusfile
PUBLIC
3rd/opusfile/include
)
target_compile_options(opusfile PRIVATE -O2)
target_link_libraries(opusfile PUBLIC ogg opus)

set(LUA51_PLATFORM i686)
set(LUA51_URL "https://github.com/oov/lua-5.1.5/releases/download/v5.1.5/lua_v5.1.5_${LUA51_PLATFORM}.zip")
string(REGEX MATCH "[^/]+$" LUA51_ARCHIVE_NAME "${LUA51_URL}")
set(LUA51_ARCHIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${LUA51_ARCHIVE_NAME}")
set(LUA51_DIR "${CMAKE_CURRENT_BINARY_DIR}/lua51")
set(LUA51_DLL "${LUA51_DIR}/bin/lua51.dll")
set(LUA51_INCLUDE "${LUA51_DIR}/include")
if(NOT EXISTS "${LUA51_DIR}")
  if(NOT EXISTS "${LUA51_ARCHIVE_PATH}")
    file(DOWNLOAD "${LUA51_URL}" "${LUA51_ARCHIVE_PATH}")
  endif()
  string(REGEX REPLACE "\\.[^.]+$" "" LUA51_ARCHIVE_NOEXT "${LUA51_ARCHIVE_NAME}")
  file(MAKE_DIRECTORY "${LUA51_DIR}")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf ${LUA51_ARCHIVE_PATH}
    WORKING_DIRECTORY "${LUA51_DIR}"
  )
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy "${LUA51_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/lua51.dll"
  )
endif()

if(FORMAT_SOURCES)
  file(GLOB_RECURSE sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "*.h" "*.c")
  list(FILTER sources EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/3rd")
  find_program(CLANG_FORMAT_EXE clang-format)
  add_custom_target(${PROJECT_NAME}-format ALL
    COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${sources}
  )
endif()

add_custom_target(generate_version_h COMMAND
  ${CMAKE_COMMAND}
  -Dlocal_dir="${PROJECT_SOURCE_DIR}"
  -Dinput_file="${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
  -Doutput_file="${CMAKE_CURRENT_BINARY_DIR}/version.h"
  -P "${ovutil_SOURCE_DIR}/src/cmake/version.cmake"
)

# generate i18n.rc
set(LANGCSV "${CMAKE_CURRENT_SOURCE_DIR}/i18n/langs.csv")
file(READ "${LANGCSV}" langs)
string(STRIP ${langs} langs)
string(REPLACE "\n" ";" langs "${langs}")
foreach(line IN LISTS langs)
  if (line MATCHES "^#.*$|^([^,]+),$")
    continue()
  endif()
  if (line MATCHES "^([^,]+),([^,]+)$")
    list(APPEND polist "${CMAKE_CURRENT_SOURCE_DIR}/i18n/${CMAKE_MATCH_1}.po.DO_NOT_EDIT")
  else()
    message(FATAL_ERROR "invalid language difinition: ${line}")
  endif()
endforeach()
add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/i18n.rc"
  COMMAND
    ${CMAKE_COMMAND}
    -Dsrc_dir="${CMAKE_CURRENT_SOURCE_DIR}/i18n"
    -Doutput_dir="${CMAKE_CURRENT_BINARY_DIR}"
    -Drctmpl="${CMAKE_CURRENT_SOURCE_DIR}/i18n.rc.tmpl"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/i18n_rc.cmake"
  WORKING_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n.rc.tmpl"
    ${polist}
)
add_custom_target(generate_rc DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/i18n.rc")

set(is_clang "$<C_COMPILER_ID:Clang>")
set(v16_or_later "$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,16>")
set(v18_or_later "$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,18>")
add_library(subtitler_intf INTERFACE)
target_include_directories(subtitler_intf INTERFACE
  "${CMAKE_CURRENT_BINARY_DIR}" # for version.h
  "${LUA51_INCLUDE}"
)
target_compile_definitions(subtitler_intf INTERFACE
  _WIN32_WINNT=0x0601
  _WINDOWS
  $<$<CONFIG:Release>:NDEBUG>
)
target_compile_options(subtitler_intf INTERFACE
  $<$<AND:$<BOOL:${WIN32}>,$<BOOL:${USE_COMPILER_RT}>>:--rtlib=compiler-rt>
  -mstackrealign
  -Wall
  -Wextra
  -Werror
  -Weverything
  -Wshadow
  -Werror=return-type
  -pedantic-errors
  -Wno-declaration-after-statement
  -Wno-padded
  $<$<AND:${is_clang},${v16_or_later}>:-Wno-unsafe-buffer-usage>
  $<$<AND:${is_clang},${v18_or_later}>:-Wno-switch-default>
  -ffunction-sections
  -fdata-sections
  -msse2
  -ffast-math
  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Release>:-O2>
  -flto
)
target_link_options(subtitler_intf INTERFACE
  -fuse-ld=lld
  -Wl,--gc-sections
  # -Wl,--print-gc-sections
  -Wl,--kill-at
  $<$<CONFIG:Release>:-s>
  -Wl,-delayload,lua51.dll
)
target_link_libraries(subtitler_intf INTERFACE
  comctl32
  ovbase
  ovutil
  yyjson
  libopusenc
  opusfile
  "${LUA51_DLL}"
)

add_custom_target(copy_related_files
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/src/lua" "${CMAKE_BINARY_DIR}/bin/Subtitler"
  COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/README.md" "${CMAKE_BINARY_DIR}/bin/Subtitler.txt"
)

add_library(subtitler_auf SHARED
  aviutl.c
  config.c
  export_audio.c
  i18n.rc
  json2exo.c
  jsoncommon.c
  luactx.c
  opus2json.c
  path.c
  process.c
  processor.c
  raw2opus.c
  sub.c
  subtitler.c
  subtitler.rc
)
set_target_properties(subtitler_auf PROPERTIES
  OUTPUT_NAME "Subtitler.auf"
  PREFIX ""
  SUFFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_dependencies(subtitler_auf generate_version_h copy_related_files)
target_link_libraries(subtitler_auf PRIVATE subtitler_intf)

add_executable(test_export_audio export_audio_test.c export_audio.c)
target_link_libraries(test_export_audio PRIVATE subtitler_intf)
add_test(NAME test_export_audio COMMAND test_export_audio)

add_executable(test_opus2json opus2json_test.c path.c process.c)
target_link_libraries(test_opus2json PRIVATE subtitler_intf)
add_test(NAME test_opus2json COMMAND test_opus2json)

add_executable(test_path path_test.c path.c)
target_link_libraries(test_path PRIVATE subtitler_intf)
add_test(NAME test_path COMMAND test_path)

add_executable(test_subtitler subtitler_test.c)
target_link_libraries(test_subtitler PRIVATE subtitler_intf)
add_test(NAME test_subtitler COMMAND test_subtitler)
