#include "internal.h"
#include <stdlib.h>
#include <string.h>

static void owner_dtor(void *, int32_t, const ecs_type_info_t *);

bool fe_world_valid(ecs_world_t *world)
{
	if (!world)
		return false;
	const ecs_world_t *real = ecs_get_world(world);
	ecs_flags32_t flags = ecs_world_get_flags(real);
	if (flags & (EcsWorldMultiThreaded | EcsWorldFini))
		return false;
	if (world == real)
		return !(flags & EcsWorldReadonly);
	return ecs_stage_get_id(world) == 0;
}

struct fe_context *fe_context(ecs_world_t *world)
{
	ecs_entity_t id = ecs_lookup(world, "flecs_enet.NativeHost");
	const ecs_type_hooks_t *hooks = id ? ecs_get_hooks_id(world, id) : NULL;
	return hooks && hooks->dtor == owner_dtor ? hooks->ctx : NULL;
}

struct fe_host *fe_find_host(struct fe_context *ctx, ecs_entity_t entity)
{
	for (struct fe_host *h = ctx->hosts; h; h = h->next) {
		if (h->alive && h->entity == entity)
			return h;
	}
	return NULL;
}

struct fe_peer *fe_find_peer(struct fe_host *host, ecs_entity_t entity)
{
	for (struct fe_peer *p = host->peers; p; p = p->next) {
		if (p->entity == entity)
			return p;
	}
	return NULL;
}

void fe_sync(struct fe_host *host)
{
	if (!host->alive ||
		(ecs_world_get_flags(host->world) & EcsWorldFini))
		return;
	flecs_enet_host_status_t *s = ecs_get_mut_id(host->world,
		host->entity, host->ctx->ids.host_status);
	if (s)
		*s = host->status;
}

void fe_ref(struct fe_host *host)
{
	host->refs++;
}

void fe_unref(struct fe_host *host)
{
	if (--host->refs)
		return;
	struct fe_host **link = &host->ctx->hosts;
	while (*link != host)
		link = &(*link)->next;
	*link = host->next;
	free(host);
}

flecs_enet_result_t fe_normalize(const flecs_enet_host_config_t *input,
	flecs_enet_host_config_t *out)
{
	*out = *input;
#define DEFAULT(field, value) if (!out->field) out->field = (value)
	DEFAULT(peer_limit, 32);
	DEFAULT(channel_limit, 2);
	DEFAULT(limits.max_requests, 1024);
	DEFAULT(limits.max_events, 1024);
	DEFAULT(limits.max_outgoing_packets, 8192);
	DEFAULT(limits.events_per_progress, 64);
	DEFAULT(limits.max_event_bytes, 32 * 1024 * 1024);
	DEFAULT(limits.max_outgoing_bytes, 32 * 1024 * 1024);
	DEFAULT(limits.max_packet_bytes, 1024 * 1024);
	DEFAULT(limits.max_waiting_bytes, 32 * 1024 * 1024);
#undef DEFAULT
	const flecs_enet_limits_t *l = &out->limits;
	if (out->peer_limit > 4095 || out->channel_limit > 255 ||
		out->compression < FLECS_ENET_COMPRESSION_NONE ||
		out->compression > FLECS_ENET_COMPRESSION_RANGE_CODER ||
		l->max_packet_bytes > 32 * 1024 * 1024 ||
		l->max_packet_bytes > l->max_event_bytes ||
		l->max_packet_bytes > l->max_outgoing_bytes ||
		l->max_packet_bytes > l->max_waiting_bytes)
		return FLECS_ENET_ARGUMENT;
	return FLECS_ENET_OK;
}

flecs_enet_result_t fe_reserve(struct fe_host *host, size_t bytes,
	bool outgoing)
{
	/* Checked addition for counts/bytes against normalized limits.
	 * Separate pending module bytes, accepted ENet bytes, retained RX
	 * bytes.
	 * Reserve payload at admission, ENet packet count at execution; unwind
	 * in reverse order on every failure, cancellation, removal or teardown.
	 * Broadcast reserves one payload copy per selected peer before
	 * execution.
	 */
	flecs_enet_host_status_t *s = &host->status;
	const flecs_enet_limits_t *l = &s->active.limits;
	if (outgoing) {
		if (s->pending_requests >= l->max_requests ||
			s->outgoing_bytes > l->max_outgoing_bytes ||
			bytes > l->max_outgoing_bytes - s->outgoing_bytes)
			return FLECS_ENET_BACKPRESSURE;
		s->pending_requests++;
		s->outgoing_bytes += bytes;
	} else {
		flecs_enet_result_t r = fe_event_room(host, bytes);
		if (r)
			return r;
		s->retained_events++;
		s->event_bytes += bytes;
	}
	return FLECS_ENET_OK;
}

flecs_enet_result_t fe_teardown(ecs_world_t *world, ecs_entity_t entity,
	struct fe_host *host)
{
	/* Private owner hooks prohibit copy/copy-construction.
	 * Move/move-construction transfer owner and zero source; never memcpy
	 * duplicate native ownership. Constructors initialize empty owners.
	 * On removal/dtor: stop processing, cancel pending work, release
	 * pending
	 * native event, destroy host while packet callback accounting is alive,
	 * null peer slots; release module payloads and queries/contexts once.
	 * ChildOf cascade/world fini must be safe under arbitrary hook order.
	 * Runtime release follows native-owner destruction in context cleanup.
	 */
	(void)world;
	(void)entity;
	if (host->has_pending) {
		if (host->pending_event.packet)
			fe_packet_destroy(host->pending_event.packet);
		host->has_pending = false;
	}
	for (struct fe_peer *p = host->peers; p; p = p->next) {
		if (p->native)
			p->native->data = NULL;
		p->native = NULL;
		p->status.state = FLECS_ENET_PEER_DISCONNECTED;
		if (host->alive)
			fe_peer_sync(p);
	}
	if (host->native) {
		fe_host_destroy(host->native);
		host->native = NULL;
	}
	return FLECS_ENET_OK;
}

static void release_owner(void *ptr, const ecs_type_info_t *info)
{
	if (!ptr)
		return;
	struct fe_context *ctx = info->hooks.ctx;
	if (info->component == ctx->host_owner) {
		struct fe_host *h = ptr;
		h->alive = false;
		fe_teardown(h->world, h->entity, h);
		fe_unref(h);
	} else if (info->component == ctx->peer_owner) {
		struct fe_peer *p = ptr;
		struct fe_host *h = p->host;
		if (p->native) {
			if (h->has_pending &&
				h->pending_event.peer == p->native) {
				ENetPacket *packet = h->pending_event.packet;
				if (packet)
					fe_packet_destroy(packet);
				h->has_pending = false;
			}
			p->native->data = NULL;
			fe_peer_reset(p->native);
		}
		struct fe_peer **link = &h->peers;
		while (*link != p)
			link = &(*link)->next;
		*link = p->next;
		free(p);
		fe_unref(h);
	} else {
		struct fe_payload *p = ptr;
		struct fe_host *h = p->host;
		if (p->pending) {
			fe_start_finish(p, FLECS_ENET_STATE, 0);
			h->status.pending_requests--;
			h->status.outgoing_bytes -= p->reservation;
			if (h->closing_request == p->entity) {
				h->closing = false;
				h->closing_request = 0;
			}
			struct fe_payload **link = &h->requests;
			while (*link != p)
				link = &(*link)->next;
			*link = p->next;
		}
		if (p->event) {
			h->status.retained_events--;
			h->status.event_bytes -= p->size;
		}
		if (p->native)
			fe_packet_destroy(p->native);
		free(p->completion);
		free(p->bytes);
		free(p);
		fe_sync(h);
		fe_unref(h);
	}
}

static void owner_ctor(void *ptr, int32_t count, const ecs_type_info_t *info)
{
	memset(ptr, 0, (size_t)count * (size_t)info->size);
}

static void owner_dtor(void *ptr, int32_t count, const ecs_type_info_t *info)
{
	struct fe_owner *owners = ptr;
	for (int32_t i = 0; i < count; i++) {
		release_owner(owners[i].ptr, info);
		owners[i].ptr = NULL;
	}
}

static void owner_move_ctor(void *dst, void *src, int32_t count,
	const ecs_type_info_t *info)
{
	memcpy(dst, src, (size_t)count * (size_t)info->size);
	memset(src, 0, (size_t)count * (size_t)info->size);
}

static void owner_move(void *dst, void *src, int32_t count,
	const ecs_type_info_t *info)
{
	owner_dtor(dst, count, info);
	owner_move_ctor(dst, src, count, info);
}

void fe_attach(ecs_world_t *world, ecs_entity_t entity, ecs_entity_t id,
	void *ptr)
{
	struct fe_owner *owner = ecs_emplace_id(world, entity, id,
		sizeof(*owner), NULL);
	owner->ptr = ptr;
}

static void context_free(void *ptr)
{
	struct fe_context *ctx = ptr;
	fe_deinitialize();
	free(ctx);
}

void fe_install_owners(ecs_world_t *world, struct fe_context *ctx)
{
	ctx->declaration = ecs_entity_init(world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.Declaration" });
	ecs_add_id(world, ctx->declaration, EcsExclusive);
	ecs_add_pair(world, ctx->declaration, EcsOnDeleteTarget, EcsDelete);
	const char *names[] = { "flecs_enet.NativeHost",
		"flecs_enet.NativePeer", "flecs_enet.PayloadOwner" };
	ecs_entity_t *ids[] = { &ctx->host_owner, &ctx->peer_owner,
		&ctx->payload_owner };
	for (size_t i = 0; i < 3; i++) {
		ecs_entity_t id = ecs_entity_init(world,
			&(ecs_entity_desc_t){ .name = names[i] });
		*ids[i] = ecs_component_init(world, &(ecs_component_desc_t){
			.entity = id, .type.size = sizeof(struct fe_owner),
			.type.alignment = _Alignof(struct fe_owner) });
		ecs_set_hooks_id(world, id, &(ecs_type_hooks_t){
			.ctor = owner_ctor, .dtor = owner_dtor,
			.move = owner_move, .move_ctor = owner_move_ctor,
			.flags = ECS_TYPE_HOOK_COPY_ILLEGAL |
				ECS_TYPE_HOOK_COPY_CTOR_ILLEGAL,
			.ctx = ctx, .ctx_free = i == 0 ? context_free : NULL });
	}
}
