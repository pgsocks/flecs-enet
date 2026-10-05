#include <flecs_enet.h>
#include <stdio.h>

/* Setup and initial tuning live in examples/scripts. */
flecs_enet_result_t example_minimal(void)
{
	ecs_world_t *world = ecs_init();
	flecs_enet_ids_t ids;
	flecs_enet_result_t rc = flecs_enet_import(world, &ids);
	if (rc)
		goto done;
	if (ecs_script_run_file(world,
		FLECS_ENET_EXAMPLES "/listener.flecs")) {
		rc = FLECS_ENET_ARGUMENT;
		goto done;
	}
	ecs_progress(world, 0.01f);
	ecs_entity_t host = ecs_lookup(world, "server");
	const flecs_enet_start_status_t *s = ecs_get_id(world, host,
		ids.start_status);
	rc = s ? s->code : FLECS_ENET_STATE;
done:
	ecs_fini(world);
	return rc;
}

flecs_enet_result_t example_multi_peer(ecs_world_t *world, ecs_entity_t host,
	ecs_entity_t first, ecs_entity_t second, ecs_entity_t *request)
{
	flecs_enet_result_t rc = flecs_enet_send(world, host, first, 0,
		FLECS_ENET_RELIABLE, "control", 7, 3, request);
	if (rc != FLECS_ENET_OK)
		return rc;
	rc = flecs_enet_send(world, host, second, 1, FLECS_ENET_UNRELIABLE,
		"state", 5, 4, request);
	if (rc != FLECS_ENET_OK)
		return rc;
	return flecs_enet_broadcast(world, host, 0, FLECS_ENET_RELIABLE,
		"announcement", 12, 5, request);
}

/* Select minimum sequence; native query/table order is not event order. */
ecs_entity_t example_next_event(ecs_world_t *world,
	const flecs_enet_ids_t *ids, ecs_entity_t host)
{
	ecs_entity_t next = 0;
	uint64_t sequence = UINT64_MAX;
	ecs_iter_t it = ecs_each_id(world, ids->event);
	while (ecs_each_next(&it)) {
		for (int32_t i = 0; i < it.count; i++) {
			ecs_entity_t e = it.entities[i];
			if (!ecs_has_pair(world, e, ids->host, host))
				continue;
			const flecs_enet_event_t *event = ecs_get_id(world, e,
				ids->event);
			if (!next || event->sequence < sequence) {
				next = e;
				sequence = event->sequence;
			}
		}
	}
	return next;
}

flecs_enet_result_t example_consume(ecs_world_t *world,
	const flecs_enet_ids_t *ids, ecs_entity_t host)
{
	ecs_entity_t entity = example_next_event(world, ids, host);
	if (!entity)
		return FLECS_ENET_OK;
	const flecs_enet_event_t *event = ecs_get_id(world, entity, ids->event);
	if (event->kind == FLECS_ENET_EVENT_RECEIVE) {
		const flecs_enet_packet_t *packet = ecs_get_id(world, entity,
			ids->packet);
		/* Process/copy bytes before consumption. */
		printf("received %zu bytes on %u\n",
			packet->size, event->channel);
	}
	if (event->kind == FLECS_ENET_EVENT_RESULT) {
		const flecs_enet_operation_result_t *result = ecs_get_id(world,
			entity, ids->result);
		printf("request result %d: %u/%u accepted\n", result->code,
			result->accepted, result->attempted);
		/* Complete request metadata no longer needs retention. */
		if (event->request && ecs_is_alive(world, event->request))
			ecs_delete(world, event->request);
	}
	return flecs_enet_consume(world, host, entity);
}

flecs_enet_result_t example_pressure_retry(ecs_world_t *world,
	ecs_entity_t host, ecs_entity_t peer, ecs_entity_t *request)
{
	/* Caller retains this message if admission returns BACKPRESSURE.
	 * Retry only failed admission, not an accepted request or partial
	 * result.
	 */
	return flecs_enet_send(world, host, peer, 0, FLECS_ENET_RELIABLE,
		"important", 9, 8, request);
}
