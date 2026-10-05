execute_process(COMMAND "${DRIVER}" INPUT_FILE "${INPUT}"
	RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
	TIMEOUT 10)
set(patterns "ok loaded=1" "ok bound=1"
	"ok exists=1 state=0 code=0 admitted=0 peer=0 start=1"
	"ok exists=1 state=2 code=0 admitted=1 peer=0 start=0"
	"ok exists=1 state=2 code=0 admitted=1 peer=1 start=0"
	"ok deleted=1\nok exists=0")
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Script fixture failed: ${result}: ${error}")
endif()
foreach(pattern IN LISTS patterns)
	string(FIND "${output}" "${pattern}" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "Missing ${pattern}:\n${output}\n${error}")
	endif()
endforeach()
