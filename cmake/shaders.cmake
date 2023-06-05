find_program(
    GLSL_LANG_VALIDATOR
    glslangValidator 
    DOC "glslangValidator for compiling the shaders"
    REQUIRED
)

function(compile_shaders target extern_directories)
    message(STATUS ${target})
    set(shaders_header "${CMAKE_CURRENT_BINARY_DIR}/include/shaders.hpp")
    file(WRITE ${shaders_header} "#pragma once\n#include <cstdint>\n#include <liblava/core/data.hpp>\n")

    set(shaders_source "${CMAKE_CURRENT_BINARY_DIR}/shaders.cpp")
    file(WRITE ${shaders_source} "#include \"shaders.hpp\"\n")
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
    target_sources(${target} PRIVATE ${shaders_header} ${shaders_source})

    get_target_property(sources ${target} SOURCES)
    foreach (source ${sources})
        get_filename_component(source_directory ${source} DIRECTORY)
        get_filename_component(source_name ${source} NAME_WE)
        get_filename_component(source_extension ${source} EXT)

        if (source_extension STREQUAL ".vert" OR
            source_extension STREQUAL ".tesc" OR
            source_extension STREQUAL ".tese" OR
            source_extension STREQUAL ".geom" OR
            source_extension STREQUAL ".frag" OR
            source_extension STREQUAL ".comp"
        )
            string(SUBSTRING ${source_extension} 1 -1 source_extension_without_dot)
            set(var_name "${source_name}_${source_extension_without_dot}")
            set(generated_file "${CMAKE_CURRENT_BINARY_DIR}/${var_name}.spv")
            set(depfile "${CMAKE_CURRENT_BINARY_DIR}/${var_name}.dep")

            add_custom_command(
                OUTPUT ${generated_file}
                COMMAND ${GLSL_LANG_VALIDATOR} -I${extern_directories} ${source} "--vn" ${var_name} "-V" "-o" ${generated_file} "--depfile" ${depfile}
                MAIN_DEPENDENCY ${source}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                DEPFILE ${depfile}
            )
            file(APPEND ${shaders_header} "extern const std::uint32_t ${var_name}[];\n")
            file(APPEND ${shaders_header} "extern const lava::cdata ${var_name}_cdata;\n")
            file(APPEND ${shaders_source} "#include \"${var_name}.spv\"\n")
            file(APPEND ${shaders_source} "const lava::cdata ${var_name}_cdata(${var_name}, sizeof(${var_name}));\n")
            target_sources(${target} PRIVATE ${generated_file})
        endif ()
    endforeach()
endfunction(compile_shaders)
