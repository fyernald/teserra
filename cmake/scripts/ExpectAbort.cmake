# Runs TESERRA_PROGRAM and fails unless it ends abnormally and its output matches the regex
# TESERRA_EXPECT. Used by teserra_add_abort_test() in cmake/TeserraTesting.cmake.
#
#   cmake -DTESERRA_PROGRAM=<exe> -DTESERRA_EXPECT=<regex> -P ExpectAbort.cmake

execute_process(
    COMMAND "${TESERRA_PROGRAM}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
message("${output}")

if(result STREQUAL "0")
    message(FATAL_ERROR "ExpectAbort: '${TESERRA_PROGRAM}' exited normally, expected an abnormal end")
endif()
if(NOT output MATCHES "${TESERRA_EXPECT}")
    message(FATAL_ERROR "ExpectAbort: the output does not match '${TESERRA_EXPECT}'")
endif()
message(STATUS "ExpectAbort: ended abnormally (${result}) with the expected output")
