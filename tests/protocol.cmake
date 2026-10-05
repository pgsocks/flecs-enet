execute_process(COMMAND "${DRIVER}" INPUT_FILE "${INPUT}"
	RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
	TIMEOUT 10)
set(expected "ok imported=1\nok configured=1\nok exists=0\n")
string(APPEND expected "ok progressed=1\nok imported=1\nok configured=1\n")
string(APPEND expected "ok imported=1\n")
string(APPEND expected "ok hosts=0 packets=0 sends=0 protocol=0 tuning=0")
string(APPEND expected " flags=0 channel=0 size=0\nok finalized=1\n")
string(APPEND expected "ok imported=1\nok finalized=1\nok quit=1\n")
if(NOT result EQUAL 0 OR NOT output STREQUAL expected)
	message(FATAL_ERROR "Fixture protocol failure: ${result}\n"
		"stdout=${output}\nstderr=${error}")
endif()
