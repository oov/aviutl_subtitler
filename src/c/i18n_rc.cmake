find_program(MSGFMT msgfmt REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)
file(READ "langs.csv" langs)
string(STRIP ${langs} langs)
string(REPLACE "\n" ";" langs "${langs}")
foreach(line IN LISTS langs)
  if (line MATCHES "^#.*$|^([^,]+),$")
    continue()
  endif()
  if (line MATCHES "^([^,]+),([^,]+)$")
    execute_process(COMMAND "${MSGFMT}" --no-hash --output-file "${output_dir}/${CMAKE_MATCH_1}.mo" "${src_dir}/${CMAKE_MATCH_1}.po.DO_NOT_EDIT")
    if (NOT EXISTS "${output_dir}/${CMAKE_MATCH_1}.mo")
      message(FATAL_ERROR "failed to compile ${CMAKE_MATCH_1}.po")
    endif()
    math(EXPR mainlang "${CMAKE_MATCH_2} & 0x3ff")
    math(EXPR sublang "${CMAKE_MATCH_2} >> 10")
    list(APPEND rclist "LANGUAGE ${mainlang}, ${sublang}")
    list(APPEND rclist "MO RCDATA \"${CMAKE_MATCH_1}.mo\"")
  else()
    message(FATAL_ERROR "invalid language difinition: ${line}")
  endif()
endforeach()
string(REPLACE ";" "\n" rclist "${rclist}")
file(REMOVE "${output_dir}/i18n.rc")
configure_file("${rctmpl}" "${output_dir}/i18n.rc" @ONLY)
