#include "internal.h"
#include <stdlib.h>

static bool outgoing(flecs_enet_operation_t op)
{
	return op == FLECS_ENET_SEND || op == FLECS_ENET_BROADCAST ||
		op == FLECS_ENET_FLUSH;
}

static struct fe_payload *first(struct fe_host *h, bool send)
{
	for (struct fe_payload *p = h->requests; p; p = p->next) {
		if (outgoing(p->desc.operation) == send)
			return p;
	}
	return NULL;
}

static bool prepare(struct fe_payload *p)
{
	if (fe_event_room(p->host, 0))
		return false;
	if (!p->completion)
		p->completion = calloc(1, sizeof(*p->completion));
	return p->completion != NULL;
}

static void fault(struct fe_host *h, flecs_enet_result_t code)
{
	for (struct fe_peer *p = h->peers; p; p = p->next) {
		if (!p->native)
			continue;
		p->diagnostic = true;
		p->diagnostic_result = code;
	}
	h->status.state = FLECS_ENET_HOST_FAULTED;
	h->status.fault = code;
	fe_teardown(h->world, h->entity, h);
	fe_sync(h);
}

static flecs_enet_result_t open_host(struct fe_host *h)
{
	if (h->status.state != FLECS_ENET_HOST_CLOSED)
		return FLECS_ENET_STATE;
	const flecs_enet_host_config_t *input = ecs_get_id(h->world,
		h->entity, h->ctx->ids.host_config);
	flecs_enet_host_config_t c;
	if (!input)
		return FLECS_ENET_NOT_FOUND;
	flecs_enet_result_t r = fe_normalize(input, &c);
	if (r)
		return r;
	flecs_enet_peer_config_t tuning = {0};
	const flecs_enet_peer_config_t *defaults = ecs_get_id(h->world,
		h->entity, h->ctx->ids.peer_config);
	if (defaults)
		tuning = *defaults;
	r = fe_validate_peer(&tuning);
	if (r)
		return r;
	ENetAddress address = fe_address(&c.address);
	ENetHost *native = fe_host_create(c.listen ? &address : NULL,
		c.peer_limit, c.channel_limit, c.incoming_bandwidth,
		c.outgoing_bandwidth);
	if (!native)
		return FLECS_ENET_TRANSPORT;
	native->maximumPacketSize = c.limits.max_packet_bytes;
	native->maximumWaitingData = c.limits.max_waiting_bytes;
	if (c.compression == FLECS_ENET_COMPRESSION_RANGE_CODER &&
		fe_host_compress_with_range_coder(native) < 0) {
		fe_host_destroy(native);
		return FLECS_ENET_NOMEM;
	}
	h->native = native;
	h->peer_defaults = tuning;
	h->status.active = c;
	h->status.address = fe_public_address(&native->address);
	h->status.state = FLECS_ENET_HOST_OPEN;
	h->status.fault = FLECS_ENET_OK;
	return FLECS_ENET_OK;
}

static flecs_enet_result_t check_peer(struct fe_host *h,
	const flecs_enet_request_t *d, struct fe_peer **out)
{
	struct fe_peer *p = fe_find_peer(h, d->peer);
	*out = p;
	if (!p || !ecs_is_alive(h->world, d->peer))
		return FLECS_ENET_NOT_FOUND;
	if (!p->native || h->status.state != FLECS_ENET_HOST_OPEN)
		return FLECS_ENET_STATE;
	bool immediate = d->operation == FLECS_ENET_RESET ||
		d->operation == FLECS_ENET_DISCONNECT_NOW;
	if (!immediate && p->status.state != FLECS_ENET_PEER_CONNECTED)
		return FLECS_ENET_STATE;
	return FLECS_ENET_OK;
}

static flecs_enet_operation_result_t control(struct fe_payload *request)
{
	struct fe_host *h = request->host;
	const flecs_enet_request_t *d = &request->desc;
	flecs_enet_operation_result_t r = { .peer = d->peer };
	if (d->operation == FLECS_ENET_OPEN) {
		r.code = open_host(h);
		return r;
	}
	if (h->status.state != FLECS_ENET_HOST_OPEN) {
		r.code = FLECS_ENET_STATE;
		return r;
	}
	struct fe_peer *p = NULL;
	if (d->peer) {
		r.code = check_peer(h, d, &p);
		if (r.code)
			return r;
	}
	switch (d->operation) {
	case FLECS_ENET_CONNECT: {
		uint32_t channels = d->channels ? d->channels :
			h->status.active.channel_limit;
		if (channels > h->status.active.channel_limit) {
			r.code = FLECS_ENET_ARGUMENT;
			break;
		}
		ENetAddress address = fe_address(&d->address);
		ENetPeer *native = fe_host_connect(h->native, &address,
			channels, d->data);
		if (!native) {
			r.code = FLECS_ENET_TRANSPORT;
			break;
		}
		p = fe_new_peer(h, native, d->correlation,
			request->tuned ? &request->peer_config : NULL);
		if (!p) {
			fe_peer_reset(native);
			r.code = FLECS_ENET_NOMEM;
			break;
		}
		if (request->startup)
			ecs_add_pair(h->world, p->entity,
				h->ctx->declaration, request->startup);
		p->status.data = d->data;
		fe_peer_sync(p);
		r.peer = p->entity;
		break;
	}
	case FLECS_ENET_DISCONNECT:
		fe_peer_disconnect(p->native, d->data);
		p->status.state = FLECS_ENET_PEER_DISCONNECTING;
		break;
	case FLECS_ENET_DISCONNECT_LATER:
		fe_peer_disconnect_later(p->native, d->data);
		p->status.state = FLECS_ENET_PEER_DISCONNECTING;
		break;
	case FLECS_ENET_DISCONNECT_NOW:
	case FLECS_ENET_RESET:
		if (d->operation == FLECS_ENET_RESET)
			fe_peer_reset(p->native);
		else
			fe_peer_disconnect_now(p->native, d->data);
		p->native->data = NULL;
		p->native = NULL;
		p->status.state = FLECS_ENET_PEER_DISCONNECTED;
		p->status.data = d->data;
		p->diagnostic = true;
		p->diagnostic_result = FLECS_ENET_OK;
		break;
	case FLECS_ENET_BANDWIDTH:
		fe_host_bandwidth_limit(h->native, d->incoming_bandwidth,
			d->outgoing_bandwidth);
		h->status.active.incoming_bandwidth = d->incoming_bandwidth;
		h->status.active.outgoing_bandwidth = d->outgoing_bandwidth;
		break;
	case FLECS_ENET_COMPRESSION:
		if (d->compression == FLECS_ENET_COMPRESSION_NONE)
			fe_host_compress(h->native, NULL);
		else if (fe_host_compress_with_range_coder(h->native) < 0)
			r.code = FLECS_ENET_NOMEM;
		if (!r.code)
			h->status.active.compression = d->compression;
		break;
	case FLECS_ENET_PING:
		fe_peer_ping(p->native);
		break;
	case FLECS_ENET_PING_INTERVAL:
		fe_peer_ping_interval(p->native, d->interval_ms);
		break;
	case FLECS_ENET_TIMEOUT:
		fe_peer_timeout(p->native, d->timeout_limit,
			d->timeout_minimum_ms, d->timeout_maximum_ms);
		break;
	case FLECS_ENET_THROTTLE:
		fe_peer_throttle_configure(p->native, d->interval_ms,
			d->acceleration, d->deceleration);
		break;
	default:
		r.code = FLECS_ENET_ARGUMENT;
		break;
	}
	if (p)
		fe_peer_sync(p);
	return r;
}

static bool close_host(struct fe_payload *close)
{
	struct fe_host *h = close->host;
	h->closing = true;
	h->closing_request = close->entity;
	/* Cancellation spans frames when consumers retain result events. */
	while (h->requests) {
		struct fe_payload *p = h->requests;
		if (p == close)
			p = p->next;
		if (!p)
			break;
		if (!prepare(p))
			return false;
		fe_finish(p, (flecs_enet_operation_result_t){
			.code = FLECS_ENET_STATE, .peer = p->desc.peer });
	}
	if (!prepare(close))
		return false;
	fe_teardown(h->world, h->entity, h);
	h->status.state = FLECS_ENET_HOST_CLOSED;
	h->status.fault = FLECS_ENET_OK;
	h->closing = false;
	fe_finish(close, (flecs_enet_operation_result_t){0});
	return true;
}

flecs_enet_result_t fe_receive(ecs_world_t *world, ecs_entity_t entity,
	struct fe_host *h)
{
	/* Merge admitted commands and select lifecycle/tuning by ticket.
	 * Reserve a result-event slot BEFORE executing each operation.
	 * OPEN: normalize config, convert address, create native host,
	 * configure
	 * packet/waiting limits and compression; unwind partial allocations.
	 * CONNECT: reserve native slot, create fresh CONNECTING peer child;
	 * publish operation result separately from eventual CONNECT event.
	 * CLOSE: cancel other pending requests with STATE results
	 * incrementally,
	 * retaining work under event pressure; disconnect peers, release native
	 * packets/host when cancellation finishes; retain public peer/event
	 * data.
	 * DISCONNECT*: validate live association; map graceful/now/later/reset;
	 * now/reset synthesize one local DISCONNECT (upstream has no local
	 * one).
	 * Tuning: map bandwidth/compression/ping/timeout/throttle functions;
	 * compression allocation failure leaves previous effective config
	 * intact.
	 * Retry pending native event before service(host,&event,0).
	 * If dispatch capacity is exhausted call service(host,NULL,0) instead.
	 * Service -1 faults only this host and closes native associations;
	 * retain
	 * eventual diagnostic DISCONNECT events under pressure, never lose
	 * them.
	 */
	(void)world;
	(void)entity;
	for (struct fe_payload *p; (p = first(h, false)); ) {
		if (p->desc.operation == FLECS_ENET_CLOSE) {
			if (!close_host(p))
				break;
		} else {
			if (!prepare(p))
				break;
			fe_finish(p, control(p));
		}
	}
	for (struct fe_peer *p = h->peers; p; p = p->next) {
		if (p->diagnostic && fe_diagnostic(p))
			break;
	}
	if (!h->native)
		return FLECS_ENET_OK;
	uint32_t budget = h->status.active.limits.events_per_progress;
	for (uint32_t i = 0; i < budget && !h->closing; i++) {
		if (h->has_pending) {
			/* Detach: sequence faults may tear down the host. */
			ENetEvent event = h->pending_event;
			h->has_pending = false;
			flecs_enet_result_t r = fe_publish(h->world, h->entity,
				h, &event);
			if (r) {
				if (!h->native) {
					if (event.packet)
						fe_packet_destroy(event.packet);
					return r;
				}
				h->pending_event = event;
				h->has_pending = true;
				break;
			}
			continue;
		}
		if (fe_event_room(h, 0))
			break;
		ENetEvent event;
		int serviced = fe_host_service(h->native, &event, 0);
		if (serviced < 0) {
			fault(h, FLECS_ENET_TRANSPORT);
			return FLECS_ENET_TRANSPORT;
		}
		if (!serviced)
			return FLECS_ENET_OK;
		flecs_enet_result_t r = fe_publish(h->world, h->entity,
			h, &event);
		if (r) {
			if (!h->native) {
				if (event.packet)
					fe_packet_destroy(event.packet);
				return r;
			}
			h->pending_event = event;
			h->has_pending = true;
			break;
		}
	}
	if (h->native && fe_host_service(h->native, NULL, 0) < 0)
		fault(h, FLECS_ENET_TRANSPORT);
	return FLECS_ENET_OK;
}

struct accepted_packet {
	struct fe_host *host;
	size_t size;
};

static void packet_released(ENetPacket *packet)
{
	struct accepted_packet *owner = packet->userData;
	struct fe_host *h = owner->host;
	h->status.outgoing_packets--;
	h->status.outgoing_bytes -= owner->size;
	free(owner);
	fe_sync(h);
	fe_unref(h);
}

static flecs_enet_result_t send_packet(struct fe_payload *p,
	struct fe_peer *peer)
{
	if (p->desc.channel >= peer->status.channel_count)
		return FLECS_ENET_ARGUMENT;
	enet_uint32 flags = 0;
	switch (p->desc.delivery) {
	case FLECS_ENET_RELIABLE:
		flags = ENET_PACKET_FLAG_RELIABLE;
		break;
	case FLECS_ENET_UNSEQUENCED: {
		size_t overhead = sizeof(ENetProtocolHeader) +
			sizeof(ENetProtocolSendFragment);
		if (peer->native->mtu <= overhead ||
			p->size > peer->native->mtu - overhead)
			return FLECS_ENET_LIMIT;
		flags = ENET_PACKET_FLAG_UNSEQUENCED;
		break;
	}
	case FLECS_ENET_UNRELIABLE_FRAGMENT:
		flags = ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
		break;
	default:
		break;
	}
	struct accepted_packet *owner = malloc(sizeof(*owner));
	if (!owner)
		return FLECS_ENET_NOMEM;
	ENetPacket *packet = fe_packet_create(p->bytes, p->size, flags);
	if (!packet) {
		free(owner);
		return FLECS_ENET_NOMEM;
	}
	if (fe_peer_send(peer->native, p->desc.channel, packet) < 0) {
		fe_packet_destroy(packet);
		free(owner);
		return FLECS_ENET_TRANSPORT;
	}
	struct fe_host *h = p->host;
	*owner = (struct accepted_packet){ .host = h, .size = p->size };
	packet->userData = owner;
	packet->freeCallback = packet_released;
	fe_ref(h);
	h->status.outgoing_packets++;
	h->status.outgoing_bytes += p->size;
	/* ENet may accept an empty packet without retaining any commands. */
	if (!packet->referenceCount)
		fe_packet_destroy(packet);
	return FLECS_ENET_OK;
}

static int peer_order(const void *a, const void *b)
{
	const struct fe_peer *pa = *(struct fe_peer *const *)a;
	const struct fe_peer *pb = *(struct fe_peer *const *)b;
	return pa->entity < pb->entity ? -1 : pa->entity > pb->entity;
}

static flecs_enet_operation_result_t send_request(struct fe_payload *p)
{
	struct fe_host *h = p->host;
	flecs_enet_operation_result_t r = { .peer = p->desc.peer };
	if (p->desc.operation == FLECS_ENET_SEND) {
		r.attempted = 1;
		struct fe_peer *peer;
		r.code = check_peer(h, &p->desc, &peer);
		if (r.code)
			return r;
		if (p->desc.channel >= peer->status.channel_count) {
			r.code = FLECS_ENET_ARGUMENT;
			return r;
		}
	}
	if (h->status.state != FLECS_ENET_HOST_OPEN) {
		r.code = FLECS_ENET_STATE;
		return r;
	}
	if (p->desc.operation == FLECS_ENET_FLUSH) {
		fe_host_flush(h->native);
		return r;
	}
	uint32_t count = 0;
	for (struct fe_peer *peer = h->peers; peer; peer = peer->next) {
		if (peer->native &&
			peer->status.state == FLECS_ENET_PEER_CONNECTED &&
			(!p->desc.peer || p->desc.peer == peer->entity))
			count++;
	}
	const flecs_enet_limits_t *l = &h->status.active.limits;
	if (count > l->max_outgoing_packets ||
		(p->size && count > l->max_outgoing_bytes / p->size)) {
		r.code = FLECS_ENET_LIMIT;
		return r;
	}
	size_t bytes = (size_t)count * p->size;
	size_t occupied = h->status.outgoing_bytes - p->reservation;
	if (count > l->max_outgoing_packets - h->status.outgoing_packets ||
		bytes > l->max_outgoing_bytes - occupied) {
		r.code = FLECS_ENET_BACKPRESSURE;
		return r;
	}
	if (p->desc.operation == FLECS_ENET_BROADCAST &&
		p->desc.channel >= h->status.active.channel_limit) {
		r.code = FLECS_ENET_ARGUMENT;
		return r;
	}
	if (!count)
		return r;
	struct fe_peer **peers = malloc((size_t)count * sizeof(*peers));
	if (!peers) {
		r.code = FLECS_ENET_NOMEM;
		return r;
	}
	uint32_t n = 0;
	for (struct fe_peer *peer = h->peers; peer; peer = peer->next) {
		if (peer->native &&
			peer->status.state == FLECS_ENET_PEER_CONNECTED &&
			(!p->desc.peer || p->desc.peer == peer->entity))
			peers[n++] = peer;
	}
	qsort(peers, count, sizeof(*peers), peer_order);
	/* Transfer admission bytes before acceptance to avoid SIZE_MAX wrap. */
	h->status.outgoing_bytes -= p->reservation;
	p->reservation = 0;
	r.attempted = count;
	flecs_enet_result_t failure = FLECS_ENET_OK;
	for (uint32_t i = 0; i < count; i++) {
		flecs_enet_result_t code = send_packet(p, peers[i]);
		if (!code)
			r.accepted++;
		else
			failure = code;
	}
	free(peers);
	if (r.accepted != count)
		r.code = p->desc.operation == FLECS_ENET_SEND ? failure :
			r.accepted ? FLECS_ENET_PARTIAL : FLECS_ENET_TRANSPORT;
	return r;
}

flecs_enet_result_t fe_outgoing(ecs_world_t *world, ecs_entity_t entity,
	struct fe_host *h)
{
	/* Select SEND/BROADCAST/FLUSH by ticket after application update.
	 * Revalidate generation/state/channel; reserve result-event capacity.
	 * SEND: create ENet packet, transfer ownership only after acceptance;
	 * failed send destroys unaccepted packet and publishes TRANSPORT.
	 * Track accepted bytes until the private packet free callback fires.
	 * BROADCAST: snapshot connected peers sorted by entity ID; reserve all
	 * potential outgoing copies before first send, record partial
	 * acceptance.
	 * Never retry an accepted peer or replay an entire partial broadcast.
	 * FLUSH calls ENet flush; it provides no delivery/acknowledgement
	 * promise.
	 * Host closed during Receive causes queued SEND to complete STATE.
	 */
	(void)world;
	(void)entity;
	if (h->closing)
		return FLECS_ENET_BACKPRESSURE;
	for (struct fe_payload *p; (p = first(h, true)); ) {
		if (!prepare(p))
			return FLECS_ENET_BACKPRESSURE;
		flecs_enet_operation_result_t r = send_request(p);
		if (r.code == FLECS_ENET_BACKPRESSURE)
			return r.code;
		fe_finish(p, r);
	}
	return FLECS_ENET_OK;
}
