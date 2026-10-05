#include "fixture.h"
#include <string.h>

int fixture_init(struct fixture *f)
{
	memset(f, 0, sizeof(*f));
	f->world = ecs_init();
	return flecs_enet_import(f->world, &f->ids) == FLECS_ENET_OK;
}

void fixture_fini(struct fixture *f)
{
	if (f->world)
		ecs_fini(f->world);
	f->world = NULL;
}

ecs_entity_t fixture_host(struct fixture *f, unsigned slot,
	const flecs_enet_host_config_t *config)
{
	if (slot >= FE_SLOTS)
		return 0;
	f->host[slot] = ecs_new(f->world);
	ecs_set_id(f->world, f->host[slot], f->ids.host_config,
		sizeof(*config), config);
	return f->host[slot];
}

static unsigned children(struct fixture *f, ecs_entity_t host,
	ecs_entity_t component, ecs_entity_t *out)
{
	unsigned count = 0;
	ecs_iter_t it = ecs_each_id(f->world, component);
	while (ecs_each_next(&it)) {
		for (int32_t i = 0; i < it.count; i++) {
			ecs_entity_t e = it.entities[i];
			if (!ecs_has_pair(f->world, e, EcsChildOf, host))
				continue;
			if (count < FE_SLOTS)
				out[count] = e;
			count++;
		}
	}
	return count;
}

ecs_entity_t fixture_peer(struct fixture *f, ecs_entity_t host, unsigned n)
{
	ecs_entity_t out[FE_SLOTS];
	unsigned count = children(f, host, f->ids.peer_status, out);
	if (count > FE_SLOTS || n >= count)
		return 0;
	for (unsigned i = 0; i < count; i++) {
		for (unsigned j = i + 1; j < count; j++) {
			if (out[j] < out[i]) {
				ecs_entity_t tmp = out[i];
				out[i] = out[j];
				out[j] = tmp;
			}
		}
	}
	return out[n];
}

ecs_entity_t fixture_event(struct fixture *f, ecs_entity_t host, unsigned n)
{
	ecs_entity_t out[FE_SLOTS];
	unsigned count = children(f, host, f->ids.event, out);
	if (count > FE_SLOTS || n >= count)
		return 0;
	for (unsigned i = 0; i < count; i++) {
		for (unsigned j = i + 1; j < count; j++) {
			const flecs_enet_event_t *a = ecs_get_id(f->world,
				out[i], f->ids.event);
			const flecs_enet_event_t *b = ecs_get_id(f->world,
				out[j], f->ids.event);
			if (b->sequence < a->sequence) {
				ecs_entity_t tmp = out[i];
				out[i] = out[j];
				out[j] = tmp;
			}
		}
	}
	return out[n];
}

unsigned fixture_events(struct fixture *f, ecs_entity_t host)
{
	ecs_entity_t out[FE_SLOTS];
	return children(f, host, f->ids.event, out);
}

flecs_enet_result_t fixture_submit(struct fixture *f, unsigned host,
	const flecs_enet_request_t *desc, const void *data, size_t size,
	ecs_entity_t *out)
{
	if (host >= FE_SLOTS || f->requests == FE_SLOTS) {
		*out = 0;
		return FLECS_ENET_LIMIT;
	}
	flecs_enet_result_t result = flecs_enet_submit(f->world, f->host[host],
		desc, data, size, out);
	if (result == FLECS_ENET_OK)
		f->request[f->requests++] = *out;
	return result;
}
