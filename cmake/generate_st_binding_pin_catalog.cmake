cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_program(PYTHON_EXECUTABLE NAMES python3 python REQUIRED)

set(arguments
    "${PLCOPEN_ROOT}/tools/generate_st_binding_pin_catalog.py")
if(PLCOPEN_UPDATE_GENERATED)
    list(APPEND arguments --update)
endif()
if(PLCOPEN_ALLOW_UNRESOLVED)
    list(APPEND arguments --allow-unresolved)
endif()

execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" ${arguments}
    WORKING_DIRECTORY "${PLCOPEN_ROOT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "ST binding pin catalog generation/verification failed:\n${output}${error}")
endif()
string(STRIP "${output}" output)
message(STATUS "${output}")

set(type_arguments
    "${PLCOPEN_ROOT}/tools/generate_st_binding_types.py")
if(PLCOPEN_UPDATE_GENERATED)
    list(APPEND type_arguments --update)
endif()
execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" ${type_arguments}
    WORKING_DIRECTORY "${PLCOPEN_ROOT}"
    RESULT_VARIABLE type_result
    OUTPUT_VARIABLE type_output
    ERROR_VARIABLE type_error)
if(NOT type_result EQUAL 0)
    message(FATAL_ERROR
            "ST binding type generation/verification failed:\n${type_output}${type_error}")
endif()
string(STRIP "${type_output}" type_output)
message(STATUS "${type_output}")
