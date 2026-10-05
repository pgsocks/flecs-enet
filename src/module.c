#include "internal.h"
#include <string.h>
#include <stdlib.h>

struct declaration {
	const char *name;
	size_t size, alignment;
};

struct phase_edges {
	ecs_entity_t from[4], to[4];
};

static bool depends(ecs_world_t *world, ecs_entity_t from,
	ecs_entity_t target, const struct phase_edges *edges, unsigned depth)
{
	if (from == target || depth > 256)
		return true;
	/* Missing phases have virtual identities during schema preflight. */
	if (from != UINT64_MAX && from != UINT64_MAX - 1) {
		for (int32_t i = 0; ; i++) {
			ecs_entity_t next = ecs_get_target(world, from,
				EcsDependsOn, i);
			if (!next)
				break;
			if (depends(world, next, target, edges, depth + 1))
				return true;
		}
	}
	for (size_t i = 0; i < 4; i++) {
		if (edges->from[i] == from &&
			depends(world, edges->to[i], target, edges, depth + 1))
			return true;
	}
	return false;
}

static void scheduled(ecs_iter_t *it)
{
	bool deferred = ecs_is_deferred(it->world);
	if (deferred)
		ecs_defer_suspend(it->world);
	struct fe_context *ctx = it->ctx;
	bool send = it->system == ecs_lookup(it->world,
		"flecs_enet.SendSystem");
	if (!send)
		fe_startup(it->world, ctx);
	/* Tasks have no table lock: publication can move any host or child. */
	for (struct fe_host *h = ctx->hosts, *next; h; h = next) {
		next = h->next;
		if (!h->alive || !ecs_is_alive(it->world, h->entity) ||
			!ecs_has_id(it->world, h->entity, ctx->host_owner))
			continue;
		fe_ref(h);
		if (send)
			fe_outgoing(it->world, h->entity, h);
		else
			fe_receive(it->world, h->entity, h);
		fe_sync(h);
		fe_unref(h);
	}
	if (deferred)
		ecs_defer_resume(it->world);
}

static void install_system(ecs_world_t *world, struct fe_context *ctx,
	const char *name, ecs_entity_t phase)
{
	ecs_entity_t entity = ecs_entity_init(world,
		&(ecs_entity_desc_t){ .name = name });
	ecs_add_pair(world, entity, EcsDependsOn, phase);
	ecs_system_desc_t desc = { .entity = entity, .callback = scheduled,
		.ctx = ctx, .immediate = true };
	/* Explicit out-of-query access forces merges around these tasks. */
	desc.query.terms[0] = (ecs_term_t){ .id = EcsWildcard,
		.src.id = EcsIsEntity, .inout = EcsInOut };
	ecs_system_init(world, &desc);
}

flecs_enet_result_t flecs_enet_import(ecs_world_t *world,
	flecs_enet_ids_t *out_ids)
{
	if (out_ids)
		memset(out_ids, 0, sizeof(*out_ids));
	if (!world || !out_ids || ecs_get_world(world) != world ||
		ecs_is_deferred(world) ||
		(ecs_world_get_flags(world) &
		 (EcsWorldReadonly | EcsWorldFrameInProgress)))
		return FLECS_ENET_ARGUMENT;
	/* Preflight public layouts before acquiring runtime or registering. */
#define DECL(name, type) {name, sizeof(type), _Alignof(type)}
	const struct declaration declarations[] = {
		DECL("flecs_enet.HostConfig", flecs_enet_host_config_t),
		DECL("flecs_enet.HostStatus", flecs_enet_host_status_t),
		DECL("flecs_enet.PeerStatus", flecs_enet_peer_status_t),
		DECL("flecs_enet.Request", flecs_enet_request_t),
		DECL("flecs_enet.Result", flecs_enet_operation_result_t),
		DECL("flecs_enet.Event", flecs_enet_event_t),
		DECL("flecs_enet.Packet", flecs_enet_packet_t),
		DECL("flecs_enet.ConnectionConfig",
			flecs_enet_connection_config_t),
		DECL("flecs_enet.PeerConfig", flecs_enet_peer_config_t),
		DECL("flecs_enet.StartStatus", flecs_enet_start_status_t),
		DECL("flecs_enet.Address", flecs_enet_address_t),
		DECL("flecs_enet.Limits", flecs_enet_limits_t),
		DECL("flecs_enet.Compression", flecs_enet_compression_t),
		DECL("flecs_enet.ResultCode", flecs_enet_result_t),
		DECL("flecs_enet.StartState", flecs_enet_start_state_t),
		DECL("flecs_enet.HostState", flecs_enet_host_state_t),
		DECL("flecs_enet.PeerState", flecs_enet_peer_state_t),
		DECL("flecs_enet.Delivery", flecs_enet_delivery_t),
		DECL("flecs_enet.Operation", flecs_enet_operation_t),
		DECL("flecs_enet.EventKind", flecs_enet_event_kind_t)
	};
#undef DECL
	const size_t count = sizeof(declarations) / sizeof(declarations[0]);
	for (size_t i = 0; i < count; i++) {
		ecs_entity_t id = ecs_lookup(world, declarations[i].name);
		if (!id)
			continue;
		const EcsComponent *c = ecs_get(world, id, EcsComponent);
		if (!c || c->size != (ecs_size_t)declarations[i].size ||
			c->alignment != (ecs_size_t)declarations[i].alignment)
			return FLECS_ENET_SCHEMA;
	}
	const char *tags[] = { "flecs_enet", "flecs_enet.Host",
		"flecs_enet.Receive", "flecs_enet.Send", "flecs_enet.Start" };
	for (size_t i = 0; i < 5; i++) {
		ecs_entity_t id = ecs_lookup(world, tags[i]);
		if (!id)
			continue;
		if (ecs_get(world, id, EcsComponent) ||
			(i == 0 && !ecs_has_id(world, id, EcsModule)) ||
			(i == 1 && (!ecs_has_id(world, id, EcsExclusive) ||
			 !ecs_has_pair(world, id,
				EcsOnDeleteTarget, EcsDelete))) ||
			(i >= 2 && i < 4 && !ecs_has_id(world, id, EcsPhase)))
			return FLECS_ENET_SCHEMA;
	}
	struct fe_context *existing = fe_context(world);
	if (!fe_reflection_valid(world, existing != NULL))
		return FLECS_ENET_SCHEMA;
	if (existing) {
		*out_ids = existing->ids;
		return FLECS_ENET_OK;
	}
	const char *private_names[] = { "flecs_enet.NativeHost",
		"flecs_enet.NativePeer", "flecs_enet.PayloadOwner",
		"flecs_enet.ReceiveSystem", "flecs_enet.SendSystem",
		"flecs_enet.Declaration" };
	for (size_t i = 0; i < 6; i++) {
		if (ecs_lookup(world, private_names[i]))
			return FLECS_ENET_SCHEMA;
	}
	ecs_entity_t rx = ecs_lookup(world, "flecs_enet.Receive");
	ecs_entity_t tx = ecs_lookup(world, "flecs_enet.Send");
	if (!rx)
		rx = UINT64_MAX;
	if (!tx)
		tx = UINT64_MAX - 1;
	struct phase_edges edges = {
		.from = { rx, EcsPreUpdate, tx, EcsPreStore },
		.to = { EcsOnLoad, rx, EcsPostUpdate, tx } };
	for (size_t i = 0; i < 4; i++) {
		if (depends(world, edges.to[i], edges.from[i], &edges, 0))
			return FLECS_ENET_SCHEMA;
	}
	struct fe_context *ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return FLECS_ENET_NOMEM;
	if (fe_initialize() < 0) {
		free(ctx);
		return FLECS_ENET_TRANSPORT;
	}
	flecs_enet_ids_t ids = {0};
	ids.module = ecs_entity_init(world,
		&(ecs_entity_desc_t){ .name = "flecs_enet" });
	ecs_add_id(world, ids.module, EcsModule);
	ecs_entity_t components[sizeof(declarations) / sizeof(declarations[0])];
	for (size_t i = 0; i < count; i++) {
		ecs_entity_t id = ecs_entity_init(world,
			&(ecs_entity_desc_t){ .name = declarations[i].name });
		ecs_component_desc_t desc = { .entity = id };
		desc.type.size = (ecs_size_t)declarations[i].size;
		desc.type.alignment = (ecs_size_t)declarations[i].alignment;
		components[i] = ecs_component_init(world, &desc);
	}
	ids.host_config = components[0];
	ids.host_status = components[1];
	ids.peer_status = components[2];
	ids.request = components[3];
	ids.result = components[4];
	ids.event = components[5];
	ids.packet = components[6];
	ids.connection_config = components[7];
	ids.peer_config = components[8];
	ids.start_status = components[9];
	ids.start = ecs_entity_init(world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.Start" });
	fe_reflect(world, &ids);
	ids.host = ecs_entity_init(world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.Host" });
	ecs_add_id(world, ids.host, EcsExclusive);
	ecs_add_pair(world, ids.host, EcsOnDeleteTarget, EcsDelete);
	ids.receive_phase = ecs_entity_init(world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.Receive" });
	ids.send_phase = ecs_entity_init(world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.Send" });
	ecs_add_id(world, ids.receive_phase, EcsPhase);
	ecs_add_id(world, ids.send_phase, EcsPhase);
	ecs_add_pair(world, ids.receive_phase, EcsDependsOn, EcsOnLoad);
	ecs_add_pair(world, EcsPreUpdate, EcsDependsOn, ids.receive_phase);
	ecs_add_pair(world, ids.send_phase, EcsDependsOn, EcsPostUpdate);
	ecs_add_pair(world, EcsPreStore, EcsDependsOn, ids.send_phase);
	/* Validate all namespace traits/layouts before mutating anything.
	 * Register private noncopyable native-owner types and lifetime hooks.
	 * Install single-threaded immediate Receive/Send systems matching
	 * hosts.
	 * Annotate out-of-query writes and merges for application visibility.
	 * Reject cyclic phase dependencies before installing automatic systems.
	 * Keep all query/system contexts world-local and release them at fini.
	 */
	ctx->ids = ids;
	fe_install_owners(world, ctx);
	install_system(world, ctx, "flecs_enet.ReceiveSystem",
		ids.receive_phase);
	install_system(world, ctx, "flecs_enet.SendSystem", ids.send_phase);
	*out_ids = ids;
	return FLECS_ENET_OK;
}
