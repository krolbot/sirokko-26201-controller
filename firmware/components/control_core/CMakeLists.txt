idf_component_register(
    SRCS "control.cpp" "button.cpp" "operator.cpp" "ui.cpp" "service.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
