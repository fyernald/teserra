# Runs TESERRA_PROGRAM and fails unless it ends abnormally (a non-zero exit code or a crash) and its output
# matches the regex TESERRA_EXPECT. Used by teserra_add_abort_test() in cmake/TeserraTesting.cmake and by
# tests of command-line tools, which pass the program's arguments as the list TESERRA_ARGS.
#
#   cmake -DTESERRA_PROGRAM=<exe> [-DTESERRA_ARGS=<arg>;...] -DTESERRA_EXPECT=<regex> -P ExpectAbort.cmake

execute_process(
    COMMAND "${TESERRA_PROGRAM}" ${TESERRA_ARGS}
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
