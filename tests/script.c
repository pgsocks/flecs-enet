#include "fixture.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(expr) do { if (!(expr)) { \
	fprintf(stderr, "SCRIPT %d: %s\n", __LINE__, #expr); \
	failures++; goto done; } } while (0)

static int run(struct fixture *f, const char *code)
{
	return ecs_script_run(f->world, "configuration", code, NULL);
}

static const flecs_enet_start_status_t *status(struct fixture *f,
	ecs_entity_t entity)
{
	return ecs_get_id(f->world, entity, f->ids.start_status);
}

static void workflow(void)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	CHECK(!run(&f, "using flecs_enet\n"
		"client { HostConfig: {limits: {max_event_bytes: 4, "
		"max_packet_bytes: 4}}\n"
		" remote { ConnectionConfig: {address: {ip: [127,0,0,1], "
		"port: 9050}, channels: 2, data: 7, correlation: 91}\n"
		" Start } }\n"));
	ecs_entity_t host = ecs_lookup(f.world, "client");
	ecs_entity_t remote = ecs_lookup(f.world, "client.remote");
	ecs_progress(f.world, 0.01f);
	CHECK(!fe_test.hosts && !status(&f, host));
	CHECK(status(&f, remote) &&
		status(&f, remote)->state == FLECS_ENET_START_WAITING);
	CHECK(ecs_has_id(f.world, remote, f.ids.start));
	ecs_add_id(f.world, host, f.ids.start);
	ecs_progress(f.world, 0.01f);
	CHECK(fe_test.hosts == 1);
	CHECK(status(&f, host)->state == FLECS_ENET_START_FINISHED);
	CHECK(!status(&f, host)->code);
	CHECK(status(&f, remote) &&
		status(&f, remote)->state == FLECS_ENET_START_WAITING);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, remote)->state == FLECS_ENET_START_FINISHED);
	CHECK(!status(&f, remote)->code && status(&f, remote)->peer);
	CHECK(fixture_events(&f, host) == 2);
	ecs_entity_t peer = status(&f, remote)->peer;
	ecs_entity_t request = status(&f, remote)->request;
	const flecs_enet_peer_status_t *p = ecs_get_id(f.world, peer,
		f.ids.peer_status);
	CHECK(p && p->correlation == 91 && p->data == 7);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, remote)->peer == peer);
	ecs_add_id(f.world, remote, f.ids.start);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, remote)->code == FLECS_ENET_STATE);
	CHECK(ecs_is_alive(f.world, peer));
	CHECK(fixture_events(&f, host) == 2);
	CHECK(!status(&f, remote)->request);
	ENetHost *native = fe_test_latest();
	fe_test_inject(native, 0, ENET_EVENT_TYPE_CONNECT, NULL, 0, 0, 0);
	ecs_progress(f.world, 0.01f);
	fe_test_inject(native, 0, ENET_EVENT_TYPE_RECEIVE, "abc", 3, 0, 1);
	fe_test_inject(native, 0, ENET_EVENT_TYPE_RECEIVE, "def", 3, 0, 1);
	ecs_progress(f.world, 0.01f);
	CHECK(fe_test.packets == 2);
	ecs_delete(f.world, remote);
	/* Retained receive survives; pending native dispatch is released. */
	CHECK(fe_test.packets == 1);
	CHECK(!ecs_is_alive(f.world, peer));
	CHECK(!ecs_is_alive(f.world, request));
	CHECK(fixture_events(&f, host) == 4);
done:
	fixture_fini(&f);
}

static void runtime(void)
{
	struct fixture f = {0}, other = {0};
	f.world = ecs_init();
	memset(&f.ids, 0xff, sizeof(f.ids));
	fe_fail_initialize = 1;
	CHECK(flecs_enet_import(f.world, &f.ids) == FLECS_ENET_TRANSPORT);
	flecs_enet_ids_t zero = {0};
	CHECK(!memcmp(&f.ids, &zero, sizeof(zero)));
	CHECK(!ecs_lookup(f.world, "flecs_enet"));
	CHECK(!fe_test.runtimes && !fe_test.deinitializes);
	CHECK(flecs_enet_import(f.world, &f.ids) == FLECS_ENET_OK);
	CHECK(fe_test.runtimes == 1);
	flecs_enet_ids_t again;
	CHECK(flecs_enet_import(f.world, &again) == FLECS_ENET_OK);
	CHECK(fe_test.initializes == 2 && !memcmp(&again, &f.ids,
		sizeof(again)));
	CHECK(fixture_init(&other));
	CHECK(fe_test.runtimes == 2);
	CHECK(!run(&f, "using flecs_enet\n"
		"first { HostConfig: {}; Start }\n"));
	CHECK(!run(&other, "using flecs_enet\n"
		"client { HostConfig: {}; Start }\n"));
	ecs_progress(f.world, 0.01f);
	ecs_progress(other.world, 0.01f);
	CHECK(fe_test.hosts == 2);
	fixture_fini(&f);
	CHECK(fe_test.runtimes == 1 && fe_test.deinitializes == 1);
	ecs_entity_t host = ecs_lookup(other.world, "client"), request;
	CHECK(flecs_enet_flush(other.world, host, 0, &request) ==
		FLECS_ENET_OK);
	ecs_progress(other.world, 0.01f);
	CHECK(fe_test.hosts == 1 && fe_test.flushes == 1);
done:
	fixture_fini(&f);
	fixture_fini(&other);
}


static void events_clear(struct fixture *f, ecs_entity_t host)
{
	ecs_entity_t e;
	while ((e = fixture_event(f, host, 0)))
		ecs_delete(f->world, e);
}

static void examples(void)
{
	const char *files[] = { "listener", "client", "multiple", "tuning",
		"limits" };
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		struct fixture f = {0}, other = {0};
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s.flecs",
			FLECS_ENET_EXAMPLES, files[i]);
		CHECK(fixture_init(&f) && fixture_init(&other));
		CHECK(!ecs_script_run_file(f.world, path));
		CHECK(!ecs_script_run_file(other.world, path));
		ecs_progress(f.world, 0.01f);
		ecs_progress(f.world, 0.01f);
		ecs_progress(other.world, 0.01f);
		ecs_progress(other.world, 0.01f);
		CHECK(fe_test.hosts == 2);
		ecs_entity_t host = ecs_lookup(f.world, i == 0 || i == 4 ?
			"server" : "client");
		const flecs_enet_host_status_t *h = ecs_get_id(f.world, host,
			f.ids.host_status);
		CHECK(h && h->state == FLECS_ENET_HOST_OPEN);
		CHECK(h->active.peer_limit == (i == 2 ? 8 : i == 4 ? 16 : 32));
		CHECK(h->active.channel_limit == (i == 2 ? 3 : 2));
		CHECK(h->active.compression == (i == 3 ?
			FLECS_ENET_COMPRESSION_RANGE_CODER :
			FLECS_ENET_COMPRESSION_NONE));
		if (i == 4) {
			CHECK(h->active.limits.max_packet_bytes == 65536);
			CHECK(h->active.limits.max_event_bytes == 4194304);
			CHECK(h->active.limits.max_requests == 128);
			CHECK(h->address.ip[0] == 127 && h->address.port);
		}
		if (i == 1 || i == 3) {
			ecs_entity_t c = ecs_lookup(f.world, "client.server");
			CHECK(status(&f, c) && status(&f, c)->peer);
			CHECK(!status(&f, c)->code);
		}
		if (i == 2) {
			ecs_entity_t a = ecs_lookup(f.world, "client.primary");
			ecs_entity_t b = ecs_lookup(f.world,
				"client.secondary");
			CHECK(status(&f, a)->peer && status(&f, b)->peer);
			CHECK(status(&f, a)->peer != status(&f, b)->peer);
		}
	done:
		fixture_fini(&f);
		fixture_fini(&other);
	}
}

static void pressure(bool deletion)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	CHECK(!run(&f, "using flecs_enet\n"
		"client { HostConfig: {limits: {max_requests: 1, "
		"max_events: 1}};"
		" Start\n remote { ConnectionConfig: "
		"{address: {ip: [127,0,0,1], port: 9050}}; Start } }\n"));
	ecs_entity_t host = ecs_lookup(f.world, "client");
	ecs_entity_t c = ecs_lookup(f.world, "client.remote");
	ecs_progress(f.world, 0.01f);
	ecs_entity_t flush;
	CHECK(flecs_enet_flush(f.world, host, 0, &flush) == FLECS_ENET_OK);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, c)->state == FLECS_ENET_START_WAITING);
	CHECK(status(&f, c)->code == FLECS_ENET_BACKPRESSURE);
	CHECK(ecs_has_id(f.world, c, f.ids.start));
	ecs_delete(f.world, flush);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, c)->state == FLECS_ENET_START_PENDING);
	CHECK(!ecs_has_id(f.world, c, f.ids.start));
	ecs_entity_t request = status(&f, c)->request;
	ecs_add_id(f.world, c, f.ids.start);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, c)->request == request);
	CHECK(status(&f, c)->state == FLECS_ENET_START_PENDING);
	CHECK(!ecs_has_id(f.world, c, f.ids.start));
	if (deletion) {
		ecs_delete(f.world, c);
		CHECK(!ecs_is_alive(f.world, request));
		const flecs_enet_host_status_t *h = ecs_get_id(f.world, host,
			f.ids.host_status);
		CHECK(!h->pending_requests);
		events_clear(&f, host);
		ecs_progress(f.world, 0.01f);
		CHECK(!fixture_peer(&f, host, 0));
	} else {
		events_clear(&f, host);
		ecs_progress(f.world, 0.01f);
		CHECK(status(&f, c)->state == FLECS_ENET_START_FINISHED);
		CHECK(status(&f, c)->peer && !status(&f, c)->code);
		CHECK(ecs_get_id(f.world, request, f.ids.result));
		CHECK(fixture_events(&f, host) == 1);
	}
done:
	fixture_fini(&f);
}

static void failures_restart(bool connection)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	CHECK(!run(&f, "using flecs_enet\n"
		"client { HostConfig: {}; Start\n"
		"remote { ConnectionConfig: "
		"{address: {ip: [127,0,0,1], port: 9050}}; Start } }\n"));
	ecs_entity_t host = ecs_lookup(f.world, "client");
	ecs_entity_t c = ecs_lookup(f.world, "client.remote");
	ecs_entity_t target = connection ? c : host;
	if (connection)
		fe_fail_connect = 1;
	else
		fe_fail_host = 1;
	ecs_progress(f.world, 0.01f);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, target)->state == FLECS_ENET_START_FINISHED);
	CHECK(status(&f, target)->code == FLECS_ENET_TRANSPORT);
	ecs_entity_t failed = status(&f, target)->request;
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, target)->request == failed);
	CHECK(!ecs_has_id(f.world, target, f.ids.start));
	ecs_add_id(f.world, target, f.ids.start);
	ecs_progress(f.world, 0.01f);
	CHECK(!status(&f, target)->code);
	CHECK(status(&f, target)->request != failed);
	if (connection) {
		ecs_entity_t old_peer = status(&f, c)->peer, request;
		CHECK(flecs_enet_disconnect(f.world, host, old_peer,
			FLECS_ENET_RESET, 0, 0, &request) == FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		ecs_progress(f.world, 0.01f);
		CHECK(status(&f, c)->peer == old_peer);
		CHECK(!ecs_has_id(f.world, c, f.ids.start));
		ecs_add_id(f.world, c, f.ids.start);
		ecs_progress(f.world, 0.01f);
		CHECK(status(&f, c)->peer != old_peer && status(&f, c)->peer);
		CHECK(ecs_is_alive(f.world, old_peer));
		ecs_delete(f.world, c);
		CHECK(!ecs_is_alive(f.world, old_peer));
	} else {
		ecs_add_id(f.world, host, f.ids.start);
		ecs_progress(f.world, 0.01f);
		CHECK(status(&f, host)->code == FLECS_ENET_STATE);
		ecs_entity_t request;
		CHECK(flecs_enet_close(f.world, host, 0, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		flecs_enet_host_config_t config = { .peer_limit = 8 };
		ecs_set_id(f.world, host, f.ids.host_config, sizeof(config),
			&config);
		ecs_add_id(f.world, host, f.ids.start);
		ecs_progress(f.world, 0.01f);
		const flecs_enet_host_status_t *h = ecs_get_id(f.world, host,
			f.ids.host_status);
		CHECK(h->active.peer_limit == 8 && !status(&f, host)->code);
	}
done:
	fixture_fini(&f);
}

static void tuning(void)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	CHECK(!run(&f, "using flecs_enet\n"
		"client { HostConfig: {}; PeerConfig: {ping_interval_ms: 111,"
		"timeout_limit: 32, timeout_minimum_ms: 5, "
		"timeout_maximum_ms: 30,"
		"throttle_interval_ms: 50, acceleration: 2, "
		"deceleration: 3}; Start\n"
		"a { ConnectionConfig: {address: {ip: "
		"[127,0,0,1], port: 9050}} }\n"
		"b { ConnectionConfig: {address: {ip: "
		"[127,0,0,1], port: 9051}};"
		"PeerConfig: {ping_interval_ms: 222} }\n"
		"c { ConnectionConfig: {address: {ip: [127,0,0,1], "
		"port: 9052}}; PeerConfig: {} } }\n"));
	ecs_entity_t host = ecs_lookup(f.world, "client");
	ecs_entity_t a = ecs_lookup(f.world, "client.a");
	ecs_entity_t b = ecs_lookup(f.world, "client.b");
	ecs_entity_t c = ecs_lookup(f.world, "client.c");
	ecs_progress(f.world, 0.01f);
	ENetHost *native = fe_test_latest();
	flecs_enet_peer_config_t edit = { .ping_interval_ms = 999 };
	ecs_set_id(f.world, host, f.ids.peer_config, sizeof(edit), &edit);
	ecs_add_id(f.world, a, f.ids.start);
	ecs_add_id(f.world, b, f.ids.start);
	ecs_add_id(f.world, c, f.ids.start);
	ecs_progress(f.world, 0.01f);
	CHECK(!status(&f, a)->code && !status(&f, b)->code);
	CHECK(!status(&f, c)->code);
	ENetPeer *pa = NULL, *pb = NULL, *pc = NULL;
	for (size_t i = 0; i < native->peerCount; i++) {
		if (native->peers[i].address.port == 9050)
			pa = &native->peers[i];
		if (native->peers[i].address.port == 9051)
			pb = &native->peers[i];
		if (native->peers[i].address.port == 9052)
			pc = &native->peers[i];
	}
	CHECK(pa && pb && pc);
	CHECK(pc->pingInterval == ENET_PEER_PING_INTERVAL);
	CHECK(pc->timeoutMinimum == ENET_PEER_TIMEOUT_MINIMUM);
	CHECK(pa->pingInterval == 111 && pa->timeoutMinimum == 5);
	CHECK(pa->packetThrottleInterval == 50);
	CHECK(pa->packetThrottleAcceleration == 2);
	CHECK(pb->pingInterval == 222 &&
		pb->timeoutMinimum == ENET_PEER_TIMEOUT_MINIMUM);
	CHECK(pb->packetThrottleInterval == ENET_PEER_PACKET_THROTTLE_INTERVAL);
	fe_test_inject(native, 3, ENET_EVENT_TYPE_CONNECT, NULL, 0, 0, 0);
	ecs_progress(f.world, 0.01f);
	CHECK(native->peers[3].pingInterval == 111);
	CHECK(native->peers[3].timeoutMaximum == 30);
done:
	fixture_fini(&f);
}

static void invalid(void)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	CHECK(!run(&f, "using flecs_enet\n"
		"bad { HostConfig: {}; PeerConfig: "
		"{timeout_limit: 1}; Start }\n"
		"badThrottle { HostConfig: {}; PeerConfig: {acceleration: 33};"
		"Start }\n"
		"orphan { ConnectionConfig: {}; Start }\n"));
	ecs_progress(f.world, 0.01f);
	CHECK(!fe_test.hosts && !fe_test.tuning);
	CHECK(status(&f, ecs_lookup(f.world, "bad"))->code ==
		FLECS_ENET_ARGUMENT);
	CHECK(status(&f, ecs_lookup(f.world, "badThrottle"))->code ==
		FLECS_ENET_ARGUMENT);
	CHECK(status(&f, ecs_lookup(f.world, "orphan"))->code ==
		FLECS_ENET_NOT_FOUND);
	CHECK(!run(&f, "using flecs_enet\n"
		"client { HostConfig: {}; Start\n"
		"bad { ConnectionConfig: {address: {ip: "
		"[127,0,0,1], port: 9050}};"
		"PeerConfig: {throttle_interval_ms: 10, "
		"deceleration: 33}; Start }\n"
		"badAddress { ConnectionConfig: {}; Start } }\n"));
	ecs_progress(f.world, 0.01f);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, ecs_lookup(f.world, "client.bad"))->code ==
		FLECS_ENET_ARGUMENT);
	CHECK(status(&f, ecs_lookup(f.world, "client.badAddress"))->code ==
		FLECS_ENET_ARGUMENT);
	CHECK(!fixture_peer(&f, ecs_lookup(f.world, "client"), 0));
done:
	fixture_fini(&f);
}

static void cancellation(void)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	CHECK(!run(&f, "using flecs_enet\n"
		"client { HostConfig: {limits: {max_events: 1}}; Start\n"
		"c { ConnectionConfig: {address: {ip: "
		"[127,0,0,1], port: 9050}};"
		"Start } }\n"));
	ecs_entity_t host = ecs_lookup(f.world, "client");
	ecs_entity_t c = ecs_lookup(f.world, "client.c");
	ecs_progress(f.world, 0.01f);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, c)->state == FLECS_ENET_START_PENDING);
	ecs_delete(f.world, status(&f, c)->request);
	CHECK(status(&f, c)->state == FLECS_ENET_START_FINISHED);
	CHECK(status(&f, c)->code == FLECS_ENET_STATE);
	events_clear(&f, host);
	ecs_progress(f.world, 0.01f);
	CHECK(!fixture_peer(&f, host, 0));
	ecs_add_id(f.world, c, f.ids.start);
	ecs_progress(f.world, 0.01f);
	CHECK(status(&f, c)->peer && !status(&f, c)->code);
	ecs_entity_t peer = status(&f, c)->peer;
	ecs_delete(f.world, host);
	CHECK(!ecs_is_alive(f.world, c) && !ecs_is_alive(f.world, peer));
	CHECK(!fe_test.hosts && !fe_test.packets);
done:
	fixture_fini(&f);
}

static void schema(void)
{
	struct fixture f = { .world = ecs_init() };
	ecs_entity_t module = ecs_entity_init(f.world,
		&(ecs_entity_desc_t){ .name = "flecs_enet" });
	ecs_add_id(f.world, module, EcsModule);
	ecs_entity_t config = ecs_entity_init(f.world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.PeerConfig" });
	ecs_component_init(f.world, &(ecs_component_desc_t){ .entity = config,
		.type.size = sizeof(flecs_enet_peer_config_t),
		.type.alignment = _Alignof(flecs_enet_peer_config_t) });
	ecs_struct_init(f.world, &(ecs_struct_desc_t){ .entity = config,
		.members = {{ .name = "wrong", .type = ecs_id(ecs_u32_t) }} });
	CHECK(flecs_enet_import(f.world, &f.ids) == FLECS_ENET_SCHEMA);
	CHECK(!f.ids.module && !fe_test.initializes);
	CHECK(!ecs_lookup(f.world, "flecs_enet.HostConfig"));
done:
	fixture_fini(&f);
}

static void byte_width(void)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	if (sizeof(size_t) > 4) {
		CHECK(!run(&f, "using flecs_enet\n"
			"host { HostConfig: {limits: {"
			"max_event_bytes: 4294967297, "
			"max_outgoing_bytes: 4294967298, "
			"max_waiting_bytes: 4294967299}}; Start }\n"));
		ecs_entity_t host = ecs_lookup(f.world, "host");
		ecs_progress(f.world, 0.01f);
		const flecs_enet_host_status_t *h = ecs_get_id(f.world, host,
			f.ids.host_status);
		CHECK(h && h->state == FLECS_ENET_HOST_OPEN);
		CHECK(h->active.limits.max_event_bytes == UINT64_C(4294967297));
		CHECK(h->active.limits.max_outgoing_bytes ==
			UINT64_C(4294967298));
		CHECK(h->active.limits.max_waiting_bytes ==
			UINT64_C(4294967299));
	}
done:
	fixture_fini(&f);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	fe_test_reset();
	const char *name = argv[1];
	if (!strcmp(name, "runtime"))
		runtime();
	else if (!strcmp(name, "workflow"))
		workflow();
	else if (!strcmp(name, "examples"))
		examples();
	else if (!strcmp(name, "pressure"))
		pressure(false);
	else if (!strcmp(name, "deletion"))
		pressure(true);
	else if (!strcmp(name, "host_restart"))
		failures_restart(false);
	else if (!strcmp(name, "connection_restart"))
		failures_restart(true);
	else if (!strcmp(name, "tuning"))
		tuning();
	else if (!strcmp(name, "invalid"))
		invalid();
	else if (!strcmp(name, "cancellation"))
		cancellation();
	else if (!strcmp(name, "byte_width"))
		byte_width();
	else if (!strcmp(name, "schema"))
		schema();
	else
		return 2;
	if (fe_test.hosts || fe_test.packets || fe_test.runtimes)
		failures++;
	return failures ? 1 : 0;
}
