#include "internal.h"
#include <stdlib.h>
#include <string.h>

static bool peer_operation(flecs_enet_operation_t op)
{
	return op == FLECS_ENET_SEND ||
		(op >= FLECS_ENET_DISCONNECT && op <= FLECS_ENET_RESET) ||
		op >= FLECS_ENET_PING;
}

static flecs_enet_result_t validate(struct fe_host *h,
	const flecs_enet_request_t *d, const void *data, size_t size)
{
	if (d->operation < FLECS_ENET_OPEN ||
		d->operation > FLECS_ENET_THROTTLE || (size && !data))
		return FLECS_ENET_ARGUMENT;
	flecs_enet_request_t used = { .operation = d->operation,
		.correlation = d->correlation };
	bool payload = d->operation == FLECS_ENET_SEND ||
		d->operation == FLECS_ENET_BROADCAST;
	if (!payload && (data || size))
		return FLECS_ENET_ARGUMENT;
	if (peer_operation(d->operation)) {
		used.peer = d->peer;
		if (!ecs_is_alive(h->world, d->peer) ||
			!fe_find_peer(h, d->peer))
			return FLECS_ENET_NOT_FOUND;
	}
	if (payload) {
		used.channel = d->channel;
		used.delivery = d->delivery;
		if (d->delivery < FLECS_ENET_RELIABLE ||
			d->delivery > FLECS_ENET_UNRELIABLE_FRAGMENT)
			return FLECS_ENET_ARGUMENT;
		if (size > h->status.active.limits.max_packet_bytes)
			return FLECS_ENET_LIMIT;
		uint32_t channels = h->status.active.channel_limit;
		if (d->operation == FLECS_ENET_SEND) {
			struct fe_peer *p = fe_find_peer(h, d->peer);
			channels = p->status.channel_count;
		}
		if (d->channel >= channels)
			return FLECS_ENET_ARGUMENT;
	}
	switch (d->operation) {
	case FLECS_ENET_CONNECT: {
		used.address = d->address;
		used.channels = d->channels;
		used.data = d->data;
		uint32_t ip;
		memcpy(&ip, d->address.ip, sizeof(ip));
		if (!ip || ip == UINT32_MAX || !d->address.port ||
			d->channels > h->status.active.channel_limit)
			return FLECS_ENET_ARGUMENT;
		break;
	}
	case FLECS_ENET_DISCONNECT:
	case FLECS_ENET_DISCONNECT_NOW:
	case FLECS_ENET_DISCONNECT_LATER:
		used.data = d->data;
		break;
	case FLECS_ENET_BANDWIDTH:
		used.incoming_bandwidth = d->incoming_bandwidth;
		used.outgoing_bandwidth = d->outgoing_bandwidth;
		break;
	case FLECS_ENET_COMPRESSION:
		used.compression = d->compression;
		if (d->compression < FLECS_ENET_COMPRESSION_NONE ||
			d->compression > FLECS_ENET_COMPRESSION_RANGE_CODER)
			return FLECS_ENET_ARGUMENT;
		break;
	case FLECS_ENET_PING_INTERVAL:
	case FLECS_ENET_THROTTLE:
		used.interval_ms = d->interval_ms;
		if (!d->interval_ms)
			return FLECS_ENET_ARGUMENT;
		if (d->operation == FLECS_ENET_THROTTLE) {
			used.acceleration = d->acceleration;
			used.deceleration = d->deceleration;
			if (d->acceleration > 32 || d->deceleration > 32)
				return FLECS_ENET_ARGUMENT;
		}
		break;
	case FLECS_ENET_TIMEOUT:
		used.timeout_limit = d->timeout_limit;
		used.timeout_minimum_ms = d->timeout_minimum_ms;
		used.timeout_maximum_ms = d->timeout_maximum_ms;
		if (!d->timeout_limit || !d->timeout_minimum_ms ||
			d->timeout_minimum_ms > d->timeout_maximum_ms)
			return FLECS_ENET_ARGUMENT;
		break;
	default:
		break;
	}
	/* Compare fields, not struct padding supplied by the caller. */
#define SAME(field) if (d->field != used.field) return FLECS_ENET_ARGUMENT
	SAME(peer); SAME(channels); SAME(data); SAME(channel); SAME(delivery);
	SAME(compression); SAME(incoming_bandwidth); SAME(outgoing_bandwidth);
	SAME(interval_ms); SAME(timeout_limit); SAME(timeout_minimum_ms);
	SAME(timeout_maximum_ms); SAME(acceleration); SAME(deceleration);
#undef SAME
	if (memcmp(d->address.ip, used.address.ip, 4) ||
		d->address.port != used.address.port)
		return FLECS_ENET_ARGUMENT;
	return FLECS_ENET_OK;
}

flecs_enet_result_t flecs_enet_submit(ecs_world_t *world,
	ecs_entity_t entity, const flecs_enet_request_t *desc,
	const void *data, size_t size, ecs_entity_t *out_request)
{
	/* Validate world/stage, entity generations and ranges.
	 * Normalize/validate closed-host admission limits without opening ENet.
	 * Reserve request slot and pending payload bytes with overflow checks.
	 * Copy bytes into a private owner BEFORE any deferred ECS command.
	 * Create ChildOf(host), (Host,host), immutable Request and Packet view.
	 * Assign monotonic ticket; transfer owner into ECS using move hooks.
	 * Roll back reservations/allocations on failure, leaving output zero.
	 */
	if (out_request)
		*out_request = 0;
	if (!fe_world_valid(world) || !desc || !out_request)
		return FLECS_ENET_ARGUMENT;
	struct fe_context *ctx = fe_context(world);
	if (!ctx)
		return FLECS_ENET_STATE;
	if (!ecs_is_alive(world, entity))
		return FLECS_ENET_NOT_FOUND;
	const flecs_enet_host_config_t *config = ecs_get_id(world, entity,
		ctx->ids.host_config);
	if (!config)
		return FLECS_ENET_NOT_FOUND;
	struct fe_host *h = fe_find_host(ctx, entity);
	struct fe_host initial = { .alive = true, .entity = entity,
		.world = (ecs_world_t *)ecs_get_world(world), .ctx = ctx,
		.refs = 1 };
	if (!h || h->status.state == FLECS_ENET_HOST_CLOSED) {
		flecs_enet_host_config_t normalized;
		flecs_enet_result_t r = fe_normalize(config, &normalized);
		if (r)
			return r;
		initial.status.active = normalized;
		if (!h)
			h = &initial;
	}
	/* Validation and allocations precede any reservation or ECS command. */
	struct fe_host validation = *h;
	if (h->status.state == FLECS_ENET_HOST_CLOSED)
		validation.status.active = initial.status.active;
	flecs_enet_result_t r = validate(&validation, desc, data, size);
	if (r)
		return r;
	if (h->next_ticket == UINT64_MAX)
		return FLECS_ENET_LIMIT;
	const flecs_enet_limits_t *l = &validation.status.active.limits;
	if (h->status.pending_requests >= l->max_requests ||
		h->status.outgoing_bytes > l->max_outgoing_bytes ||
		size > l->max_outgoing_bytes - h->status.outgoing_bytes)
		return FLECS_ENET_BACKPRESSURE;
	struct fe_payload *p = calloc(1, sizeof(*p));
	if (!p)
		return FLECS_ENET_NOMEM;
	if (size) {
		p->bytes = malloc(size);
		if (!p->bytes) {
			free(p);
			return FLECS_ENET_NOMEM;
		}
		memcpy(p->bytes, data, size);
	}
	if (h == &initial) {
		h = malloc(sizeof(*h));
		if (!h) {
			free(p->bytes);
			free(p);
			return FLECS_ENET_NOMEM;
		}
		*h = initial;
		h->next = ctx->hosts;
		ctx->hosts = h;
		fe_attach(world, entity, ctx->host_owner, h);
		ecs_set_id(world, entity, ctx->ids.host_status,
			sizeof(h->status), &h->status);
	}
	if (h->status.state == FLECS_ENET_HOST_CLOSED)
		h->status.active = validation.status.active;
	fe_reserve(h, size, true);
	p->size = size;
	p->reservation = size;
	p->host = h;
	p->desc = *desc;
	p->pending = true;
	p->ticket = ++h->next_ticket;
	fe_ref(h);
	struct fe_payload **tail = &h->requests;
	while (*tail)
		tail = &(*tail)->next;
	*tail = p;
	p->entity = ecs_new(world);
	ecs_add_pair(world, p->entity, EcsChildOf, entity);
	ecs_add_pair(world, p->entity, ctx->ids.host, entity);
	fe_attach(world, p->entity, ctx->payload_owner, p);
	ecs_set_id(world, p->entity, ctx->ids.request, sizeof(*desc), desc);
	if (desc->operation == FLECS_ENET_SEND ||
		desc->operation == FLECS_ENET_BROADCAST) {
		flecs_enet_packet_t view = { .data = p->bytes, .size = size,
			.delivery = desc->delivery };
		ecs_set_id(world, p->entity, ctx->ids.packet,
			sizeof(view), &view);
	}
	fe_sync(h);
	*out_request = p->entity;
	return FLECS_ENET_OK;
}

flecs_enet_result_t flecs_enet_close(ecs_world_t *world, ecs_entity_t host,
	uint64_t correlation, ecs_entity_t *out_request)
{
	flecs_enet_request_t desc = { .operation = FLECS_ENET_CLOSE,
		.correlation = correlation };
	return flecs_enet_submit(world, host, &desc, NULL, 0, out_request);
}

flecs_enet_result_t flecs_enet_flush(ecs_world_t *world, ecs_entity_t host,
	uint64_t correlation, ecs_entity_t *out_request)
{
	flecs_enet_request_t desc = { .operation = FLECS_ENET_FLUSH,
		.correlation = correlation };
	return flecs_enet_submit(world, host, &desc, NULL, 0, out_request);
}

flecs_enet_result_t flecs_enet_send(ecs_world_t *world, ecs_entity_t host,
	ecs_entity_t peer, uint8_t channel, flecs_enet_delivery_t delivery,
	const void *data, size_t size, uint64_t correlation,
	ecs_entity_t *out_request)
{
	flecs_enet_request_t desc = { .operation = FLECS_ENET_SEND,
		.peer = peer, .channel = channel, .delivery = delivery,
		.correlation = correlation };
	return flecs_enet_submit(world, host, &desc, data, size, out_request);
}

flecs_enet_result_t flecs_enet_broadcast(ecs_world_t *world,
	ecs_entity_t host, uint8_t channel, flecs_enet_delivery_t delivery,
	const void *data, size_t size, uint64_t correlation,
	ecs_entity_t *out_request)
{
	flecs_enet_request_t desc = { .operation = FLECS_ENET_BROADCAST,
		.channel = channel, .delivery = delivery,
		.correlation = correlation };
	return flecs_enet_submit(world, host, &desc, data, size, out_request);
}

flecs_enet_result_t flecs_enet_disconnect(ecs_world_t *world,
	ecs_entity_t host, ecs_entity_t peer, flecs_enet_operation_t mode,
	uint32_t data, uint64_t correlation, ecs_entity_t *out_request)
{
	if (out_request)
		*out_request = 0;
	if (mode < FLECS_ENET_DISCONNECT || mode > FLECS_ENET_RESET)
		return FLECS_ENET_ARGUMENT;
	flecs_enet_request_t desc = { .operation = mode, .peer = peer,
		.data = data, .correlation = correlation };
	return flecs_enet_submit(world, host, &desc, NULL, 0, out_request);
}
