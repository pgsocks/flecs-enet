#include "internal.h"
#include <stdlib.h>

flecs_enet_result_t fe_validate_peer(const flecs_enet_peer_config_t *c)
{
	if ((c->timeout_limit || c->timeout_minimum_ms ||
		c->timeout_maximum_ms) && (!c->timeout_limit ||
		!c->timeout_minimum_ms ||
		c->timeout_minimum_ms > c->timeout_maximum_ms))
		return FLECS_ENET_ARGUMENT;
	if (c->acceleration > 32 || c->deceleration > 32 ||
		(!c->throttle_interval_ms &&
		 (c->acceleration || c->deceleration)))
		return FLECS_ENET_ARGUMENT;
	return FLECS_ENET_OK;
}

void fe_tune_peer(ENetPeer *native, const flecs_enet_peer_config_t *c)
{
	if (c->ping_interval_ms)
		fe_peer_ping_interval(native, c->ping_interval_ms);
	if (c->timeout_limit)
		fe_peer_timeout(native, c->timeout_limit,
			c->timeout_minimum_ms, c->timeout_maximum_ms);
	if (c->throttle_interval_ms)
		fe_peer_throttle_configure(native, c->throttle_interval_ms,
			c->acceleration, c->deceleration);
}

void fe_start_finish(struct fe_payload *p, flecs_enet_result_t code,
	ecs_entity_t peer)
{
	struct fe_host *h = p->host;
	if (!p->startup || !h->alive ||
		(ecs_world_get_flags(h->world) & EcsWorldFini) ||
		!ecs_is_alive(h->world, p->startup))
		return;
	const flecs_enet_start_status_t *old = ecs_get_id(h->world,
		p->startup, h->ctx->ids.start_status);
	if (!old || old->state != FLECS_ENET_START_PENDING ||
		old->request != p->entity)
		return;
	flecs_enet_start_status_t s = { .state = FLECS_ENET_START_FINISHED,
		.code = code, .request = p->entity, .peer = peer };
	ecs_set_id(h->world, p->startup, h->ctx->ids.start_status,
		sizeof(s), &s);
}

static void admit(ecs_world_t *world, struct fe_context *ctx,
	ecs_entity_t entity, bool connection)
{
	const flecs_enet_ids_t *ids = &ctx->ids;
	if (!ecs_is_alive(world, entity) ||
		!ecs_has_id(world, entity, ids->start))
		return;
	const flecs_enet_start_status_t *old = ecs_get_id(world, entity,
		ids->start_status);
	if (old && old->state == FLECS_ENET_START_PENDING) {
		ecs_remove_id(world, entity, ids->start);
		return;
	}
	flecs_enet_start_status_t s = { .state = FLECS_ENET_START_WAITING };
	ecs_entity_t host = connection ?
		ecs_get_target(world, entity, EcsChildOf, 0) : entity;
	struct fe_host *h = fe_find_host(ctx, host);
	flecs_enet_request_t d = { .operation = connection ?
		FLECS_ENET_CONNECT : FLECS_ENET_OPEN };
	flecs_enet_peer_config_t tuning = {0};
	if (connection) {
		const flecs_enet_connection_config_t *c = ecs_get_id(world,
			entity, ids->connection_config);
		d.address = c->address;
		d.channels = c->channels;
		d.data = c->data;
		d.correlation = c->correlation;
		if (!host || !ecs_has_id(world, host, ids->host_config)) {
			s.code = FLECS_ENET_NOT_FOUND;
			goto terminal;
		}
		if (h) {
			for (struct fe_peer *p = h->peers; p; p = p->next) {
				if (p->native && ecs_has_pair(world, p->entity,
					ctx->declaration, entity)) {
					s.code = FLECS_ENET_STATE;
					goto terminal;
				}
			}
		}
		if (h && (h->closing ||
			h->status.state == FLECS_ENET_HOST_FAULTED)) {
			s.code = FLECS_ENET_STATE;
			goto terminal;
		}
		if (!h || h->status.state != FLECS_ENET_HOST_OPEN)
			goto publish;
		tuning = h->peer_defaults;
	} else if (h && h->status.state != FLECS_ENET_HOST_CLOSED) {
		s.code = FLECS_ENET_STATE;
		goto terminal;
	}
	const flecs_enet_peer_config_t *c = ecs_get_id(world, entity,
		ids->peer_config);
	if (c)
		tuning = *c;
	s.code = fe_validate_peer(&tuning);
	if (s.code)
		goto terminal;
	s.code = flecs_enet_submit(world, host, &d, NULL, 0, &s.request);
	if (s.code == FLECS_ENET_BACKPRESSURE)
		goto publish;
	if (s.code)
		goto terminal;
	s.state = FLECS_ENET_START_PENDING;
	h = fe_find_host(ctx, host);
	struct fe_payload *p = h->requests;
	while (p->entity != s.request)
		p = p->next;
	p->startup = entity;
	p->peer_config = tuning;
	p->tuned = connection;
	if (connection)
		ecs_add_pair(world, s.request, ctx->declaration, entity);
	ecs_remove_id(world, entity, ids->start);
	goto publish;
terminal:
	s.state = FLECS_ENET_START_FINISHED;
	ecs_remove_id(world, entity, ids->start);
publish:
	ecs_set_id(world, entity, ids->start_status, sizeof(s), &s);
}

void fe_startup(ecs_world_t *world, struct fe_context *ctx)
{
	/* Structural publication must happen after releasing iterator locks. */
	int32_t count = ecs_count_id(world, ctx->ids.start);
	if (!count)
		return;
	ecs_entity_t *entities = malloc((size_t)count * sizeof(*entities));
	if (!entities)
		return;
	int32_t n = 0;
	ecs_iter_t it = ecs_each_id(world, ctx->ids.start);
	while (ecs_each_next(&it)) {
		for (int32_t i = 0; i < it.count; i++)
			entities[n++] = it.entities[i];
	}
	for (int pass = 0; pass < 2; pass++) {
		for (int32_t i = 0; i < n; i++) {
			ecs_entity_t e = entities[i];
			bool host = ecs_has_id(world, e, ctx->ids.host_config);
			bool connection = ecs_has_id(world, e,
				ctx->ids.connection_config);
			if (!pass && host)
				admit(world, ctx, e, false);
			if (pass && connection && !host)
				admit(world, ctx, e, true);
		}
	}
	free(entities);
}
