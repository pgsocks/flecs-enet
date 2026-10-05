#ifndef FLECS_ENET_TEST_BOUNDARY_H
#define FLECS_ENET_TEST_BOUNDARY_H
#include <enet/enet.h>
#include <stdbool.h>

/* Private test-only instrumentation; each executable owns its state. */
struct fe_test_counters {
	unsigned hosts, packets, sends, protocol_services, flushes, tuning;
	unsigned dispatches, disconnects, initializes, deinitializes, runtimes;
	unsigned last_channel;
	enet_uint32 last_flags;
	size_t last_size;
	unsigned char last_data[32];
};
extern struct fe_test_counters fe_test;
extern unsigned fe_fail_host, fe_fail_connect, fe_fail_packet;
extern unsigned fe_fail_service, fe_fail_send_at, fe_fail_compression;
extern unsigned fe_fail_initialize;
int fe_initialize(void);
void fe_deinitialize(void);
void fe_test_reset(void);
void fe_test_release(void);
ENetHost *fe_test_latest(void);
ENetPeer *fe_test_peer(ENetHost *, unsigned);
void fe_test_inject(ENetHost *, unsigned, ENetEventType,
	const void *, size_t, enet_uint8, enet_uint32);

ENetHost *fe_host_create(const ENetAddress *, size_t, size_t,
	enet_uint32, enet_uint32);
void fe_host_destroy(ENetHost *);
int fe_host_service(ENetHost *, ENetEvent *, enet_uint32);
ENetPeer *fe_host_connect(ENetHost *, const ENetAddress *, size_t,
	enet_uint32);
void fe_host_flush(ENetHost *);
int fe_peer_send(ENetPeer *, enet_uint8, ENetPacket *);
ENetPacket *fe_packet_create(const void *, size_t, enet_uint32);
void fe_packet_destroy(ENetPacket *);
void fe_host_bandwidth_limit(ENetHost *, enet_uint32, enet_uint32);
void fe_host_compress(ENetHost *, const ENetCompressor *);
int fe_host_compress_with_range_coder(ENetHost *);
void fe_peer_disconnect(ENetPeer *, enet_uint32);
void fe_peer_disconnect_now(ENetPeer *, enet_uint32);
void fe_peer_disconnect_later(ENetPeer *, enet_uint32);
void fe_peer_reset(ENetPeer *);
void fe_peer_ping(ENetPeer *);
void fe_peer_ping_interval(ENetPeer *, enet_uint32);
void fe_peer_timeout(ENetPeer *, enet_uint32, enet_uint32, enet_uint32);
void fe_peer_throttle_configure(ENetPeer *, enet_uint32, enet_uint32,
	enet_uint32);
#endif
