#include "internal.h"
#include <stdlib.h>
#include <string.h>

ENetAddress fe_address(const flecs_enet_address_t *address)
{
	ENetAddress native = { .port = address->port };
	memcpy(&native.host, address->ip, 4);
	return native;
}

flecs_enet_address_t fe_public_address(const ENetAddress *native)
{
	flecs_enet_address_t address = { .port = native->port };
	memcpy(address.ip, &native->host, 4);
	return address;
}

void fe_peer_sync(struct fe_peer *peer)
{
	struct fe_host *h = peer->host;
	if (!h->alive || (ecs_world_get_flags(h->world) & EcsWorldFini))
		return;
	ecs_set_id(h->world, peer->entity, h->ctx->ids.peer_status,
		sizeof(peer->status), &peer->status);
}

struct fe_peer *fe_new_peer(struct fe_host *h, ENetPeer *native,
	uint64_t correlation, const flecs_enet_peer_config_t *tuning)
{
	struct fe_peer *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->native = native;
	p->host = h;
	p->status.address = fe_public_address(&native->address);
	p->status.channel_count = (uint32_t)native->channelCount;
	p->status.correlation = correlation;
	p->entity = ecs_new(h->world);
	p->next = h->peers;
	h->peers = p;
	fe_ref(h);
	native->data = p;
	fe_tune_peer(native, tuning ? tuning : &h->peer_defaults);
	ecs_add_pair(h->world, p->entity, EcsChildOf, h->entity);
	ecs_add_pair(h->world, p->entity, h->ctx->ids.host, h->entity);
	fe_attach(h->world, p->entity, h->ctx->peer_owner, p);
	fe_peer_sync(p);
	return p;
}

flecs_enet_result_t fe_event_room(struct fe_host *h, size_t bytes)
{
	flecs_enet_host_status_t *s = &h->status;
	if (s->last_sequence == UINT64_MAX) {
		s->state = FLECS_ENET_HOST_FAULTED;
		s->fault = FLECS_ENET_LIMIT;
		fe_teardown(h->world, h->entity, h);
		fe_sync(h);
		return FLECS_ENET_LIMIT;
	}
	const flecs_enet_limits_t *l = &s->active.limits;
	if (s->retained_events >= l->max_events ||
		(bytes && (s->event_bytes > l->max_event_bytes ||
		 bytes > l->max_event_bytes - s->event_bytes)))
		return FLECS_ENET_BACKPRESSURE;
	return FLECS_ENET_OK;
}

static ecs_entity_t publish(struct fe_host *h, struct fe_payload *owner,
	flecs_enet_event_t event)
{
	owner->host = h;
	owner->event = true;
	fe_ref(h);
	fe_reserve(h, owner->size, false);
	event.sequence = ++h->status.last_sequence;
	ecs_entity_t entity = ecs_new(h->world);
	owner->entity = entity;
	ecs_add_pair(h->world, entity, EcsChildOf, h->entity);
	ecs_add_pair(h->world, entity, h->ctx->ids.host, h->entity);
	fe_attach(h->world, entity, h->ctx->payload_owner, owner);
	ecs_set_id(h->world, entity, h->ctx->ids.event, sizeof(event), &event);
	fe_sync(h);
	return entity;
}

void fe_finish(struct fe_payload *p, flecs_enet_operation_result_t result)
{
	struct fe_host *h = p->host;
	const flecs_enet_ids_t *ids = &h->ctx->ids;
	result.operation = p->desc.operation;
	fe_start_finish(p, result.code, result.peer);
	flecs_enet_event_t e = { .kind = FLECS_ENET_EVENT_RESULT,
		.correlation = p->desc.correlation, .request = p->entity,
		.peer = result.peer, .result = result.code };
	if (p->desc.operation == FLECS_ENET_SEND ||
		p->desc.operation == FLECS_ENET_BROADCAST)
		e.channel = p->desc.channel;
	struct fe_payload *owner = p->completion;
	p->completion = NULL;
	ecs_entity_t event = publish(h, owner, e);
	ecs_set_id(h->world, event, ids->result, sizeof(result), &result);
	ecs_set_id(h->world, p->entity, ids->result, sizeof(result), &result);
	ecs_remove_id(h->world, p->entity, ids->packet);
	/* Removing the owner releases pending bytes and its FIFO entry. */
	ecs_remove_id(h->world, p->entity, h->ctx->payload_owner);
}

flecs_enet_result_t fe_diagnostic(struct fe_peer *p)
{
	struct fe_host *h = p->host;
	flecs_enet_result_t r = fe_event_room(h, 0);
	if (r)
		return r;
	struct fe_payload *owner = calloc(1, sizeof(*owner));
	if (!owner)
		return FLECS_ENET_NOMEM;
	publish(h, owner, (flecs_enet_event_t){
		.kind = FLECS_ENET_EVENT_DISCONNECT, .peer = p->entity,
		.correlation = p->status.correlation, .data = p->status.data,
		.result = p->diagnostic_result });
	p->diagnostic = false;
	return FLECS_ENET_OK;
}

flecs_enet_result_t fe_publish(ecs_world_t *world, ecs_entity_t entity,
	struct fe_host *h, const ENetEvent *event)
{
	/* Reserve count/byte capacity before consuming native dispatch.
	 * Hold at most one already-dispatched packet privately if byte size
	 * exceeds remaining event capacity, and preserve native order on retry.
	 * CONNECT/RECEIVE/DISCONNECT update public peer state atomically with
	 * publication; copy addresses/data/channel and correlation identity.
	 * Transfer receive packet to a private event owner and const Packet
	 * view.
	 * Create a retained ChildOf(host)/(Host,host) event with ++sequence.
	 * Publish Result both on request and distinct event exactly once, then
	 * release pending count/bytes. At UINT64_MAX fault without wrapping.
	 * Requests/events do not expire automatically; terminal peers survive.
	 */
	(void)world;
	(void)entity;
	size_t bytes = event->type == ENET_EVENT_TYPE_RECEIVE ?
		event->packet->dataLength : 0;
	flecs_enet_result_t r = fe_event_room(h, bytes);
	if (r)
		return r;
	struct fe_payload *owner = calloc(1, sizeof(*owner));
	if (!owner)
		return FLECS_ENET_NOMEM;
	struct fe_peer *p = event->peer->data;
	if (!p && event->type == ENET_EVENT_TYPE_CONNECT)
		p = fe_new_peer(h, event->peer, 0, NULL);
	if (!p) {
		free(owner);
		/* Deleted peers can leave queued receive/disconnect events. */
		if (event->type == ENET_EVENT_TYPE_RECEIVE)
			fe_packet_destroy(event->packet);
		return event->type == ENET_EVENT_TYPE_CONNECT ?
			FLECS_ENET_NOMEM : FLECS_ENET_OK;
	}
	flecs_enet_event_t e = { .peer = p->entity,
		.correlation = p->status.correlation,
		.channel = event->channelID, .data = event->data };
	switch (event->type) {
	case ENET_EVENT_TYPE_CONNECT:
		e.kind = FLECS_ENET_EVENT_CONNECT;
		p->status.state = FLECS_ENET_PEER_CONNECTED;
		p->status.channel_count = (uint32_t)event->peer->channelCount;
		p->status.address = fe_public_address(&event->peer->address);
		p->status.data = event->data;
		break;
	case ENET_EVENT_TYPE_RECEIVE:
		e.kind = FLECS_ENET_EVENT_RECEIVE;
		owner->size = bytes;
		owner->native = event->packet;
		break;
	case ENET_EVENT_TYPE_DISCONNECT:
		e.kind = FLECS_ENET_EVENT_DISCONNECT;
		p->status.state = FLECS_ENET_PEER_DISCONNECTED;
		p->status.data = event->data;
		p->native->data = NULL;
		p->native = NULL;
		break;
	default:
		free(owner);
		return FLECS_ENET_ARGUMENT;
	}
	fe_peer_sync(p);
	ecs_entity_t id = publish(h, owner, e);
	if (owner->native) {
		enet_uint32 flags = owner->native->flags;
		flecs_enet_packet_t view = {
			.data = bytes ? owner->native->data : NULL,
			.size = bytes,
			.delivery = flags & ENET_PACKET_FLAG_RELIABLE ?
				FLECS_ENET_RELIABLE :
				flags & ENET_PACKET_FLAG_UNSEQUENCED ?
				FLECS_ENET_UNSEQUENCED :
				flags & ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT ?
				FLECS_ENET_UNRELIABLE_FRAGMENT :
				FLECS_ENET_UNRELIABLE };
		ecs_set_id(h->world, id, h->ctx->ids.packet,
			sizeof(view), &view);
	}
	return FLECS_ENET_OK;
}

flecs_enet_result_t flecs_enet_consume(ecs_world_t *world,
	ecs_entity_t host, ecs_entity_t event)
{
	/* Validate event generation/host relation before ecs_delete.
	 * Deferral keeps byte ownership and accounting alive until merge.
	 * Destruction hook releases receive packet and retained event slot.
	 */
	if (!fe_world_valid(world))
		return FLECS_ENET_ARGUMENT;
	struct fe_context *ctx = fe_context(world);
	if (!ctx)
		return FLECS_ENET_STATE;
	if (!ecs_is_alive(world, host) || !ecs_is_alive(world, event) ||
		!ecs_has_id(world, event, ctx->ids.event) ||
		!ecs_has_pair(world, event, ctx->ids.host, host))
		return FLECS_ENET_NOT_FOUND;
	ecs_delete(world, event);
	return FLECS_ENET_OK;
}
