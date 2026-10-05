add_library(flecs_enet_instrumented STATIC
	../src/module.c ../src/request.c ../src/transport.c ../src/events.c
	../src/ownership.c ../src/reflection.c ../src/startup.c test_boundary.c)
target_compile_definitions(flecs_enet_instrumented PRIVATE FLECS_ENET_TESTING)
target_include_directories(flecs_enet_instrumented PUBLIC ../include
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(flecs_enet_instrumented PUBLIC flecs_enet_flecs enet)
flecs_enet_warnings(flecs_enet_instrumented)
foreach(target IN ITEMS flecs_enet_contract flecs_enet_fixture
	flecs_enet_infrastructure)
	if(target STREQUAL flecs_enet_contract)
		set(source contract.c)
	elseif(target STREQUAL flecs_enet_fixture)
		set(source driver.c)
	else()
		set(source infrastructure.c)
	endif()
	add_executable(${target} ${source} fixture.c)
	target_link_libraries(${target} PRIVATE flecs_enet_instrumented)
	flecs_enet_warnings(${target})
endforeach()
add_executable(flecs_enet_udp_fixture driver.c fixture.c)
target_compile_definitions(flecs_enet_udp_fixture PRIVATE FLECS_ENET_REAL_UDP)
target_link_libraries(flecs_enet_udp_fixture PRIVATE FlecsEnet::flecs_enet enet)
flecs_enet_warnings(flecs_enet_udp_fixture)
add_executable(flecs_enet_loopback loopback.c)
target_link_libraries(flecs_enet_loopback PRIVATE FlecsEnet::flecs_enet enet)
flecs_enet_warnings(flecs_enet_loopback)
add_executable(flecs_enet_link_c link.c)
target_link_libraries(flecs_enet_link_c PRIVATE FlecsEnet::flecs_enet)
flecs_enet_warnings(flecs_enet_link_c)
add_test(NAME flecs_enet.link_c COMMAND flecs_enet_link_c)
add_test(NAME flecs_enet.boundary COMMAND flecs_enet_infrastructure)
add_test(NAME flecs_enet.fixture_protocol COMMAND ${CMAKE_COMMAND}
	-DDRIVER=$<TARGET_FILE:flecs_enet_fixture>
	-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/protocol.txt
	-P ${CMAKE_CURRENT_SOURCE_DIR}/protocol.cmake)
set_tests_properties(flecs_enet.link_c flecs_enet.boundary
	flecs_enet.fixture_protocol PROPERTIES LABELS infrastructure TIMEOUT 15)
if(FLECS_ENET_CHECK_CXX)
	add_executable(flecs_enet_link_cxx link.cpp)
	target_compile_features(flecs_enet_link_cxx PRIVATE cxx_std_17)
	set_target_properties(flecs_enet_link_cxx PROPERTIES CXX_EXTENSIONS OFF)
	target_link_libraries(flecs_enet_link_cxx PRIVATE FlecsEnet::flecs_enet)
	flecs_enet_warnings(flecs_enet_link_cxx)
	add_test(NAME flecs_enet.link_cxx COMMAND flecs_enet_link_cxx)
	set_tests_properties(flecs_enet.link_cxx PROPERTIES
		LABELS infrastructure TIMEOUT 15)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS cases.def)
file(STRINGS cases.def cases REGEX "^CASE\\([a-z0-9_]+\\)$")
foreach(case IN LISTS cases)
	string(REGEX REPLACE "^CASE\\((.*)\\)$" "\\1" name "${case}")
	add_test(NAME flecs_enet.${name} COMMAND flecs_enet_contract ${name})
	set_tests_properties(flecs_enet.${name} PROPERTIES
		LABELS "contract;unit" TIMEOUT 15)
endforeach()
foreach(name IN ITEMS probe interoperability fragmentation)
	add_test(NAME flecs_enet.udp_${name}
		COMMAND flecs_enet_loopback ${name})
	set_tests_properties(flecs_enet.udp_${name} PROPERTIES
		LABELS udp TIMEOUT 15)
endforeach()
if(FLECS_ENET_CUCUMBER)
	find_program(FLECS_ENET_RUBY NAMES ruby ruby32 ruby33 ruby34)
	find_program(FLECS_ENET_BUNDLE NAMES bundle bundler)
	if(NOT FLECS_ENET_RUBY OR NOT FLECS_ENET_BUNDLE)
		message(WARNING "Cucumber needs Ruby >=3.2 and Bundler. "
			"Tests stay registered and report missing tools.")
	endif()
	foreach(group IN ITEMS contract udp)
		add_test(NAME flecs_enet.cucumber_${group}
			COMMAND ${CMAKE_COMMAND}
			-DRUBY=${FLECS_ENET_RUBY} -DBUNDLE=${FLECS_ENET_BUNDLE}
			-DFIXTURE=$<TARGET_FILE:flecs_enet_fixture>
			-DUDP_FIXTURE=$<TARGET_FILE:flecs_enet_udp_fixture>
			-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/..
			-DGROUP=${group}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cucumber.cmake)
		set_tests_properties(flecs_enet.cucumber_${group} PROPERTIES
			LABELS "cucumber;${group}" TIMEOUT 120)
	endforeach()
endif()
add_executable(flecs_enet_script script.c fixture.c)
target_link_libraries(flecs_enet_script PRIVATE flecs_enet_instrumented)
flecs_enet_warnings(flecs_enet_script)
target_compile_definitions(flecs_enet_script PRIVATE
	FLECS_ENET_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/../examples/scripts")
foreach(case IN ITEMS runtime workflow examples pressure deletion host_restart
	connection_restart tuning invalid cancellation schema byte_width)
	add_test(NAME flecs_enet.script_${case}
		COMMAND flecs_enet_script ${case})
	set_tests_properties(flecs_enet.script_${case} PROPERTIES
		LABELS "contract;unit" TIMEOUT 15)
endforeach()
add_test(NAME flecs_enet.script_protocol COMMAND ${CMAKE_COMMAND}
	-DDRIVER=$<TARGET_FILE:flecs_enet_fixture>
	-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/script_protocol.txt
	-P ${CMAKE_CURRENT_SOURCE_DIR}/script_protocol.cmake)
set_tests_properties(flecs_enet.script_protocol PROPERTIES
	LABELS infrastructure TIMEOUT 15)
