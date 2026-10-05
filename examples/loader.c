#include <flecs_enet.h>
#include <stdio.h>
#include <stdlib.h>

static void inspect(ecs_world_t *world, const flecs_enet_ids_t *ids)
{
	ecs_iter_t it = ecs_each_id(world, ids->start_status);
	while (ecs_each_next(&it)) {
		for (int32_t i = 0; i < it.count; i++) {
			const flecs_enet_start_status_t *s = ecs_get_id(world,
				it.entities[i], ids->start_status);
			printf("%s: startup state=%d code=%d peer=%llu\n",
				ecs_get_name(world, it.entities[i]), s->state,
				s->code, (unsigned long long)s->peer);
		}
	}
}

static void consume(ecs_world_t *world, const flecs_enet_ids_t *ids)
{
	/* Finish iteration before deleting entities (tables may move). */
	for (;;) {
		ecs_entity_t entity = 0;
		uint64_t sequence = UINT64_MAX;
		ecs_iter_t it = ecs_each_id(world, ids->event);
		while (ecs_each_next(&it)) {
			for (int32_t i = 0; i < it.count; i++) {
				const flecs_enet_event_t *e = ecs_get_id(world,
					it.entities[i], ids->event);
				if (!entity || e->sequence < sequence) {
					entity = it.entities[i];
					sequence = e->sequence;
				}
			}
		}
		if (!entity)
			break;
		flecs_enet_event_t event = *(const flecs_enet_event_t *)
			ecs_get_id(world, entity, ids->event);
		ecs_entity_t host = ecs_get_target(world, entity, ids->host, 0);
		printf("event sequence=%llu kind=%d result=%d\n",
			(unsigned long long)event.sequence, event.kind,
			event.result);
		if (event.kind == FLECS_ENET_EVENT_RECEIVE) {
			const flecs_enet_packet_t *packet = ecs_get_id(world,
				entity, ids->packet);
			printf("received %zu bytes\n", packet->size);
		}
		if (event.kind == FLECS_ENET_EVENT_RESULT && event.request &&
			ecs_is_alive(world, event.request))
			ecs_delete(world, event.request);
		flecs_enet_consume(world, host, entity);
	}
}

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s script.flecs [frames]\n", argv[0]);
		return 2;
	}
	unsigned long frames = argc == 3 ? strtoul(argv[2], NULL, 10) : 120;
	ecs_world_t *world = ecs_init();
	flecs_enet_ids_t ids;
	int rc = 1;
	flecs_enet_result_t r = flecs_enet_import(world, &ids);
	if (r) {
		fprintf(stderr, "import failed: %d\n", r);
		goto done;
	}
	if (ecs_script_run_file(world, argv[1])) {
		fprintf(stderr, "script loading failed: %s\n", argv[1]);
		goto done;
	}
	ecs_set_target_fps(world, 60);
	for (unsigned long i = 0; i < frames; i++) {
		if (!ecs_progress(world, 0))
			break;
		consume(world, &ids);
	}
	inspect(world, &ids);
	rc = 0;
done:
	ecs_fini(world);
	return rc;
}
