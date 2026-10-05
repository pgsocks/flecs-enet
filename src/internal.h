#ifndef FLECS_ENET_INTERNAL_H
#define FLECS_ENET_INTERNAL_H
#include <flecs_enet.h>
#include "boundary.h"

struct fe_context;
struct fe_payload;
struct fe_peer;
/* Private owners: never expose or shallow-copy native pointers into ECS. */
struct fe_host {
	ENetHost *native;
	ENetEvent pending_event;
	bool has_pending, alive, closing;
	flecs_enet_host_status_t status;
	flecs_enet_peer_config_t peer_defaults;
	uint64_t next_ticket;
	size_t refs;
	ecs_entity_t entity, closing_request;
	ecs_world_t *world;
	struct fe_context *ctx;
	struct fe_host *next;
	struct fe_payload *requests;
	struct fe_peer *peers;
};
struct fe_peer {
	ENetPeer *native;
	struct fe_host *host;
	ecs_entity_t entity;
	flecs_enet_peer_status_t status;
	bool diagnostic;
	flecs_enet_result_t diagnostic_result;
	struct fe_peer *next;
};
struct fe_payload {
	void *bytes;
	size_t size, reservation;
	ENetPacket *native;
	struct fe_host *host;
	ecs_entity_t entity, startup;
	flecs_enet_peer_config_t peer_config;
	bool pending, event, tuned;
	uint64_t ticket;
	flecs_enet_request_t desc;
	struct fe_payload *next, *completion;
};
struct fe_owner { void *ptr; };
struct fe_context {
	flecs_enet_ids_t ids;
	ecs_entity_t host_owner, peer_owner, payload_owner, declaration;
	struct fe_host *hosts;
};

struct fe_context *fe_context(ecs_world_t *);
bool fe_world_valid(ecs_world_t *);
flecs_enet_result_t fe_normalize(const flecs_enet_host_config_t *,
	flecs_enet_host_config_t *);
void fe_sync(struct fe_host *);
void fe_startup(ecs_world_t *, struct fe_context *);
void fe_start_finish(struct fe_payload *, flecs_enet_result_t, ecs_entity_t);
flecs_enet_result_t fe_validate_peer(const flecs_enet_peer_config_t *);
void fe_tune_peer(ENetPeer *, const flecs_enet_peer_config_t *);
bool fe_reflection_valid(ecs_world_t *, bool);
void fe_reflect(ecs_world_t *, const flecs_enet_ids_t *);
void fe_ref(struct fe_host *);
void fe_unref(struct fe_host *);
void fe_install_owners(ecs_world_t *, struct fe_context *);
void fe_attach(ecs_world_t *, ecs_entity_t, ecs_entity_t, void *);
struct fe_host *fe_find_host(struct fe_context *, ecs_entity_t);
struct fe_peer *fe_find_peer(struct fe_host *, ecs_entity_t);
struct fe_peer *fe_new_peer(struct fe_host *, ENetPeer *, uint64_t,
	const flecs_enet_peer_config_t *);
void fe_peer_sync(struct fe_peer *);
flecs_enet_result_t fe_diagnostic(struct fe_peer *);
void fe_finish(struct fe_payload *, flecs_enet_operation_result_t);
flecs_enet_result_t fe_event_room(struct fe_host *, size_t);
flecs_enet_result_t fe_receive(ecs_world_t *, ecs_entity_t, struct fe_host *);
flecs_enet_result_t fe_outgoing(ecs_world_t *, ecs_entity_t, struct fe_host *);
flecs_enet_result_t fe_publish(ecs_world_t *, ecs_entity_t, struct fe_host *,
	const ENetEvent *);
flecs_enet_result_t fe_teardown(ecs_world_t *, ecs_entity_t, struct fe_host *);
flecs_enet_result_t fe_reserve(struct fe_host *, size_t, bool);
ENetAddress fe_address(const flecs_enet_address_t *);
flecs_enet_address_t fe_public_address(const ENetAddress *);
#endif
