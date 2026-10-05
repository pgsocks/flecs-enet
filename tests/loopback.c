#include "setup.h"
#include <flecs_enet.h>
#include <enet/enet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *kind, const char *detail)
{
	fprintf(stderr, "%s: %s\n", kind, detail);
	return 1;
}

static int receive(ecs_world_t *world, const flecs_enet_ids_t *ids,
	ecs_entity_t host, const void *bytes, size_t size)
{
	ecs_iter_t it = ecs_each_id(world, ids->event);
	while (ecs_each_next(&it)) {
		for (int32_t i = 0; i < it.count; i++) {
			ecs_entity_t entity = it.entities[i];
			const flecs_enet_event_t *e = ecs_get_id(world, entity,
				ids->event);
			if (e->kind != FLECS_ENET_EVENT_RECEIVE)
				continue;
			const flecs_enet_packet_t *p = ecs_get_id(world, entity,
				ids->packet);
			if (!p || p->size != size || e->channel != 1 ||
				memcmp(p->data, bytes, size)) {
				ecs_iter_fini(&it);
				fail("CONTRACT", "receive mismatch");
				return -1;
			}
			ecs_entity_t request;
			flecs_enet_result_t rc = flecs_enet_send(world, host,
				e->peer, 1, FLECS_ENET_RELIABLE,
				bytes, size, 32,
				&request);
			ecs_iter_fini(&it);
			if (rc != FLECS_ENET_OK) {
				fail("CONTRACT", "return packet admission");
				return -1;
			}
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2 || enet_initialize())
		return 2;
	int rc = 1;
	ecs_world_t *world = NULL;
	ENetAddress address = {0};
	enet_address_set_host_ip(&address, "127.0.0.1");
	ENetHost *upstream = enet_host_create(&address, 1, 2, 0, 0);
	if (!upstream) {
		rc = fail("ENVIRONMENT", "upstream UDP host unavailable");
		goto done;
	}
	if (!strcmp(argv[1], "probe")) {
		rc = 0;
		goto done;
	}
	bool fragmented = !strcmp(argv[1], "fragmentation");
	size_t size = fragmented ? 16384 : 32;
	unsigned char bytes[16384];
	for (size_t i = 0; i < size; i++)
		bytes[i] = (unsigned char)(i % 251);
	world = ecs_init();
	flecs_enet_ids_t ids;
	if (flecs_enet_import(world, &ids) != FLECS_ENET_OK) {
		rc = fail("FIXTURE", "module import");
		goto done;
	}
	ecs_entity_t host = ecs_new(world), request;
	flecs_enet_host_config_t config = {0};
	ecs_set_id(world, host, ids.host_config, sizeof(config), &config);
	if (test_open(world, host, 0, &request) != FLECS_ENET_OK) {
		rc = fail("FIXTURE", "OPEN admission");
		goto done;
	}
	ecs_progress(world, 0.01f);
	flecs_enet_address_t remote = {{127, 0, 0, 1}, upstream->address.port};
	if (test_connect(world, host, &remote, 2, 0, 31, &request) !=
		FLECS_ENET_OK) {
		rc = fail("FIXTURE", "CONNECT admission");
		goto done;
	}
	bool upstream_sent = false, received = false, returned = false;
	uint32_t start = enet_time_get();
	while (enet_time_get() - start < 8000 && !returned) {
		ecs_progress(world, 0.01f);
		ENetEvent e;
		while (enet_host_service(upstream, &e, 1) > 0) {
			if (e.type == ENET_EVENT_TYPE_CONNECT) {
				ENetPacket *p = enet_packet_create(bytes, size,
					ENET_PACKET_FLAG_RELIABLE);
				if (!p || enet_peer_send(e.peer, 1, p) < 0) {
					enet_packet_destroy(p);
					fail("FIXTURE", "upstream packet send");
					goto done;
				}
				upstream_sent = true;
			}
			if (e.type == ENET_EVENT_TYPE_RECEIVE) {
				returned = e.channelID == 1 &&
					e.packet->dataLength == size &&
					!memcmp(e.packet->data, bytes, size);
				enet_packet_destroy(e.packet);
			}
		}
		if (!received) {
			int n = receive(world, &ids, host, bytes, size);
			if (n < 0)
				goto done;
			received = n > 0;
		}
	}
	rc = upstream_sent && received && returned ? 0 :
		fail("CONTRACT", "upstream round trip did not complete in 8 s");
done:
	if (world)
		ecs_fini(world);
	if (upstream)
		enet_host_destroy(upstream);
	enet_deinitialize();
	return rc;
}
