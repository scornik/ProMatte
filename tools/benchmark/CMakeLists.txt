add_executable(promatte-bench
    main.cpp
    image_io.cpp
)
target_link_libraries(promatte-bench PRIVATE promatte-core promatte::json)
promatte_set_warnings(promatte-bench)
set_target_properties(promatte-bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_command(TARGET promatte-bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROMATTE_RUNTIME_DLLS} "$<TARGET_FILE_DIR:promatte-bench>"
    COMMAND_EXPAND_LISTS)
