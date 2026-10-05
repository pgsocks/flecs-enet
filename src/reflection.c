#include "internal.h"
#include <stddef.h>
#include <string.h>

/* Explicit offsets preserve the public C layouts, including byte arrays and
 * size_t fields. Reflection never substitutes script-created layouts.
 */
#define MEMBER(ctype, field, meta, n) { .name = #field, .type = meta, \
	.count = n, .offset = offsetof(ctype, field), .use_offset = true }

static ecs_entity_t reflect_enum(ecs_world_t *world,
	const ecs_enum_desc_t *desc, bool check, bool complete, bool *valid)
{
	if (!check)
		return ecs_enum_init(world, desc);
	const EcsEnum *meta = desc->entity ?
		ecs_get(world, desc->entity, EcsEnum) : NULL;
	const EcsType *type = desc->entity ?
		ecs_get(world, desc->entity, EcsType) : NULL;
	if (type && type->kind != EcsEnumType)
		*valid = false;
	if (!meta) {
		if (complete || type)
			*valid = false;
		for (int32_t i = 0; desc->entity &&
			desc->constants[i].name; i++) {
			if (ecs_lookup_child(world, desc->entity,
				desc->constants[i].name))
				*valid = false;
		}
		return desc->entity;
	}
	int32_t n = 0;
	while (desc->constants[n].name)
		n++;
	const EcsConstants *constants = ecs_get(world, desc->entity,
		EcsConstants);
	if (!constants || ecs_map_count(constants->constants) != n ||
		meta->underlying_type != ecs_id(ecs_i32_t))
		*valid = false;
	for (int32_t i = 0; i < n; i++) {
		ecs_entity_t e = ecs_lookup_child(world, desc->entity,
			desc->constants[i].name);
		const int32_t *v = e ? ecs_get_id(world, e,
			ecs_pair(EcsConstant, ecs_id(ecs_i32_t))) : NULL;
		if (!v || *v != desc->constants[i].value)
			*valid = false;
	}
	return desc->entity;
}

static void reflect_struct(ecs_world_t *world,
	const ecs_struct_desc_t *desc, bool check, bool complete, bool *valid)
{
	if (!check) {
		ecs_struct_init(world, desc);
		return;
	}
	const EcsStruct *meta = desc->entity ?
		ecs_get(world, desc->entity, EcsStruct) : NULL;
	const EcsType *type = desc->entity ?
		ecs_get(world, desc->entity, EcsType) : NULL;
	if (type && type->kind != EcsStructType)
		*valid = false;
	if (!meta) {
		if (complete || type)
			*valid = false;
		for (int32_t i = 0; desc->entity &&
			desc->members[i].name; i++) {
			if (ecs_lookup_child(world, desc->entity,
				desc->members[i].name))
				*valid = false;
		}
		return;
	}
	int32_t n = 0;
	while (desc->members[n].name)
		n++;
	if (ecs_vec_count(&meta->members) != n) {
		*valid = false;
		return;
	}
	const ecs_member_t *members = ecs_vec_first(&meta->members);
	for (int32_t i = 0; i < n; i++) {
		const ecs_member_t *a = &members[i], *b = &desc->members[i];
		if (strcmp(a->name, b->name) || a->type != b->type ||
			a->offset != b->offset ||
			a->count != b->count)
			*valid = false;
	}
}

static bool reflection(ecs_world_t *world, const flecs_enet_ids_t *ids,
	bool check, bool complete)
{
	bool valid = true;
	ecs_entity_t compression = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Compression"),
		.constants = {
			{ .name = "None", .value = 0 },
			{ .name = "RangeCoder", .value = 1 },
		} }, check, complete, &valid);
	ecs_entity_t result_code = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.ResultCode"),
		.constants = {
			{ .name = "OK", .value = 0 },
			{ .name = "ARGUMENT", .value = 1 },
			{ .name = "STATE", .value = 2 },
			{ .name = "NOT_FOUND", .value = 3 },
			{ .name = "BACKPRESSURE", .value = 4 },
			{ .name = "NOMEM", .value = 5 },
			{ .name = "TRANSPORT", .value = 6 },
			{ .name = "PARTIAL", .value = 7 },
			{ .name = "SCHEMA", .value = 8 },
			{ .name = "LIMIT", .value = 9 },
			{ .name = "NOT_IMPLEMENTED", .value = 10 },
		} }, check, complete, &valid);
	ecs_entity_t start_state = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.StartState"),
		.constants = {
			{ .name = "Waiting", .value = 0 },
			{ .name = "Pending", .value = 1 },
			{ .name = "Finished", .value = 2 },
		} }, check, complete, &valid);
	ecs_entity_t host_state = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.HostState"),
		.constants = {
			{ .name = "Closed", .value = 0 },
			{ .name = "Open", .value = 1 },
			{ .name = "Faulted", .value = 2 },
		} }, check, complete, &valid);
	ecs_entity_t peer_state = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.PeerState"),
		.constants = {
			{ .name = "Connecting", .value = 0 },
			{ .name = "Connected", .value = 1 },
			{ .name = "Disconnecting", .value = 2 },
			{ .name = "Disconnected", .value = 3 },
		} }, check, complete, &valid);
	ecs_entity_t delivery = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Delivery"),
		.constants = {
			{ .name = "Reliable", .value = 0 },
			{ .name = "Unreliable", .value = 1 },
			{ .name = "Unsequenced", .value = 2 },
			{ .name = "UnreliableFragment", .value = 3 },
		} }, check, complete, &valid);
	ecs_entity_t operation = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Operation"),
		.constants = {
			{ .name = "Open", .value = 0 },
			{ .name = "Close", .value = 1 },
			{ .name = "Connect", .value = 2 },
			{ .name = "Send", .value = 3 },
			{ .name = "Broadcast", .value = 4 },
			{ .name = "Flush", .value = 5 },
			{ .name = "Disconnect", .value = 6 },
			{ .name = "DisconnectNow", .value = 7 },
			{ .name = "DisconnectLater", .value = 8 },
			{ .name = "Reset", .value = 9 },
			{ .name = "Bandwidth", .value = 10 },
			{ .name = "Compression", .value = 11 },
			{ .name = "Ping", .value = 12 },
			{ .name = "PingInterval", .value = 13 },
			{ .name = "Timeout", .value = 14 },
			{ .name = "Throttle", .value = 15 },
		} }, check, complete, &valid);
	ecs_entity_t event_kind = reflect_enum(world, &(ecs_enum_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.EventKind"),
		.constants = {
			{ .name = "Connect", .value = 0 },
			{ .name = "Receive", .value = 1 },
			{ .name = "Disconnect", .value = 2 },
			{ .name = "Result", .value = 3 },
		} }, check, complete, &valid);
	ecs_entity_t address = ecs_lookup(world, "flecs_enet.Address");
	ecs_entity_t limits = ecs_lookup(world, "flecs_enet.Limits");
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Address"),
		.members = {
			MEMBER(flecs_enet_address_t,
				ip, ecs_id(ecs_u8_t), 4),
			MEMBER(flecs_enet_address_t,
				port, ecs_id(ecs_u16_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Limits"),
		.members = {
			MEMBER(flecs_enet_limits_t,
				max_requests, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_limits_t,
				max_events, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_limits_t,
				max_outgoing_packets, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_limits_t,
				events_per_progress, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_limits_t,
				max_event_bytes, ecs_id(ecs_uptr_t), 0),
			MEMBER(flecs_enet_limits_t,
				max_outgoing_bytes, ecs_id(ecs_uptr_t), 0),
			MEMBER(flecs_enet_limits_t,
				max_packet_bytes, ecs_id(ecs_uptr_t), 0),
			MEMBER(flecs_enet_limits_t,
				max_waiting_bytes, ecs_id(ecs_uptr_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.HostConfig"),
		.members = {
			MEMBER(flecs_enet_host_config_t,
				listen, ecs_id(ecs_bool_t), 0),
			MEMBER(flecs_enet_host_config_t,
				address, address, 0),
			MEMBER(flecs_enet_host_config_t,
				peer_limit, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_host_config_t,
				channel_limit, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_host_config_t,
				incoming_bandwidth, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_host_config_t,
				outgoing_bandwidth, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_host_config_t,
				compression, compression, 0),
			MEMBER(flecs_enet_host_config_t,
				limits, limits, 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.ConnectionConfig"),
		.members = {
			MEMBER(flecs_enet_connection_config_t,
				address, address, 0),
			MEMBER(flecs_enet_connection_config_t,
				channels, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_connection_config_t,
				data, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_connection_config_t,
				correlation, ecs_id(ecs_u64_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.PeerConfig"),
		.members = {
			MEMBER(flecs_enet_peer_config_t,
				ping_interval_ms, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_config_t,
				timeout_limit, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_config_t,
				timeout_minimum_ms, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_config_t,
				timeout_maximum_ms, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_config_t,
				throttle_interval_ms, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_config_t,
				acceleration, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_config_t,
				deceleration, ecs_id(ecs_u32_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.StartStatus"),
		.members = {
			MEMBER(flecs_enet_start_status_t,
				state, start_state, 0),
			MEMBER(flecs_enet_start_status_t,
				code, result_code, 0),
			MEMBER(flecs_enet_start_status_t,
				request, ecs_id(ecs_entity_t), 0),
			MEMBER(flecs_enet_start_status_t,
				peer, ecs_id(ecs_entity_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.HostStatus"),
		.members = {
			MEMBER(flecs_enet_host_status_t,
				state, host_state, 0),
			MEMBER(flecs_enet_host_status_t,
				fault, result_code, 0),
			MEMBER(flecs_enet_host_status_t,
				address, address, 0),
			MEMBER(flecs_enet_host_status_t,
				active, ids->host_config, 0),
			MEMBER(flecs_enet_host_status_t,
				last_sequence, ecs_id(ecs_u64_t), 0),
			MEMBER(flecs_enet_host_status_t,
				pending_requests, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_host_status_t,
				retained_events, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_host_status_t,
				outgoing_packets, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_host_status_t,
				event_bytes, ecs_id(ecs_uptr_t), 0),
			MEMBER(flecs_enet_host_status_t,
				outgoing_bytes, ecs_id(ecs_uptr_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.PeerStatus"),
		.members = {
			MEMBER(flecs_enet_peer_status_t,
				state, peer_state, 0),
			MEMBER(flecs_enet_peer_status_t,
				address, address, 0),
			MEMBER(flecs_enet_peer_status_t,
				channel_count, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_status_t,
				data, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_peer_status_t,
				correlation, ecs_id(ecs_u64_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Result"),
		.members = {
			MEMBER(flecs_enet_operation_result_t,
				code, result_code, 0),
			MEMBER(flecs_enet_operation_result_t,
				operation, operation, 0),
			MEMBER(flecs_enet_operation_result_t,
				peer, ecs_id(ecs_entity_t), 0),
			MEMBER(flecs_enet_operation_result_t,
				attempted, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_operation_result_t,
				accepted, ecs_id(ecs_u32_t), 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Event"),
		.members = {
			MEMBER(flecs_enet_event_t,
				kind, event_kind, 0),
			MEMBER(flecs_enet_event_t,
				sequence, ecs_id(ecs_u64_t), 0),
			MEMBER(flecs_enet_event_t,
				correlation, ecs_id(ecs_u64_t), 0),
			MEMBER(flecs_enet_event_t,
				request, ecs_id(ecs_entity_t), 0),
			MEMBER(flecs_enet_event_t,
				peer, ecs_id(ecs_entity_t), 0),
			MEMBER(flecs_enet_event_t,
				channel, ecs_id(ecs_u8_t), 0),
			MEMBER(flecs_enet_event_t,
				data, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_event_t,
				result, result_code, 0),
		} }, check, complete, &valid);
	reflect_struct(world, &(ecs_struct_desc_t){
		.entity = ecs_lookup(world, "flecs_enet.Request"),
		.members = {
			MEMBER(flecs_enet_request_t,
				operation, operation, 0),
			MEMBER(flecs_enet_request_t,
				peer, ecs_id(ecs_entity_t), 0),
			MEMBER(flecs_enet_request_t,
				correlation, ecs_id(ecs_u64_t), 0),
			MEMBER(flecs_enet_request_t,
				address, address, 0),
			MEMBER(flecs_enet_request_t,
				channels, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				data, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				channel, ecs_id(ecs_u8_t), 0),
			MEMBER(flecs_enet_request_t,
				delivery, delivery, 0),
			MEMBER(flecs_enet_request_t,
				compression, compression, 0),
			MEMBER(flecs_enet_request_t,
				incoming_bandwidth, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				outgoing_bandwidth, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				interval_ms, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				timeout_limit, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				timeout_minimum_ms, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				timeout_maximum_ms, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				acceleration, ecs_id(ecs_u32_t), 0),
			MEMBER(flecs_enet_request_t,
				deceleration, ecs_id(ecs_u32_t), 0),
		} }, check, complete, &valid);
	return valid;
}

void fe_reflect(ecs_world_t *world, const flecs_enet_ids_t *ids)
{
	reflection(world, ids, false, false);
}

bool fe_reflection_valid(ecs_world_t *world, bool complete)
{
	flecs_enet_ids_t ids = { .host_config = ecs_lookup(world,
		"flecs_enet.HostConfig") };
	return reflection(world, &ids, true, complete);
}
