set(app "ncc")

# Generate the grammar library using the macro
epc_generate_grammar(
    TARGET c_grammar
    GDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/c_grammar.gdl
    HEADER callbacks.h
    BOOTSTRAP_AST
)

add_executable(${app}
  main.c
  ast_node_name.c
  ast_print.c
  c_grammar_ast_actions.c
  debug.c
  ir_gen_error.c
  llvm_ir_generator.c
  scope.c
  symbol_table.c
)

target_compile_options(${app} PRIVATE -O3 -std=gnu11 -Wall -Wextra -Wswitch-enum -pedantic)

# Add LLVM support for linking
# Map LLVM components to library names.
# Focusing on X86 support and core IR components.
llvm_map_components_to_libnames(llvm_libs support core irreader X86 target mc asmparser asmprinter)

target_link_libraries(${app} PRIVATE
    c_grammar
    ${llvm_libs}
    stdc++
)

target_include_directories(${app} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    # Add LLVM include directories if not automatically handled by find_package
    # ${LLVM_INCLUDE_DIRS} # This might be needed if find_package doesn't set it up correctly
)

# Enable LTO if supported
if(lto_supported)
  message(STATUS "LTO enabled for ${app} and c_grammar.")
  set_target_properties(${app} c_grammar PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
  )
endif()
