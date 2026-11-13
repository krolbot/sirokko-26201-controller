idf_component_register(SRCS "control.cpp" "profile.cpp" "regulator.cpp" "combustion.cpp" INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
