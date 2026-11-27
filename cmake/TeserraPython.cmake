# Finds the Python 3 interpreter that the check targets and the tools.scripts test run.
#
#   teserra_find_python()
#
# Sets Python3_Interpreter_FOUND and Python3_EXECUTABLE in the calling scope, as find_package(Python3) does.
# A Python3_EXECUTABLE in the cache that no longer exists (a deleted virtual environment) would make
# find_package fail, and the checks and the tools.scripts test would silently disappear from the build; it is
# dropped with a warning, so that the search finds another interpreter.

include_guard(GLOBAL)

macro(teserra_find_python)
    if(DEFINED Python3_EXECUTABLE AND NOT EXISTS "${Python3_EXECUTABLE}")
        message(WARNING "Teserra: Python3_EXECUTABLE '${Python3_EXECUTABLE}' does not exist; searching again")
        unset(Python3_EXECUTABLE CACHE)
        unset(Python3_EXECUTABLE)
        unset(_Python3_EXECUTABLE CACHE)
    endif()
    find_package(Python3 COMPONENTS Interpreter)
endmacro()
