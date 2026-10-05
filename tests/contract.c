#include "setup.h"
#include "fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(expr) do { if (!(expr)) { \
	fprintf(stderr, "CONTRACT %s:%d: %s\n", __FILE__, __LINE__, #expr); \
	failures++; goto done; } } while (0)

static bool prerequisite(bool condition, const char *detail)
{
	if (condition)
		return true;
	fprintf(stderr, "FIXTURE prerequisite: %s\n", detail);
	failures++;
	return false;
}

static bool start(struct fixture *f, flecs_enet_host_config_t config)
{
	if (!prerequisite(fixture_init(f), "module import"))
		return false;
	fixture_host(f, 0, &config);
	ecs_entity_t request = 0;
	flecs_enet_result_t r = test_open(f->world, f->host[0], 11,
		&request);
	if (!prerequisite(r == FLECS_ENET_OK,
		"OPEN admission"))
		return false;
	ecs_progress(f->world, 0.01f);
	const flecs_enet_host_status_t *s = ecs_get_id(f->world, f->host[0],
		f->ids.host_status);
	return prerequisite(s && s->state == FLECS_ENET_HOST_OPEN,
		"OPEN execution");
}

static bool connected(struct fixture *f, unsigned count)
{
	ENetHost *native = fe_test_latest();
	if (!prerequisite(native != NULL, "instrumented native host"))
		return false;
	for (unsigned i = 0; i < count; i++)
		fe_test_inject(native, i, ENET_EVENT_TYPE_CONNECT,
			NULL, 0, 0, 0);
	ecs_progress(f->world, 0.01f);
	return prerequisite(fixture_peer(f, f->host[0], count - 1) != 0,
		"connect-event publication");
}

static const flecs_enet_operation_result_t *result(struct fixture *f,
	ecs_entity_t request)
{
	return ecs_get_id(f->world, request, f->ids.result);
}

static void clear_events(struct fixture *f)
{
	ecs_entity_t e;
	while ((e = fixture_event(f, f->host[0], 0)))
		ecs_delete(f->world, e);
}

struct update_context {
	struct fixture *fixture;
	ecs_entity_t peer, request;
	bool input_visible;
	unsigned sends_before;
	flecs_enet_result_t admission;
};

static void application_update(ecs_iter_t *it)
{
	struct update_context *ctx = it->ctx;
	struct fixture *f = ctx->fixture;
	ecs_entity_t entity = fixture_event(f, f->host[0], 0);
	if (entity) {
		const flecs_enet_event_t *event = ecs_get_id(f->world, entity,
			f->ids.event);
		ctx->input_visible = event->kind == FLECS_ENET_EVENT_RECEIVE;
	}
	ctx->sends_before = fe_test.sends;
	ctx->admission = flecs_enet_send(it->world, f->host[0], ctx->peer, 0,
		FLECS_ENET_RELIABLE, "reply", 5, 35, &ctx->request);
}

static void metadata(const char *name)
{
	struct fixture f = {0}, other = {0};
	CHECK(fixture_init(&f));
	if (!strcmp(name, "import_idempotent")) {
		flecs_enet_ids_t again;
		CHECK(flecs_enet_import(f.world, &again) == FLECS_ENET_OK);
		CHECK(!memcmp(&again, &f.ids, sizeof(again)));
		CHECK(fe_test.hosts == 0);
	} else if (!strcmp(name, "world_isolation")) {
		CHECK(fixture_init(&other));
		flecs_enet_host_config_t c = { .peer_limit = 8 };
		fixture_host(&f, 0, &c);
		CHECK(ecs_count_id(f.world, f.ids.host_config) == 1);
		CHECK(ecs_count_id(other.world, other.ids.host_config) == 0);
		fixture_fini(&f);
		CHECK(ecs_is_alive(other.world, other.ids.host_config));
	} else if (!strcmp(name, "import_arguments")) {
		flecs_enet_ids_t ids;
		memset(&ids, 255, sizeof(ids));
		CHECK(flecs_enet_import(NULL, &ids) == FLECS_ENET_ARGUMENT);
		CHECK(ids.module == 0 && ids.packet == 0);
		ecs_defer_begin(f.world);
		CHECK(flecs_enet_import(f.world, &ids) == FLECS_ENET_ARGUMENT);
		ecs_defer_end(f.world);
	} else if (!strcmp(name, "schema_conflict")) {
		fixture_fini(&f);
		f.world = ecs_init();
		ecs_entity_t id = ecs_entity_init(f.world,
			&(ecs_entity_desc_t){ .name = "flecs_enet.Packet" });
		ecs_component_desc_t d = { .entity = id };
		d.type.size = sizeof(int);
		d.type.alignment = _Alignof(int);
		ecs_component_init(f.world, &d);
		CHECK(flecs_enet_import(f.world, &f.ids) == FLECS_ENET_SCHEMA);
		CHECK(!f.ids.module);
	} else {
		CHECK(ecs_has_pair(f.world, EcsPreUpdate, EcsDependsOn,
			f.ids.receive_phase));
		CHECK(ecs_has_pair(f.world, f.ids.send_phase, EcsDependsOn,
			EcsPostUpdate));
	}
done:
	if (f.world && ecs_is_deferred(f.world))
		ecs_defer_end(f.world);
	fixture_fini(&f);
	fixture_fini(&other);
}

static void admission(const char *name)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	flecs_enet_host_config_t c = {0};
	fixture_host(&f, 0, &c);
	ecs_entity_t request = 999;
	if (!strcmp(name, "argument_ranges")) {
		flecs_enet_request_t desc = { .operation = FLECS_ENET_SEND };
		CHECK(flecs_enet_submit(f.world, f.host[0], &desc, NULL, 3,
			&request) == FLECS_ENET_ARGUMENT);
		CHECK(request == 0);
		desc.operation = (flecs_enet_operation_t)99;
		CHECK(flecs_enet_submit(f.world, f.host[0], &desc, NULL, 0,
			&request) == FLECS_ENET_ARGUMENT);
	} else if (!strcmp(name, "deferred_payload_copy")) {
		unsigned char bytes[] = {1, 2, 3};
		ecs_defer_begin(f.world);
		flecs_enet_result_t r = flecs_enet_broadcast(f.world, f.host[0],
			0, FLECS_ENET_RELIABLE, bytes, sizeof(bytes),
			17, &request);
		bytes[0] = 99;
		ecs_defer_end(f.world);
		CHECK(r == FLECS_ENET_OK);
		const flecs_enet_packet_t *p = ecs_get_id(f.world, request,
			f.ids.packet);
		CHECK(p && p->size == 3);
		CHECK(((const unsigned char *)p->data)[0] == 1);
	} else {
		flecs_enet_result_t r = test_open(f.world, f.host[0], 19,
			&request);
		fprintf(stderr, "OPEN admission: expected OK(0), actual %d\n",
			r);
		CHECK(r == FLECS_ENET_OK);
		CHECK(request && ecs_is_alive(f.world, request));
		CHECK(ecs_has_pair(f.world, request, EcsChildOf, f.host[0]));
		CHECK(!result(&f, request));
		const flecs_enet_request_t *d = ecs_get_id(f.world, request,
			f.ids.request);
		CHECK(d && d->correlation == 19);
		ecs_progress(f.world, 0.01f);
		const flecs_enet_operation_result_t *completion =
			result(&f, request);
		CHECK(completion && completion->code == FLECS_ENET_OK);
		ecs_entity_t entity = fixture_event(&f, f.host[0], 0);
		CHECK(entity && entity != request);
		const flecs_enet_event_t *event = ecs_get_id(f.world, entity,
			f.ids.event);
		CHECK(event && event->kind == FLECS_ENET_EVENT_RESULT);
		CHECK(event->correlation == 19 && event->request == request);
		CHECK(event->sequence == 1);
		ecs_delete(f.world, request);
		CHECK(ecs_is_alive(f.world, entity));
	}
done:
	if (f.world && ecs_is_deferred(f.world))
		ecs_defer_end(f.world);
	fixture_fini(&f);
}

static void transport(const char *name)
{
	struct fixture f = {0};
	flecs_enet_host_config_t c = {0};
	if (!strcmp(name, "event_byte_pressure")) {
		c.limits.max_event_bytes = 4;
		c.limits.max_packet_bytes = 4;
	}
	if (!strcmp(name, "event_count_pressure"))
		c.limits.max_events = 2;
	if (!strcmp(name, "request_pressure"))
		c.limits.max_requests = 1;
	if (!strcmp(name, "outgoing_pressure_retry")) {
		c.limits.max_outgoing_bytes = 4;
		c.limits.max_packet_bytes = 4;
	}
	if (!strcmp(name, "pending_send_retry"))
		c.limits.max_outgoing_packets = 1;
	if (!start(&f, c))
		goto done;
	ecs_entity_t request = 0;
	if (!strcmp(name, "host_defaults")) {
		const flecs_enet_host_status_t *s = ecs_get_id(f.world,
			f.host[0],
			f.ids.host_status);
		CHECK(s->active.peer_limit == 32);
		CHECK(s->active.channel_limit == 2);
		CHECK(s->active.limits.max_requests == 1024);
		CHECK(s->active.limits.max_events == 1024);
		CHECK(s->active.limits.max_packet_bytes == 1024 * 1024);
		goto done;
	}
	if (!strcmp(name, "connect_transitions") ||
		!strcmp(name, "connect_failure")) {
		flecs_enet_address_t a = {{127, 0, 0, 1}, 19001};
		if (!strcmp(name, "connect_failure"))
			fe_fail_connect = 1;
		CHECK(test_connect(f.world, f.host[0], &a, 2, 7, 18,
			&request) == FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		const flecs_enet_operation_result_t *r = result(&f, request);
		CHECK(r);
		if (!strcmp(name, "connect_failure")) {
			CHECK(r->code == FLECS_ENET_TRANSPORT && !r->peer);
			CHECK(!fe_fail_connect);
			goto done;
		}
		CHECK(r->code == FLECS_ENET_OK && r->peer);
		const flecs_enet_peer_status_t *p = ecs_get_id(f.world, r->peer,
			f.ids.peer_status);
		CHECK(p && p->state == FLECS_ENET_PEER_CONNECTING);
		CHECK(p->correlation == 18);
		CHECK(connected(&f, 1));
		p = ecs_get_id(f.world, r->peer, f.ids.peer_status);
		CHECK(p->state == FLECS_ENET_PEER_CONNECTED);
		goto done;
	}
	if (!connected(&f, !strcmp(name, "broadcast_partial") ? 3 : 1))
		goto done;
	ecs_entity_t peer = fixture_peer(&f, f.host[0], 0);
	clear_events(&f);
	if (!strcmp(name, "schedule_visibility")) {
		struct update_context ctx = { .fixture = &f, .peer = peer };
		ecs_entity_t system = ecs_new(f.world);
		ecs_add_pair(f.world, system, EcsDependsOn, EcsOnUpdate);
		CHECK(ecs_system_init(f.world, &(ecs_system_desc_t){
			.entity = system, .callback = application_update,
			.ctx = &ctx }));
		fe_test_inject(fe_test_latest(), 0, ENET_EVENT_TYPE_RECEIVE,
			"input", 5, 0, ENET_PACKET_FLAG_RELIABLE);
		ecs_progress(f.world, 0.01f);
		CHECK(ctx.input_visible && ctx.sends_before == 0);
		CHECK(ctx.admission == FLECS_ENET_OK && ctx.request);
		CHECK(fe_test.sends == 1);
		ecs_delete(f.world, system);
	} else if (!strcmp(name, "pending_send_retry")) {
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "abc", 3, 1, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(fe_test.sends == 1);
		ecs_entity_t second;
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "def", 3, 2, &second) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(!result(&f, second) && fe_test.sends == 1);
		fe_test_release();
		ecs_progress(f.world, 0.01f);
		const flecs_enet_operation_result_t *r = result(&f, second);
		CHECK(r && r->code == FLECS_ENET_OK && r->accepted == 1);
		CHECK(fe_test.sends == 2);
		ecs_progress(f.world, 0.01f);
		CHECK(fe_test.sends == 2);
	} else if (!strcmp(name, "wrong_host_peer")) {
		fixture_host(&f, 1, &c);
		CHECK(flecs_enet_send(f.world, f.host[1], peer, 0,
			FLECS_ENET_RELIABLE, "abc", 3, 1, &request) ==
			FLECS_ENET_NOT_FOUND);
		CHECK(!request && !fe_test.sends);
	} else if (!strcmp(name, "late_stale_peer")) {
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "abc", 3, 1, &request) ==
			FLECS_ENET_OK);
		ecs_delete(f.world, peer);
		ecs_progress(f.world, 0.01f);
		const flecs_enet_operation_result_t *r = result(&f, request);
		CHECK(r && r->code == FLECS_ENET_NOT_FOUND && !r->accepted);
		CHECK(!fe_test.sends);
	} else if (!strcmp(name, "channel_range")) {
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 2,
			FLECS_ENET_RELIABLE, "abc", 3, 1, &request) ==
			FLECS_ENET_ARGUMENT);
		CHECK(!request && !fe_test.sends);
	} else if (!strcmp(name, "broadcast_partial")) {
		fe_fail_send_at = 2;
		CHECK(flecs_enet_broadcast(f.world, f.host[0], 1,
			FLECS_ENET_RELIABLE, "abc", 3, 21, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		const flecs_enet_operation_result_t *r = result(&f, request);
		CHECK(r && r->code == FLECS_ENET_PARTIAL);
		CHECK(r->attempted == 3 && r->accepted == 2);
		ecs_progress(f.world, 0.01f);
		CHECK(fe_test.sends == 3);
	} else if (!strcmp(name, "event_order_lifetime") ||
		!strcmp(name, "event_byte_pressure") ||
		!strcmp(name, "event_count_pressure") ||
		!strcmp(name, "owner_table_move")) {
		ENetHost *h = fe_test_latest();
		for (unsigned i = 0; i < 3; i++)
			fe_test_inject(h, 0, ENET_EVENT_TYPE_RECEIVE, "abc", 3,
				1, ENET_PACKET_FLAG_RELIABLE);
		ecs_progress(f.world, 0.01f);
		unsigned expected = !strcmp(name, "event_byte_pressure") ? 1 :
			!strcmp(name, "event_count_pressure") ? 2 : 3;
		CHECK(fixture_events(&f, f.host[0]) == expected);
		ecs_entity_t first = fixture_event(&f, f.host[0], 0);
		const flecs_enet_event_t *e = ecs_get_id(f.world, first,
			f.ids.event);
		CHECK(e && e->kind == FLECS_ENET_EVENT_RECEIVE);
		CHECK(e->peer == peer && e->channel == 1);
		uint64_t sequence = e->sequence;
		const flecs_enet_packet_t *p = ecs_get_id(f.world, first,
			f.ids.packet);
		CHECK(p && p->size == 3 && !memcmp(p->data, "abc", 3));
		const void *borrow = p->data;
		ecs_entity_t tag = ecs_new(f.world);
		ecs_add_id(f.world, first, tag);
		p = ecs_get_id(f.world, first, f.ids.packet);
		CHECK(p && p->data == borrow);
		ecs_progress(f.world, 0.01f);
		CHECK(!memcmp(borrow, "abc", 3));
		if (expected < 3)
			CHECK(fe_test.protocol_services > 0);
		CHECK(flecs_enet_consume(f.world, f.host[0], first) ==
			FLECS_ENET_OK);
		CHECK(!ecs_is_alive(f.world, first));
		ecs_progress(f.world, 0.01f);
		CHECK(fixture_events(&f, f.host[0]) >= 1);
		e = ecs_get_id(f.world, fixture_event(&f, f.host[0], 0),
			f.ids.event);
		CHECK(e && e->sequence > sequence);
	} else if (!strcmp(name, "request_pressure")) {
		CHECK(flecs_enet_flush(f.world, f.host[0], 1, &request) ==
			FLECS_ENET_OK);
		ecs_entity_t second = 999;
		CHECK(flecs_enet_flush(f.world, f.host[0], 2, &second) ==
			FLECS_ENET_BACKPRESSURE);
		CHECK(!second);
		ecs_delete(f.world, request);
		CHECK(flecs_enet_flush(f.world, f.host[0], 2, &second) ==
			FLECS_ENET_OK);
	} else if (!strcmp(name, "outgoing_pressure_retry")) {
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "abc", 3, 1, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(fe_test.sends == 1);
		ecs_entity_t second;
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "def", 3, 2, &second) ==
			FLECS_ENET_BACKPRESSURE);
		fe_test_release();
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "def", 3, 2, &second) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(fe_test.sends == 2);
		CHECK(!memcmp(fe_test.last_data, "def", 3));
	} else if (!strcmp(name, "tuning") ||
		!strcmp(name, "compression_failure")) {
		flecs_enet_request_t d = { .operation = FLECS_ENET_BANDWIDTH,
			.incoming_bandwidth = 1000,
			.outgoing_bandwidth = 2000 };
		CHECK(fixture_submit(&f, 0, &d, NULL, 0, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(result(&f, request) &&
			result(&f, request)->code == FLECS_ENET_OK);
		CHECK(fe_test_latest()->incomingBandwidth == 1000);
		d.operation = FLECS_ENET_COMPRESSION;
		d.incoming_bandwidth = d.outgoing_bandwidth = 0;
		d.compression = FLECS_ENET_COMPRESSION_RANGE_CODER;
		if (!strcmp(name, "compression_failure"))
			fe_fail_compression = 1;
		CHECK(fixture_submit(&f, 0, &d, NULL, 0, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(result(&f, request) && result(&f, request)->code ==
			(!strcmp(name, "compression_failure") ?
			FLECS_ENET_NOMEM : FLECS_ENET_OK));
		if (!strcmp(name, "compression_failure")) {
			const flecs_enet_host_status_t *s = ecs_get_id(f.world,
				f.host[0], f.ids.host_status);
			CHECK(s->active.compression ==
				FLECS_ENET_COMPRESSION_NONE);
			goto done;
		}
		for (int op = FLECS_ENET_PING;
			op <= FLECS_ENET_THROTTLE; op++) {
			d = (flecs_enet_request_t){
				.operation = op, .peer = peer };
			if (op == FLECS_ENET_PING_INTERVAL)
				d.interval_ms = 100;
			if (op == FLECS_ENET_TIMEOUT) {
				d.timeout_limit = 4;
				d.timeout_minimum_ms = 1000;
				d.timeout_maximum_ms = 5000;
			}
			if (op == FLECS_ENET_THROTTLE) {
				d.interval_ms = 500;
				d.acceleration = 3;
				d.deceleration = 4;
			}
			CHECK(fixture_submit(&f, 0, &d, NULL, 0, &request) ==
				FLECS_ENET_OK);
			ecs_progress(f.world, 0.01f);
			CHECK(result(&f, request) &&
				result(&f, request)->code == FLECS_ENET_OK);
		}
		ENetPeer *p = fe_test_peer(fe_test_latest(), 0);
		CHECK(p->pingInterval == 100 && p->timeoutLimit == 4);
		CHECK(p->packetThrottleInterval == 500);
		CHECK(p->packetThrottleAcceleration == 3);
		CHECK(p->packetThrottleDeceleration == 4);
	} else if (!strcmp(name, "stale_peer")) {
		ecs_delete(f.world, peer);
		ecs_new(f.world);
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "abc", 3, 0, &request) ==
			FLECS_ENET_NOT_FOUND);
		CHECK(!request && !fe_test.sends);
	} else if (!strcmp(name, "service_failure")) {
		fe_fail_service = 1;
		ecs_progress(f.world, 0.01f);
		const flecs_enet_host_status_t *s = ecs_get_id(f.world,
			f.host[0],
			f.ids.host_status);
		CHECK(s->state == FLECS_ENET_HOST_FAULTED);
		CHECK(s->fault == FLECS_ENET_TRANSPORT);
		CHECK(ecs_is_alive(f.world, peer));
		CHECK(fe_test.hosts == 0);
	} else if (!strcmp(name, "disconnect_graceful") ||
		!strcmp(name, "disconnect_immediate") ||
		!strcmp(name, "disconnect_draining") ||
		!strcmp(name, "peer_reset")) {
		flecs_enet_operation_t op = FLECS_ENET_DISCONNECT;
		if (!strcmp(name, "disconnect_immediate"))
			op = FLECS_ENET_DISCONNECT_NOW;
		if (!strcmp(name, "disconnect_draining"))
			op = FLECS_ENET_DISCONNECT_LATER;
		if (!strcmp(name, "peer_reset"))
			op = FLECS_ENET_RESET;
		CHECK(flecs_enet_disconnect(f.world, f.host[0], peer, op, 0, 33,
			&request) == FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(result(&f, request) &&
			result(&f, request)->code == FLECS_ENET_OK);
		const flecs_enet_peer_status_t *p = ecs_get_id(f.world, peer,
			f.ids.peer_status);
		bool immediate = op == FLECS_ENET_DISCONNECT_NOW ||
			op == FLECS_ENET_RESET;
		CHECK(p && p->state == (immediate ?
			FLECS_ENET_PEER_DISCONNECTED :
			FLECS_ENET_PEER_DISCONNECTING));
		if (!immediate) {
			fe_test_inject(fe_test_latest(), 0,
				ENET_EVENT_TYPE_DISCONNECT, NULL, 0, 0, 0);
			ecs_progress(f.world, 0.01f);
		}
		p = ecs_get_id(f.world, peer, f.ids.peer_status);
		CHECK(p && p->state == FLECS_ENET_PEER_DISCONNECTED);
		CHECK(ecs_is_alive(f.world, peer));
	} else if (!strcmp(name, "host_cleanup") ||
		!strcmp(name, "world_cleanup")) {
		fe_test_inject(fe_test_latest(), 0, ENET_EVENT_TYPE_RECEIVE,
			"abc", 3, 0, ENET_PACKET_FLAG_RELIABLE);
		ecs_progress(f.world, 0.01f);
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "abc", 3, 0, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		ecs_entity_t event = fixture_event(&f, f.host[0], 0);
		CHECK(event && fe_test.packets > 0);
		/* Leave pending module bytes and a queued native receive. */
		ecs_entity_t pending;
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
			FLECS_ENET_RELIABLE, "def", 3, 0, &pending) ==
			FLECS_ENET_OK);
		fe_test_inject(fe_test_latest(), 0, ENET_EVENT_TYPE_RECEIVE,
			"ghi", 3, 0, ENET_PACKET_FLAG_RELIABLE);
		if (!strcmp(name, "world_cleanup"))
			fixture_fini(&f);
		else {
			ecs_delete(f.world, f.host[0]);
			CHECK(!ecs_is_alive(f.world, peer));
			CHECK(!ecs_is_alive(f.world, request));
			CHECK(!ecs_is_alive(f.world, pending));
			CHECK(!ecs_is_alive(f.world, event));
		}
		CHECK(fe_test.hosts == 0 && fe_test.packets == 0);
	} else if (!strcmp(name, "close_reopen")) {
		CHECK(flecs_enet_close(f.world, f.host[0], 0, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(result(&f, request) &&
			result(&f, request)->code == FLECS_ENET_OK);
		CHECK(ecs_is_alive(f.world, peer) && !fe_test.hosts);
		CHECK(test_open(f.world, f.host[0], 0, &request) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(result(&f, request) &&
			result(&f, request)->code == FLECS_ENET_OK);
		CHECK(fe_test.hosts == 1);
	} else {
		flecs_enet_delivery_t mode = FLECS_ENET_RELIABLE;
		enet_uint32 flags = ENET_PACKET_FLAG_RELIABLE;
		if (!strcmp(name, "unreliable_channel")) {
			mode = FLECS_ENET_UNRELIABLE;
			flags = 0;
		}
		if (!strcmp(name, "unsequenced_channel")) {
			mode = FLECS_ENET_UNSEQUENCED;
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}
		if (!strcmp(name, "unreliable_fragment")) {
			mode = FLECS_ENET_UNRELIABLE_FRAGMENT;
			flags = ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
		}
		if (!strcmp(name, "send_failure"))
			fe_fail_send_at = 1;
		if (!strcmp(name, "packet_failure"))
			fe_fail_packet = 1;
		bool empty = !strcmp(name, "empty_packet");
		CHECK(flecs_enet_send(f.world, f.host[0], peer, 1, mode,
			empty ? NULL : "abc", empty ? 0 : 3, 5, &request) ==
			FLECS_ENET_OK);
		if (!strcmp(name, "cancel_request")) {
			ecs_delete(f.world, request);
			ecs_progress(f.world, 0.01f);
			CHECK(!fe_test.sends && !fe_test.packets);
			goto done;
		}
		ecs_progress(f.world, 0.01f);
		const flecs_enet_operation_result_t *r = result(&f, request);
		CHECK(r);
		if (!strcmp(name, "send_failure")) {
			CHECK(r->code == FLECS_ENET_TRANSPORT && !r->accepted);
			CHECK(!fe_test.packets);
		} else if (!strcmp(name, "packet_failure")) {
			CHECK(r->code == FLECS_ENET_NOMEM && !r->accepted);
			CHECK(!fe_test.sends && !fe_test.packets);
		} else {
			CHECK(r->code == FLECS_ENET_OK && r->accepted == 1);
			CHECK(fe_test.last_channel == 1);
			CHECK(fe_test.last_flags == flags);
			CHECK(fe_test.last_size == (empty ? 0 : 3));
			ecs_progress(f.world, 0.01f);
			CHECK(fe_test.sends == 1);
		}
	}
done:
	fixture_fini(&f);
}

static void open_failure(void)
{
	struct fixture f = {0};
	CHECK(fixture_init(&f));
	flecs_enet_host_config_t c = {0};
	fixture_host(&f, 0, &c);
	fe_fail_host = 1;
	ecs_entity_t request;
	CHECK(test_open(f.world, f.host[0], 0, &request) ==
		FLECS_ENET_OK);
	ecs_progress(f.world, 0.01f);
	const flecs_enet_operation_result_t *r = result(&f, request);
	CHECK(r && r->code == FLECS_ENET_TRANSPORT);
	CHECK(fe_test.hosts == 0);
done:
	fixture_fini(&f);
}

static void lifecycle_pressure(const char *name)
{
	struct fixture f = {0};
	flecs_enet_host_config_t c = {0};
	c.limits.max_events = 2;
	if (!start(&f, c) || !connected(&f, 1))
		goto done;
	clear_events(&f);
	ecs_entity_t peer = fixture_peer(&f, f.host[0], 0);
	ecs_entity_t send, close, ping;
	CHECK(flecs_enet_send(f.world, f.host[0], peer, 0,
		FLECS_ENET_RELIABLE, "abc", 3, 1, &send) == FLECS_ENET_OK);
	CHECK(flecs_enet_close(f.world, f.host[0], 2, &close) == FLECS_ENET_OK);
	flecs_enet_request_t d = { .operation = FLECS_ENET_PING,
		.peer = peer };
	CHECK(flecs_enet_submit(f.world, f.host[0], &d, NULL, 0, &ping) ==
		FLECS_ENET_OK);
	ecs_progress(f.world, 0.01f);
	CHECK(result(&f, send) && result(&f, send)->code == FLECS_ENET_STATE);
	CHECK(result(&f, ping) && result(&f, ping)->code == FLECS_ENET_STATE);
	CHECK(!result(&f, close) && !fe_test.sends);
	CHECK(fe_test.hosts == 1 && fixture_events(&f, f.host[0]) == 2);
	if (!strcmp(name, "delete_paused_close"))
		ecs_delete(f.world, close);
	clear_events(&f);
	ecs_progress(f.world, 0.01f);
	if (!strcmp(name, "delete_paused_close")) {
		ecs_entity_t flush;
		CHECK(flecs_enet_flush(f.world, f.host[0], 3, &flush) ==
			FLECS_ENET_OK);
		ecs_progress(f.world, 0.01f);
		CHECK(result(&f, flush) && !result(&f, flush)->code);
		CHECK(fe_test.flushes == 1 && fe_test.hosts == 1);
	} else {
		CHECK(result(&f, close) && !result(&f, close)->code);
		CHECK(fe_test.hosts == 0);
		CHECK(ecs_is_alive(f.world, peer));
	}
done:
	fixture_fini(&f);
}

static void deferred_consumption(void)
{
	struct fixture f = {0};
	if (!start(&f, (flecs_enet_host_config_t){0}) || !connected(&f, 1))
		goto done;
	clear_events(&f);
	fe_test_inject(fe_test_latest(), 0, ENET_EVENT_TYPE_RECEIVE,
		"abc", 3, 0, ENET_PACKET_FLAG_RELIABLE);
	ecs_progress(f.world, 0.01f);
	ecs_entity_t event = fixture_event(&f, f.host[0], 0);
	const flecs_enet_packet_t *view = ecs_get_id(f.world, event,
		f.ids.packet);
	CHECK(view && !memcmp(view->data, "abc", 3));
	const void *bytes = view->data;
	ecs_entity_t tag = ecs_new(f.world);
	ecs_add_id(f.world, f.host[0], tag);
	ecs_defer_begin(f.world);
	CHECK(flecs_enet_consume(f.world, f.host[0], event) == FLECS_ENET_OK);
	CHECK(ecs_is_alive(f.world, event) && !memcmp(bytes, "abc", 3));
	const flecs_enet_host_status_t *s = ecs_get_id(f.world, f.host[0],
		f.ids.host_status);
	CHECK(s->event_bytes == 3 && s->retained_events == 1);
	ecs_defer_end(f.world);
	s = ecs_get_id(f.world, f.host[0], f.ids.host_status);
	CHECK(!s->event_bytes && !s->retained_events && !fe_test.packets);
done:
	if (f.world && ecs_is_deferred(f.world))
		ecs_defer_end(f.world);
	fixture_fini(&f);
}

static void reopen_event_limits(void)
{
	struct fixture f = {0};
	if (!start(&f, (flecs_enet_host_config_t){0}) || !connected(&f, 1))
		goto done;
	clear_events(&f);
	fe_test_inject(fe_test_latest(), 0, ENET_EVENT_TYPE_RECEIVE,
		"0123456789", 10, 0, ENET_PACKET_FLAG_RELIABLE);
	ecs_progress(f.world, 0.01f);
	ecs_entity_t old = fixture_event(&f, f.host[0], 0), request;
	CHECK(flecs_enet_close(f.world, f.host[0], 0, &request) ==
		FLECS_ENET_OK);
	ecs_progress(f.world, 0.01f);
	flecs_enet_host_config_t c = {0};
	c.limits.max_event_bytes = c.limits.max_packet_bytes = 4;
	ecs_set_id(f.world, f.host[0], f.ids.host_config, sizeof(c), &c);
	CHECK(test_open(f.world, f.host[0], 0, &request) == FLECS_ENET_OK);
	ecs_progress(f.world, 0.01f);
	CHECK(result(&f, request) && !result(&f, request)->code);
	CHECK(connected(&f, 1));
	unsigned count = fixture_events(&f, f.host[0]);
	fe_test_inject(fe_test_latest(), 0, ENET_EVENT_TYPE_RECEIVE,
		"abc", 3, 0, ENET_PACKET_FLAG_RELIABLE);
	ecs_progress(f.world, 0.01f);
	CHECK(fixture_events(&f, f.host[0]) == count);
	CHECK(flecs_enet_consume(f.world, f.host[0], old) == FLECS_ENET_OK);
	ecs_progress(f.world, 0.01f);
	CHECK(fixture_events(&f, f.host[0]) == count);
	const flecs_enet_host_status_t *s = ecs_get_id(f.world, f.host[0],
		f.ids.host_status);
	CHECK(s->event_bytes == 3);
done:
	fixture_fini(&f);
}

static void schema_cycle(void)
{
	struct fixture f = { .world = ecs_init() };
	ecs_entity_t module = ecs_entity_init(f.world,
		&(ecs_entity_desc_t){ .name = "flecs_enet" });
	ecs_add_id(f.world, module, EcsModule);
	ecs_entity_t rx = ecs_entity_init(f.world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.Receive" });
	ecs_entity_t tx = ecs_entity_init(f.world,
		&(ecs_entity_desc_t){ .name = "flecs_enet.Send" });
	ecs_add_id(f.world, rx, EcsPhase);
	ecs_add_id(f.world, tx, EcsPhase);
	/* Standard phases use internal ordering nodes, not each other's IDs. */
	ecs_add_pair(f.world, EcsPostUpdate, EcsDependsOn, EcsPreUpdate);
	ecs_add_pair(f.world, EcsOnLoad, EcsDependsOn, tx);
	CHECK(flecs_enet_import(f.world, &f.ids) == FLECS_ENET_SCHEMA);
	CHECK(!f.ids.module);
	CHECK(!ecs_lookup(f.world, "flecs_enet.HostConfig"));
	CHECK(!ecs_has_pair(f.world, rx, EcsDependsOn, EcsOnLoad));
done:
	fixture_fini(&f);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	const char *name = argv[1];
	bool known = false;
#define CASE(id) if (!strcmp(name, #id)) known = true;
#include "cases.def"
#undef CASE
	if (!known)
		return 2;
	fe_test_reset();
	if (!strncmp(name, "import_", 7) || !strcmp(name, "world_isolation") ||
		!strcmp(name, "schema_conflict") ||
		!strcmp(name, "phase_dependencies"))
		metadata(name);
	else if (!strcmp(name, "request_admission") ||
		!strcmp(name, "deferred_payload_copy") ||
		!strcmp(name, "argument_ranges"))
		admission(name);
	else if (!strcmp(name, "host_open_failure"))
		open_failure();
	else if (!strcmp(name, "close_cancellation_pressure") ||
		!strcmp(name, "delete_paused_close"))
		lifecycle_pressure(name);
	else if (!strcmp(name, "schema_cycle"))
		schema_cycle();
	else if (!strcmp(name, "reopen_event_limits"))
		reopen_event_limits();
	else if (!strcmp(name, "deferred_consumption"))
		deferred_consumption();
	else
		transport(name);
	if (fe_test.hosts || fe_test.packets) {
		fprintf(stderr, "CONTRACT leaked hosts=%u packets=%u\n",
			fe_test.hosts, fe_test.packets);
		failures++;
	}
	return failures ? 1 : 0;
}
