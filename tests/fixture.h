#ifndef FE_FIXTURE_H
#define FE_FIXTURE_H
#include <flecs_enet.h>
#include "test_boundary.h"

#define FE_SLOTS 64
struct fixture {
	ecs_world_t *world;
	flecs_enet_ids_t ids;
	ecs_entity_t host[FE_SLOTS], request[FE_SLOTS];
	unsigned requests;
};
int fixture_init(struct fixture *);
void fixture_fini(struct fixture *);
ecs_entity_t fixture_host(struct fixture *, unsigned,
	const flecs_enet_host_config_t *);
ecs_entity_t fixture_peer(struct fixture *, ecs_entity_t, unsigned);
ecs_entity_t fixture_event(struct fixture *, ecs_entity_t, unsigned);
unsigned fixture_events(struct fixture *, ecs_entity_t);
flecs_enet_result_t fixture_submit(struct fixture *, unsigned,
	const flecs_enet_request_t *, const void *, size_t, ecs_entity_t *);
#endif
