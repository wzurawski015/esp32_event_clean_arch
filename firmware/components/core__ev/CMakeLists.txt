idf_component_register(
  SRCS "core_ev.c"
  INCLUDE_DIRS "include"
  REQUIRES
    freertos
    core__leasepool
)

# --- Doxygen target dla core__ev (opcjonalnie, z Graphviz) ---
find_program(DOXYGEN_EXECUTABLE doxygen)
if(DOXYGEN_EXECUTABLE AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/Doxyfile.in)
  set(DOXYGEN_INPUT_DIR  ${CMAKE_CURRENT_LIST_DIR})
  set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
  configure_file(${CMAKE_CURRENT_LIST_DIR}/Doxyfile.in
                 ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(docs_core__ev
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DOXYGEN_OUTPUT_DIR}
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating Doxygen (core__ev) with Graphviz"
    VERBATIM)
endif()
