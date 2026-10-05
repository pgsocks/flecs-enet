#include "fixture.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One process per scenario. Each command performs just one operation;
 * queries serialize actual ECS data and never run a named contract case.
 */
static struct fixture worlds[2];
static unsigned current;
static ecs_entity_t deleted_peers[2][FE_SLOTS][FE_SLOTS];

static int decode(const char *hex, unsigned char *out, size_t *size)
{
	*size = 0;
	if (!strcmp(hex, "-"))
		return 1;
	size_t n = strlen(hex);
	if ((n & 1) || n > 65536)
		return 0;
	for (size_t i = 0; i < n; i += 2) {
		unsigned value;
		if (sscanf(hex + i, "%2x", &value) != 1)
			return 0;
		out[(*size)++] = (unsigned char)value;
	}
	return 1;
}

static void packet(const flecs_enet_packet_t *p)
{
	printf(" size=%zu bytes=", p ? p->size : 0);
	if (!p || !p->size) {
		printf("-");
		return;
	}
	const unsigned char *data = p->data;
	for (size_t i = 0; i < p->size; i++)
		printf("%02x", data[i]);
}

static void inspect(struct fixture *f, const char *kind, unsigned host,
	unsigned slot)
{
	if (host >= FE_SLOTS || slot >= FE_SLOTS) {
		puts("error reason=range");
		return;
	}
	if (!strcmp(kind, "host")) {
		const flecs_enet_host_status_t *s = ecs_get_id(f->world,
			f->host[host], f->ids.host_status);
		if (!s) {
			puts("ok exists=0");
			return;
		}
		printf("ok exists=1 state=%d fault=%d events=%u pending=%u",
			s->state, s->fault, s->retained_events,
			s->pending_requests);
		printf(" outgoing=%zu event_bytes=%zu port=%u"
			" sequence=%" PRIu64,
			s->outgoing_bytes, s->event_bytes, s->address.port,
			s->last_sequence);
		printf(" peers=%u channels=%u\n", s->active.peer_limit,
			s->active.channel_limit);
		return;
	}
	ecs_entity_t entity = 0;
	if (!strcmp(kind, "request"))
		entity = f->request[slot];
	else if (!strcmp(kind, "peer"))
		entity = fixture_peer(f, f->host[host], slot);
	else if (!strcmp(kind, "event"))
		entity = fixture_event(f, f->host[host], slot);
	else {
		puts("error reason=kind");
		return;
	}
	if (!entity || !ecs_is_alive(f->world, entity)) {
		puts("ok exists=0");
		return;
	}
	printf("ok exists=1 entity=%" PRIu64, (uint64_t)entity);
	const flecs_enet_operation_result_t *r = ecs_get_id(f->world, entity,
		f->ids.result);
	printf(" complete=%u", r != NULL);
	if (r)
		printf(" code=%d attempted=%u accepted=%u peer=%" PRIu64,
			r->code, r->attempted, r->accepted, (uint64_t)r->peer);
	const flecs_enet_peer_status_t *p = ecs_get_id(f->world, entity,
		f->ids.peer_status);
	if (p)
		printf(" state=%d channels=%u correlation=%" PRIu64,
			p->state, p->channel_count, p->correlation);
	const flecs_enet_event_t *e = ecs_get_id(f->world, entity,
		f->ids.event);
	if (e)
		printf(" kind=%d sequence=%" PRIu64 " correlation=%" PRIu64
			" channel=%u", e->kind, e->sequence, e->correlation,
			e->channel);
	packet(ecs_get_id(f->world, entity, f->ids.packet));
	puts("");
}

static void command(char *line)
{
	char op[32] = {0};
	if (sscanf(line, "%31s", op) != 1) {
		puts("error reason=empty");
		return;
	}
	if (!strcmp(op, "world")) {
		unsigned n;
		if (sscanf(line, "%*s %u", &n) != 1 || n > 1) {
			puts("error reason=world");
			return;
		}
		current = n;
		if (!worlds[n].world && !fixture_init(&worlds[n])) {
			puts("error reason=import");
			return;
		}
		puts("ok imported=1");
		return;
	}
	struct fixture *f = &worlds[current];
	if (!f->world) {
		puts("error reason=uninitialized");
		return;
	}
	if (!strcmp(op, "script")) {
		char hex[65537], code[32769];
		size_t size;
		if (sscanf(line, "%*s %65536s", hex) != 1 ||
			!decode(hex, (unsigned char *)code, &size)) {
			puts("error reason=script");
			return;
		}
		code[size] = 0;
		int r = ecs_script_run(f->world, "fixture", code, NULL);
		printf("ok loaded=%u\n", r == 0);
	} else if (!strcmp(op, "bind")) {
		unsigned slot;
		char name[256];
		if (sscanf(line, "%*s %u %255s", &slot, name) != 2 ||
			slot >= FE_SLOTS) {
			puts("error reason=bind");
			return;
		}
		f->host[slot] = ecs_lookup(f->world, name);
		printf("ok bound=%u\n", f->host[slot] != 0);
	} else if (!strcmp(op, "startup") || !strcmp(op, "status") ||
		!strcmp(op, "delete_name")) {
		char name[256];
		if (sscanf(line, "%*s %255s", name) != 1) {
			puts("error reason=name");
			return;
		}
		ecs_entity_t entity = ecs_lookup(f->world, name);
		if (!entity) {
			puts("ok exists=0");
			return;
		}
		if (!strcmp(op, "startup")) {
			ecs_add_id(f->world, entity, f->ids.start);
			puts("ok started=1");
		} else if (!strcmp(op, "delete_name")) {
			ecs_delete(f->world, entity);
			puts("ok deleted=1");
		} else {
			const flecs_enet_start_status_t *s =
				ecs_get_id(f->world, entity,
					f->ids.start_status);
			if (!s) {
				puts("ok exists=0");
				return;
			}
			printf("ok exists=1 state=%d code=%d admitted=%u "
				"peer=%u start=%u\n", s->state, s->code,
				s->request != 0, s->peer != 0,
				ecs_has_id(f->world, entity, f->ids.start));
		}
	} else if (!strcmp(op, "host")) {
		unsigned slot, listen, peers, channels, requests, events;
		size_t outgoing, event_bytes, packet_bytes;
		if (sscanf(line, "%*s %u %u %u %u %u %u %zu %zu %zu",
			&slot, &listen, &peers, &channels, &requests, &events,
			&outgoing, &event_bytes, &packet_bytes) != 9 ||
			slot >= FE_SLOTS || f->host[slot]) {
			puts("error reason=host");
			return;
		}
		flecs_enet_host_config_t c = { .listen = listen != 0,
			.address = {{127, 0, 0, 1}, 0}, .peer_limit = peers,
			.channel_limit = channels };
		c.limits = (flecs_enet_limits_t){ .max_requests = requests,
			.max_events = events, .max_outgoing_bytes = outgoing,
			.max_event_bytes = event_bytes,
			.max_packet_bytes = packet_bytes };
		fixture_host(f, slot, &c);
		puts("ok configured=1");
	} else if (!strcmp(op, "submit")) {
		unsigned host, operation, peer, channel, mode, a, b, c;
		uint64_t correlation;
		char hex[65537];
		if (sscanf(line, "%*s %u %u %u %u %u %" SCNu64
			" %u %u %u %65536s", &host, &operation, &peer, &channel,
			&mode, &correlation, &a, &b, &c, hex) != 10 ||
			host >= FE_SLOTS || peer >= FE_SLOTS || channel > 255) {
			puts("error reason=submit");
			return;
		}
		unsigned char bytes[32768];
		size_t size;
		if (!decode(hex, bytes, &size)) {
			puts("error reason=hex");
			return;
		}
		flecs_enet_request_t d = { .operation = operation,
			.correlation = correlation };
		if (operation == FLECS_ENET_SEND ||
			operation == FLECS_ENET_BROADCAST) {
			d.channel = (uint8_t)channel;
			d.delivery = mode;
		}
		if (operation == FLECS_ENET_CONNECT) {
			d.address = (flecs_enet_address_t){
				{127, 0, 0, 1}, 19001};
			d.channels = a;
			d.data = b;
		}
		if ((operation >= FLECS_ENET_DISCONNECT &&
			operation <= FLECS_ENET_RESET) ||
			operation >= FLECS_ENET_PING ||
			operation == FLECS_ENET_SEND) {
			d.peer = fixture_peer(f, f->host[host], peer);
			if (!d.peer)
				d.peer = deleted_peers[current][host][peer];
		}
		if (operation >= FLECS_ENET_DISCONNECT &&
			operation <= FLECS_ENET_DISCONNECT_LATER)
			d.data = a;
		if (operation == FLECS_ENET_BANDWIDTH) {
			d.incoming_bandwidth = a;
			d.outgoing_bandwidth = b;
		}
		if (operation == FLECS_ENET_COMPRESSION)
			d.compression = a;
		if (operation == FLECS_ENET_PING_INTERVAL ||
			operation == FLECS_ENET_THROTTLE)
			d.interval_ms = a;
		if (operation == FLECS_ENET_THROTTLE) {
			d.acceleration = b;
			d.deceleration = c;
		}
		if (operation == FLECS_ENET_TIMEOUT) {
			d.timeout_limit = a;
			d.timeout_minimum_ms = b;
			d.timeout_maximum_ms = c;
		}
		ecs_entity_t request;
		flecs_enet_result_t r = fixture_submit(f, host, &d,
			size ? bytes : NULL, size, &request);
		memset(bytes, 0xff, sizeof(bytes));
		printf("ok code=%d entity=%" PRIu64 "\n", r, (uint64_t)request);
	} else if (!strcmp(op, "connect")) {
		unsigned source, target;
		if (sscanf(line, "%*s %u %u", &source, &target) != 2 ||
			source >= FE_SLOTS || target >= FE_SLOTS) {
			puts("error reason=connect");
			return;
		}
		const flecs_enet_host_status_t *s = ecs_get_id(f->world,
			f->host[target], f->ids.host_status);
		if (!s) {
			puts("error reason=target_closed");
			return;
		}
		flecs_enet_request_t d = { .operation = FLECS_ENET_CONNECT,
			.address = s->address, .channels = 2,
			.correlation = 99 };
		ecs_entity_t request;
		flecs_enet_result_t r = fixture_submit(f, source, &d, NULL, 0,
			&request);
		printf("ok code=%d entity=%" PRIu64 "\n", r, (uint64_t)request);
	} else if (!strcmp(op, "progress")) {
		ecs_progress(f->world, 0.01f);
		puts("ok progressed=1");
	} else if (!strcmp(op, "defer")) {
		char action[16];
		if (sscanf(line, "%*s %15s", action) != 1) {
			puts("error reason=defer");
			return;
		}
		if (!strcmp(action, "begin"))
			ecs_defer_begin(f->world);
		else if (!strcmp(action, "end"))
			ecs_defer_end(f->world);
		else {
			puts("error reason=defer");
			return;
		}
		puts("ok deferred=1");
	} else if (!strcmp(op, "query")) {
		char kind[16];
		unsigned host = 0, slot = 0;
		if (sscanf(line, "%*s %15s %u %u", kind, &host, &slot) != 3) {
			puts("error reason=query");
			return;
		}
		inspect(f, kind, host, slot);
	} else if (!strcmp(op, "consume") || !strcmp(op, "delete")) {
		char kind[16] = "event";
		unsigned host, slot;
		if (sscanf(line, "%*s %15s %u %u", kind, &host, &slot) != 3 ||
			host >= FE_SLOTS || slot >= FE_SLOTS) {
			puts("error reason=delete");
			return;
		}
		ecs_entity_t entity = 0;
		if (!strcmp(kind, "host"))
			entity = f->host[host];
		else if (!strcmp(kind, "event"))
			entity = fixture_event(f, f->host[host], slot);
		else if (!strcmp(kind, "peer")) {
			entity = fixture_peer(f, f->host[host], slot);
			deleted_peers[current][host][slot] = entity;
		}
		else if (!strcmp(kind, "request"))
			entity = f->request[slot];
		if (!entity || !ecs_is_alive(f->world, entity)) {
			puts("error reason=missing");
			return;
		}
		if (!strcmp(op, "consume"))
			printf("ok code=%d\n", flecs_enet_consume(f->world,
				f->host[host], entity));
		else {
			ecs_delete(f->world, entity);
			puts("ok deleted=1");
		}
	} else if (!strcmp(op, "fini")) {
		fixture_fini(f);
		puts("ok finalized=1");
#ifndef FLECS_ENET_REAL_UDP
	} else if (!strcmp(op, "inject")) {
		unsigned peer, type, channel, flags;
		char hex[65537];
		if (sscanf(line, "%*s %u %u %u %u %65536s", &peer, &type,
			&channel, &flags, hex) != 5 || !fe_test_latest() ||
			peer >= fe_test_latest()->peerCount) {
			puts("error reason=native_host_missing");
			return;
		}
		unsigned char bytes[32768];
		size_t size;
		if (!decode(hex, bytes, &size)) {
			puts("error reason=hex");
			return;
		}
		fe_test_inject(fe_test_latest(), peer, type, bytes, size,
			(enet_uint8)channel, flags);
		puts("ok injected=1");
	} else if (!strcmp(op, "release")) {
		fe_test_release();
		puts("ok released=1");
	} else if (!strcmp(op, "counters")) {
		printf("ok hosts=%u packets=%u sends=%u protocol=%u tuning=%u",
			fe_test.hosts, fe_test.packets, fe_test.sends,
			fe_test.protocol_services, fe_test.tuning);
		printf(" flags=%u channel=%u size=%zu\n", fe_test.last_flags,
			fe_test.last_channel, fe_test.last_size);
	} else if (!strcmp(op, "fail")) {
		char kind[16];
		unsigned value;
		if (sscanf(line, "%*s %15s %u", kind, &value) != 2) {
			puts("error reason=fail");
			return;
		}
		if (!strcmp(kind, "host")) fe_fail_host = value;
		else if (!strcmp(kind, "connect")) fe_fail_connect = value;
		else if (!strcmp(kind, "packet")) fe_fail_packet = value;
		else if (!strcmp(kind, "service")) fe_fail_service = value;
		else if (!strcmp(kind, "send")) fe_fail_send_at = value;
		else if (!strcmp(kind, "compression"))
			fe_fail_compression = value;
		else {
			puts("error reason=fail_kind");
			return;
		}
		puts("ok armed=1");
#endif
	} else {
		puts("error reason=command");
	}
}

int main(void)
{
	char line[66000];
	while (fgets(line, sizeof(line), stdin)) {
		if (!strcmp(line, "quit\n")) {
			puts("ok quit=1");
			fflush(stdout);
			break;
		}
		command(line);
		fflush(stdout);
	}
	for (unsigned i = 0; i < 2; i++) {
		if (worlds[i].world && ecs_is_deferred(worlds[i].world))
			ecs_defer_end(worlds[i].world);
		fixture_fini(&worlds[i]);
	}
	return 0;
}
