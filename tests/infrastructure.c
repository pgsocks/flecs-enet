#include "fixture.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>

int main(void)
{
	assert(enet_initialize() == 0);
	fe_test_reset();
	struct fixture f;
	assert(fixture_init(&f));
	fixture_fini(&f);
	ENetHost *h = fe_host_create(NULL, 2, 2, 0, 0);
	assert(h && fe_test.hosts == 1);
	fe_test_inject(h, 0, ENET_EVENT_TYPE_CONNECT, NULL, 0, 0, 0);
	fe_test_inject(h, 0, ENET_EVENT_TYPE_RECEIVE, "abc", 3, 1,
		ENET_PACKET_FLAG_RELIABLE);
	assert(fe_host_service(h, NULL, 0) == 0);
	assert(fe_test.dispatches == 0 && fe_test.protocol_services == 1);
	ENetEvent e;
	assert(fe_host_service(h, &e, 0) == 1);
	assert(e.type == ENET_EVENT_TYPE_CONNECT);
	assert(fe_host_service(h, &e, 0) == 1);
	assert(e.type == ENET_EVENT_TYPE_RECEIVE && e.channelID == 1);
	assert(!memcmp(e.packet->data, "abc", 3));
	fe_packet_destroy(e.packet);
	ENetPacket *p = fe_packet_create("def", 3, ENET_PACKET_FLAG_RELIABLE);
	assert(fe_peer_send(fe_test_peer(h, 0), 1, p) == 0);
	assert(fe_test.packets == 1);
	fe_test_release();
	assert(fe_test.packets == 0);
	fe_fail_host = 1;
	assert(!fe_host_create(NULL, 1, 1, 0, 0));
	fe_host_destroy(h);
	assert(fe_test.hosts == 0 && fe_test.packets == 0);
	enet_deinitialize();
	return 0;
}
