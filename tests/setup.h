#ifndef FE_TEST_SETUP_H
#define FE_TEST_SETUP_H
#include <flecs_enet.h>

static inline flecs_enet_result_t test_open(ecs_world_t *world,
	ecs_entity_t host, uint64_t correlation, ecs_entity_t *out)
{
	flecs_enet_request_t d = { .operation = FLECS_ENET_OPEN,
		.correlation = correlation };
	return flecs_enet_submit(world, host, &d, NULL, 0, out);
}

static inline flecs_enet_result_t test_connect(ecs_world_t *world,
	ecs_entity_t host, const flecs_enet_address_t *address,
	uint32_t channels, uint32_t data, uint64_t correlation,
	ecs_entity_t *out)
{
	flecs_enet_request_t d = { .operation = FLECS_ENET_CONNECT,
		.address = *address, .channels = channels, .data = data,
		.correlation = correlation };
	return flecs_enet_submit(world, host, &d, NULL, 0, out);
}
#endif
