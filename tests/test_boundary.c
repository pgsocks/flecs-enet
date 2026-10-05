#include "test_boundary.h"
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fe_test_counters fe_test;
unsigned fe_fail_initialize;
unsigned fe_fail_host, fe_fail_connect, fe_fail_packet;
unsigned fe_fail_service, fe_fail_send_at, fe_fail_compression;

struct queued {
	ENetEvent event;
	struct queued *next;
};
struct held {
	ENetPacket *packet;
	ENetPeer *peer;
	struct held *next;
};
struct test_host {
	ENetHost native;
	struct queued *head, *tail;
	struct held *held;
	struct test_host *next;
};
static struct test_host *hosts;

void fe_test_reset(void)
{
	assert(!hosts && !fe_test.hosts && !fe_test.packets &&
		!fe_test.runtimes);
	memset(&fe_test, 0, sizeof(fe_test));
	fe_fail_initialize = 0;
	fe_fail_host = fe_fail_connect = fe_fail_packet = 0;
	fe_fail_service = fe_fail_send_at = fe_fail_compression = 0;
}

int fe_initialize(void)
{
	fe_test.initializes++;
	if (fe_fail_initialize) {
		fe_fail_initialize--;
		return -1;
	}
	fe_test.runtimes++;
	return 0;
}

void fe_deinitialize(void)
{
	assert(fe_test.runtimes);
	if (fe_test.runtimes == 1)
		assert(!fe_test.hosts && !fe_test.packets);
	fe_test.runtimes--;
	fe_test.deinitializes++;
}

static void release(struct test_host *host, ENetPeer *peer)
{
	struct held **link = &host->held;
	while (*link) {
		struct held *item = *link;
		if (peer && item->peer != peer) {
			link = &item->next;
			continue;
		}
		*link = item->next;
		item->packet->referenceCount = 0;
		fe_packet_destroy(item->packet);
		free(item);
	}
}

void fe_test_release(void)
{
	for (struct test_host *host = hosts; host; host = host->next)
		release(host, NULL);
}

ENetHost *fe_test_latest(void)
{
	return hosts ? &hosts->native : NULL;
}

ENetPeer *fe_test_peer(ENetHost *host, unsigned index)
{
	assert(host && index < host->peerCount);
	return &host->peers[index];
}

ENetPacket *fe_packet_create(const void *data, size_t size, enet_uint32 flags)
{
	if (fe_fail_packet) {
		fe_fail_packet--;
		return NULL;
	}
	ENetPacket *packet = enet_packet_create(data, size, flags);
	if (packet)
		fe_test.packets++;
	return packet;
}

void fe_packet_destroy(ENetPacket *packet)
{
	if (!packet)
		return;
	assert(fe_test.packets);
	fe_test.packets--;
	/* Runs the module's real private free callback if it installed one. */
	enet_packet_destroy(packet);
}

void fe_test_inject(ENetHost *native, unsigned index, ENetEventType type,
	const void *data, size_t size, enet_uint8 channel, enet_uint32 flags)
{
	struct test_host *host = (struct test_host *)native;
	ENetPeer *peer = fe_test_peer(native, index);
	struct queued *q = calloc(1, sizeof(*q));
	assert(q);
	q->event.type = type;
	q->event.peer = peer;
	q->event.channelID = channel;
	if (type == ENET_EVENT_TYPE_RECEIVE) {
		q->event.packet = fe_packet_create(data, size, flags);
		assert(q->event.packet);
	}
	if (type == ENET_EVENT_TYPE_CONNECT)
		peer->state = ENET_PEER_STATE_CONNECTED;
	if (type == ENET_EVENT_TYPE_DISCONNECT)
		peer->state = ENET_PEER_STATE_DISCONNECTED;
	if (host->tail)
		host->tail->next = q;
	else
		host->head = q;
	host->tail = q;
}

ENetHost *fe_host_create(const ENetAddress *address, size_t count,
	size_t channels, enet_uint32 incoming, enet_uint32 outgoing)
{
	if (fe_fail_host) {
		fe_fail_host--;
		return NULL;
	}
	struct test_host *host = calloc(1, sizeof(*host));
	assert(host);
	host->native.peers = calloc(count, sizeof(ENetPeer));
	assert(host->native.peers);
	host->native.peerCount = count;
	host->native.channelLimit = channels;
	host->native.incomingBandwidth = incoming;
	host->native.outgoingBandwidth = outgoing;
	host->native.maximumPacketSize = ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE;
	if (address)
		host->native.address = *address;
	if (!host->native.address.port)
		host->native.address.port = 19000;
	for (size_t i = 0; i < count; i++) {
		host->native.peers[i].host = &host->native;
		host->native.peers[i].channelCount = channels;
		host->native.peers[i].mtu = ENET_HOST_DEFAULT_MTU;
		host->native.peers[i].pingInterval =
			ENET_PEER_PING_INTERVAL;
		host->native.peers[i].timeoutLimit =
			ENET_PEER_TIMEOUT_LIMIT;
		host->native.peers[i].timeoutMinimum =
			ENET_PEER_TIMEOUT_MINIMUM;
		host->native.peers[i].timeoutMaximum =
			ENET_PEER_TIMEOUT_MAXIMUM;
		host->native.peers[i].packetThrottleInterval =
			ENET_PEER_PACKET_THROTTLE_INTERVAL;
	}
	host->next = hosts;
	hosts = host;
	fe_test.hosts++;
	return &host->native;
}

void fe_host_destroy(ENetHost *native)
{
	struct test_host *host = (struct test_host *)native;
	release(host, NULL);
	while (host->head) {
		struct queued *q = host->head;
		host->head = q->next;
		fe_packet_destroy(q->event.packet);
		free(q);
	}
	struct test_host **link = &hosts;
	while (*link != host)
		link = &(*link)->next;
	*link = host->next;
	free(native->peers);
	free(host);
	fe_test.hosts--;
}

int fe_host_service(ENetHost *native, ENetEvent *event, enet_uint32 timeout)
{
	assert(timeout == 0);
	if (fe_fail_service) {
		fe_fail_service--;
		return -1;
	}
	if (!event) {
		fe_test.protocol_services++;
		return 0;
	}
	memset(event, 0, sizeof(*event));
	struct test_host *host = (struct test_host *)native;
	if (!host->head)
		return 0;
	struct queued *q = host->head;
	host->head = q->next;
	if (!host->head)
		host->tail = NULL;
	*event = q->event;
	free(q);
	fe_test.dispatches++;
	return 1;
}

ENetPeer *fe_host_connect(ENetHost *host, const ENetAddress *address,
	size_t channels, enet_uint32 data)
{
	(void)data;
	if (fe_fail_connect) {
		fe_fail_connect--;
		return NULL;
	}
	for (size_t i = 0; i < host->peerCount; i++) {
		ENetPeer *peer = &host->peers[i];
		if (peer->state != ENET_PEER_STATE_DISCONNECTED)
			continue;
		peer->address = *address;
		peer->channelCount = channels;
		peer->state = ENET_PEER_STATE_CONNECTING;
		return peer;
	}
	return NULL;
}

int fe_peer_send(ENetPeer *peer, enet_uint8 channel, ENetPacket *packet)
{
	fe_test.sends++;
	if (fe_fail_send_at == fe_test.sends)
		return -1;
	if (peer->state != ENET_PEER_STATE_CONNECTED ||
		channel >= peer->channelCount)
		return -1;
	fe_test.last_channel = channel;
	fe_test.last_flags = packet->flags;
	fe_test.last_size = packet->dataLength;
	size_t n = packet->dataLength < sizeof(fe_test.last_data) ?
		packet->dataLength : sizeof(fe_test.last_data);
	if (n)
		memcpy(fe_test.last_data, packet->data, n);
	struct held *item = calloc(1, sizeof(*item));
	assert(item);
	item->packet = packet;
	item->peer = peer;
	struct test_host *host = (struct test_host *)peer->host;
	item->next = host->held;
	host->held = item;
	packet->referenceCount++;
	return 0;
}

void fe_host_flush(ENetHost *host)
{
	(void)host;
	fe_test.flushes++;
	/* Test retains accepted packets until explicit release or teardown. */
}

void fe_host_bandwidth_limit(ENetHost *host, enet_uint32 in, enet_uint32 out)
{
	host->incomingBandwidth = in;
	host->outgoingBandwidth = out;
	fe_test.tuning++;
}

void fe_host_compress(ENetHost *host, const ENetCompressor *compressor)
{
	(void)host;
	(void)compressor;
	fe_test.tuning++;
}

int fe_host_compress_with_range_coder(ENetHost *host)
{
	(void)host;
	if (fe_fail_compression) {
		fe_fail_compression--;
		return -1;
	}
	fe_test.tuning++;
	return 0;
}

static void disconnect(ENetPeer *peer, bool immediate)
{
	fe_test.disconnects++;
	peer->state = immediate ? ENET_PEER_STATE_DISCONNECTED :
		ENET_PEER_STATE_DISCONNECTING;
	if (immediate)
		release((struct test_host *)peer->host, peer);
}

void fe_peer_disconnect(ENetPeer *peer, enet_uint32 data)
{
	(void)data;
	disconnect(peer, false);
}
void fe_peer_disconnect_now(ENetPeer *peer, enet_uint32 data)
{
	(void)data;
	disconnect(peer, true);
}
void fe_peer_disconnect_later(ENetPeer *peer, enet_uint32 data)
{
	(void)data;
	disconnect(peer, false);
}
void fe_peer_reset(ENetPeer *peer)
{
	disconnect(peer, true);
}
void fe_peer_ping(ENetPeer *peer)
{
	(void)peer;
	fe_test.tuning++;
}
void fe_peer_ping_interval(ENetPeer *peer, enet_uint32 interval)
{
	peer->pingInterval = interval;
	fe_test.tuning++;
}
void fe_peer_timeout(ENetPeer *peer, enet_uint32 limit, enet_uint32 minimum,
	enet_uint32 maximum)
{
	peer->timeoutLimit = limit;
	peer->timeoutMinimum = minimum;
	peer->timeoutMaximum = maximum;
	fe_test.tuning++;
}
void fe_peer_throttle_configure(ENetPeer *peer, enet_uint32 interval,
	enet_uint32 acceleration, enet_uint32 deceleration)
{
	peer->packetThrottleInterval = interval;
	peer->packetThrottleAcceleration = acceleration;
	peer->packetThrottleDeceleration = deceleration;
	fe_test.tuning++;
}
