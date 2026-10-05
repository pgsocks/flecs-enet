if(NOT EXISTS "${RUBY}")
	message(FATAL_ERROR "Cucumber unavailable: install Ruby >=3.2, "
		"RubyGems >=3.2.8 and Bundler, then bundle install in ENet/.")
endif()
if(NOT EXISTS "${BUNDLE}")
	message(FATAL_ERROR "Cucumber unavailable: Bundler missing. "
		"Install Bundler 2.6.9 and run bundle install in ENet/.")
endif()
execute_process(COMMAND "${RUBY}" -e
	"v=Gem::Version; exit(v.new(RUBY_VERSION)>=v.new('3.2') ? 0 : 1)"
	RESULT_VARIABLE ruby_result)
if(NOT ruby_result EQUAL 0)
	message(FATAL_ERROR "Cucumber 11.1.1 requires Ruby >=3.2")
endif()
set(env "BUNDLE_GEMFILE=${SOURCE}/Gemfile" "BUNDLE_FROZEN=true"
	"FLECS_ENET_FIXTURE=${FIXTURE}" "FLECS_ENET_UDP_FIXTURE=${UDP_FIXTURE}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env}
	"${BUNDLE}" exec cucumber --version WORKING_DIRECTORY "${SOURCE}"
	RESULT_VARIABLE tools_result OUTPUT_VARIABLE version
	ERROR_VARIABLE error)
if(NOT tools_result EQUAL 0 OR NOT version MATCHES "11\\.1\\.1")
	message(FATAL_ERROR "Cucumber 11.1.1 unavailable: run bundle install "
		"using the locked ENet/Gemfile. ${error}")
endif()
if(GROUP STREQUAL "udp")
	set(tags "@udp")
else()
	set(tags "not @udp")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env}
	"${BUNDLE}" exec cucumber --publish-quiet --format progress
	--tags "${tags}" features WORKING_DIRECTORY "${SOURCE}"
	RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Cucumber ${GROUP} failed (exit ${result}); "
		"See fixture diagnostics and assertions above.")
endif()
