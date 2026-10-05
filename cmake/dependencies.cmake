include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)
CPMAddPackage(NAME flecs
	GITHUB_REPOSITORY SanderMertens/flecs GIT_TAG v4.1.1
	DOWNLOAD_ONLY YES)
add_library(flecs_enet_flecs STATIC "${flecs_SOURCE_DIR}/distr/flecs.c")
target_include_directories(flecs_enet_flecs SYSTEM PUBLIC
	"${flecs_SOURCE_DIR}/distr")
set_target_properties(flecs_enet_flecs PROPERTIES C_EXTENSIONS ON)
if(WIN32)
	target_link_libraries(flecs_enet_flecs PUBLIC ws2_32)
elseif(UNIX)
	find_package(Threads REQUIRED)
	target_link_libraries(flecs_enet_flecs PUBLIC Threads::Threads m)
endif()
CPMAddPackage(NAME enet
	GITHUB_REPOSITORY lsalzman/enet GIT_TAG v1.3.18)
set_target_properties(enet PROPERTIES C_EXTENSIONS ON)
target_include_directories(enet SYSTEM PUBLIC "${enet_SOURCE_DIR}/include")
if(WIN32)
	target_link_libraries(enet PUBLIC winmm ws2_32)
endif()
