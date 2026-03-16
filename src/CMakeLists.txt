set(app "c_parser_example")

# Generate the grammar library using the macro
epc_generate_grammar(
    TARGET c_grammar
    GDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/c_grammar.gdl
    HEADER callbacks.h
    BOOTSTRAP_AST
)

add_executable(${app} 
  main.c 
  c_grammar_ast_actions.c
)

target_compile_options(${app} PRIVATE -Wall -Wextra -pedantic)

target_link_libraries(${app} PRIVATE
    c_grammar
)

target_include_directories(${app} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable LTO if supported
if(lto_supported)
  message(STATUS "LTO enabled for ${app} and c_grammar.")
  set_target_properties(${app} c_grammar PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
  )
endif()
