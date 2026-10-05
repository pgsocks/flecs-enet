/**
 * @file flecs_enet.h
 * @brief ECS-first ENet 1.3 transport, C11 with C++17 inclusion support.
 *
 * All calls, component access, deletion and ecs_progress must occur on the
 * world-owning thread; no worker stages, concurrency or reentry. Submission
 * permits the main stage and main-thread deferral. Import requires the real,
 * writable, nondeferred world. No process-global component IDs are exposed.
 * Import acquires ENet once per world; world cleanup releases it after all
 * native owners. Independent ENet users balance their own initialization.
 *
 * Load script-defined HostConfig/ConnectionConfig/PeerConfig and Start.
 * Startup runs once, reports StartStatus, and composes generic requests.
 * Config changes take effect on explicit restart; keep config unchanged
 * while startup is pending. Runtime tuning uses requests. Peers, requests
 * and events are ChildOf(host). Host is exclusive and denotes transport
 * association. Applications
 * may attach their own components, but must not overwrite, remove, clone,
 * inherit or reparent module-managed components/entities. Deleting a pending
 * request cancels it; deleting a completed request/event releases storage.
 * Host deletion cancels work and deletes all its children without events.
 * Disconnect retains peers; reopening uses fresh generation-aware entities.
 *
 * Public values copy as plain snapshots. Packet data is borrowed and const;
 * copying a view does not extend its lifetime. Structural changes invalidate
 * component pointers; receive bytes survive until their event is deleted.
 * Request packet views expire at completion/cancellation or request deletion.
 * Private native owners prohibit copying, move by transferring and clearing
 * the source, and release exactly once on removal/destruction/world fini.
 *
 * Lifecycle/tuning and nonblocking receive run before EcsOnUpdate; outgoing
 * sends/broadcast/flush run after EcsPostUpdate. Custom pipelines must include
 * both exported phases in that order with merges before their consumers.
 * Per-host request tickets are FIFO within each phase; cross-phase order is
 * receive before send, even if a send was submitted before a close. Work
 * submitted during update is eligible for Send this frame, Receive next frame.
 *
 * Finite limits never silently drop module work. Admission pressure returns
 * BACKPRESSURE without a request or copying responsibility to the caller.
 * Admitted work remains pending until result-event and outgoing capacity are
 * available. Event dispatch pauses when full; ENet service(host,NULL,0)
 * continues protocol traffic. ENet's own unreliable dropping/waiting limits
 * still apply. Accepted sends are never replayed by a capacity retry.
 *
 * Outputs are disjoint from inputs. Required outputs must be non-NULL; they
 * are zeroed on failure. Inputs are borrowed only for the call and copied on
 * success. Errors use return codes/results, never errno. Detectable failures
 * leave host/peer state unchanged unless the operation table says otherwise.
 * See docs/api.md for operation/state/pressure details and docs/usage.md.
 */
#ifndef FLECS_ENET_H
#define FLECS_ENET_H

#include <flecs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Local admission/execution result; never a delivery acknowledgement. */
typedef enum flecs_enet_result {
	FLECS_ENET_OK = 0, /**< Registration/admission/execution succeeded. */
	FLECS_ENET_ARGUMENT, /**< Invalid pointer, range or descriptor. */
	FLECS_ENET_STATE, /**< Operation conflicts with current state. */
	FLECS_ENET_NOT_FOUND, /**< Dead/missing/wrong-host entity. */
	FLECS_ENET_BACKPRESSURE, /**< Retry admission or leave admitted work. */
	FLECS_ENET_NOMEM, /**< Allocation failed; no new owner transferred. */
	FLECS_ENET_TRANSPORT, /**< ENet operation failed. */
	FLECS_ENET_PARTIAL, /**< Broadcast accepted only a proper subset. */
	FLECS_ENET_SCHEMA, /**< Reserved module namespace incompatible. */
	FLECS_ENET_LIMIT, /**< Permanent size/sequence bound exceeded. */
	FLECS_ENET_NOT_IMPLEMENTED /**< Reserved; never returned. */
} flecs_enet_result_t;

/** IPv4 numeric endpoint; octets are in wire order, port in host order.
 * No DNS/blocking name resolution. Bind 0.0.0.0 means ANY, port 0 ephemeral;
 * connect requires a nonzero address and port. Broadcast address disallowed.
 */
typedef struct flecs_enet_address {
	uint8_t ip[4]; /**< Four IPv4 octets. */
	uint16_t port; /**< 0..65535 for bind, 1..65535 for connect. */
} flecs_enet_address_t;

/** Compression is a host property; both endpoints must support it. */
typedef enum flecs_enet_compression {
	FLECS_ENET_COMPRESSION_NONE = 0, /**< Default; disabled. */
	FLECS_ENET_COMPRESSION_RANGE_CODER /**< Upstream range coder only. */
} flecs_enet_compression_t;

/** Packet mode; ordering is per channel, never across channels.
 * UNRELIABLE follows upstream promotion of oversized packets to reliable
 * fragments. UNRELIABLE_FRAGMENT opts into unreliable fragment delivery.
 * UNSEQUENCED is limited to an unfragmented MTU payload at execution.
 */
typedef enum flecs_enet_delivery {
	FLECS_ENET_RELIABLE = 0, /**< Reliable, sequenced (default). */
	FLECS_ENET_UNRELIABLE, /**< Sequenced, may drop/promote fragments. */
	FLECS_ENET_UNSEQUENCED, /**< Unsequenced and unreliable. */
	FLECS_ENET_UNRELIABLE_FRAGMENT /**< Sequenced unreliable fragments. */
} flecs_enet_delivery_t;

/** Host life cycle. Missing HostStatus on a configured entity means CLOSED. */
typedef enum flecs_enet_host_state {
	FLECS_ENET_HOST_CLOSED = 0, /**< No transport; OPEN permitted. */
	FLECS_ENET_HOST_OPEN, /**< Service and operations permitted. */
	FLECS_ENET_HOST_FAULTED /**< Sticky service fault; CLOSE then OPEN. */
} flecs_enet_host_state_t;

/** Peer state; terminal entities remain inspectable until deletion. */
typedef enum flecs_enet_peer_state {
	FLECS_ENET_PEER_CONNECTING = 0, /**< Native slot reserved. */
	FLECS_ENET_PEER_CONNECTED, /**< CONNECT event dispatched. */
	FLECS_ENET_PEER_DISCONNECTING, /**< Graceful/draining pending. */
	FLECS_ENET_PEER_DISCONNECTED /**< No reusable native association. */
} flecs_enet_peer_state_t;

/** Finite per-host limits; zero selects the documented default.
 * Nonzero counts accept 1..UINT32_MAX, bytes 1..SIZE_MAX, subject to
 * max_packet_bytes <= max_event_bytes,max_outgoing_bytes,max_waiting_bytes.
 * ENet waiting bytes are per native peer, separate from retained events.
 */
typedef struct flecs_enet_limits {
	uint32_t max_requests; /**< Pending requests: 0=1024. */
	uint32_t max_events; /**< Retained events incl. results: 0=1024. */
	uint32_t max_outgoing_packets; /**< ENet-owned packets: 0=8192. */
	uint32_t events_per_progress; /**< Dispatch budget: 0=64. */
	size_t max_event_bytes; /**< Retained receive bytes: 0=32 MiB. */
	size_t max_outgoing_bytes; /**< Pending + accepted: 0=32 MiB. */
	size_t max_packet_bytes; /**< One packet: 0=1 MiB, <=32 MiB. */
	size_t max_waiting_bytes; /**< ENet waiting bytes/peer: 0=32 MiB. */
} flecs_enet_limits_t;

/** Application-owned HostConfig component; zero initialization is valid. */
typedef struct flecs_enet_host_config {
	bool listen; /**< false: outbound-only; true: bind address. */
	flecs_enet_address_t address; /**< Ignored when listen=false. */
	uint32_t peer_limit; /**< 0=32; otherwise 1..4095. */
	uint32_t channel_limit; /**< 0=2; otherwise 1..255. */
	uint32_t incoming_bandwidth; /**< Bytes/s; 0 unlimited (ENet policy). */
	uint32_t outgoing_bandwidth; /**< Bytes/s; 0 unlimited. */
	flecs_enet_compression_t compression; /**< NONE by default. */
	flecs_enet_limits_t limits; /**< Zero selects finite defaults. */
} flecs_enet_host_config_t;

/** Outbound declaration directly beneath its HostConfig entity.
 * Snapshot at Start admission. Zero channels selects the active host limit.
 */
typedef struct flecs_enet_connection_config {
	flecs_enet_address_t address; /**< Nonzero IPv4 endpoint and port. */
	uint32_t channels; /**< 0=host limit, otherwise 1..host limit. */
	uint32_t data; /**< ENet connect protocol data. */
	uint64_t correlation; /**< Application identity; 0 unspecified. */
} flecs_enet_connection_config_t;

/** Optional peer tuning. Zero fields/groups preserve ENet defaults.
 * Host defaults snapshot at OPEN execution; connection override replaces
 * the whole default configuration and snapshots at admission.
 */
typedef struct flecs_enet_peer_config {
	uint32_t ping_interval_ms; /**< 0=default, otherwise 1..UINT32_MAX. */
	uint32_t timeout_limit; /**< Timeout group: all zero or all nonzero. */
	uint32_t timeout_minimum_ms; /**< Nonzero minimum <= maximum. */
	uint32_t timeout_maximum_ms; /**< Nonzero maximum >= minimum. */
	uint32_t throttle_interval_ms; /**< 0=default with accel/decel zero. */
	uint32_t acceleration; /**< Throttle acceleration: 0..32. */
	uint32_t deceleration; /**< Throttle deceleration: 0..32. */
} flecs_enet_peer_config_t;

/** One explicit startup attempt; Waiting retains Start for prerequisites
 * or admission pressure. Pending consumes Start; duplicates are ignored.
 * Finished requires a new Start to retry. Active transports return STATE.
 */
typedef enum flecs_enet_start_state {
	FLECS_ENET_START_WAITING = 0, /**< Prerequisite/admission wait. */
	FLECS_ENET_START_PENDING, /**< Admitted; execution may wait. */
	FLECS_ENET_START_FINISHED /**< Terminal outcome; explicit retry. */
} flecs_enet_start_state_t;

/** Module-written startup status; Finished means local execution completed,
 * not connection acknowledgement. IDs can refer to deleted entities.
 */
typedef struct flecs_enet_start_status {
	flecs_enet_start_state_t state; /**< Waiting, Pending or Finished. */
	flecs_enet_result_t code; /**< OK, admission pressure or failure. */
	ecs_entity_t request; /**< Admitted request ID, otherwise zero. */
	ecs_entity_t peer; /**< Resulting outbound peer ID, otherwise zero. */
} flecs_enet_start_status_t;

/** Module-written HostStatus component; scalars may be copied. */
typedef struct flecs_enet_host_status {
	flecs_enet_host_state_t state; /**< Current host state. */
	flecs_enet_result_t fault; /**< Sticky fault, otherwise OK. */
	flecs_enet_address_t address; /**< Actual bound address or all zero. */
	flecs_enet_host_config_t active; /**< Normalized effective config. */
	uint64_t last_sequence; /**< Last published event, starts at 1. */
	uint32_t pending_requests; /**< Admitted, unfinished requests. */
	uint32_t retained_events; /**< Includes operation results. */
	uint32_t outgoing_packets; /**< ENet-owned packets. */
	size_t event_bytes; /**< Retained received bytes. */
	size_t outgoing_bytes; /**< Pending plus ENet-owned bytes. */
} flecs_enet_host_status_t;

/** Module-written PeerStatus; Host association also uses (ids.host,host). */
typedef struct flecs_enet_peer_status {
	flecs_enet_peer_state_t state; /**< Current peer state. */
	flecs_enet_address_t address; /**< Copied remote endpoint. */
	uint32_t channel_count; /**< Negotiated count, 1..255. */
	uint32_t data; /**< Most recent connect/disconnect data. */
	uint64_t correlation; /**< CONNECT request identity, 0 inbound. */
} flecs_enet_peer_status_t;

/** Generic request operation; see docs/api.md for applicability/results. */
typedef enum flecs_enet_operation {
	FLECS_ENET_OPEN = 0, /**< Open configured CLOSED host. */
	FLECS_ENET_CLOSE, /**< Cancel work; retain peers/events. */
	FLECS_ENET_CONNECT, /**< Create CONNECTING peer on OPEN host. */
	FLECS_ENET_SEND, /**< One CONNECTED peer; acceptance result. */
	FLECS_ENET_BROADCAST, /**< Connected peers, per-peer acceptance. */
	FLECS_ENET_FLUSH, /**< Push current ENet queues without waiting. */
	FLECS_ENET_DISCONNECT, /**< Graceful disconnect. */
	FLECS_ENET_DISCONNECT_NOW, /**< Immediate disconnect, discard queues. */
	FLECS_ENET_DISCONNECT_LATER, /**< Drain then disconnect. */
	FLECS_ENET_RESET, /**< Immediate local reset; remote may time out. */
	FLECS_ENET_BANDWIDTH, /**< Replace host bandwidth limits. */
	FLECS_ENET_COMPRESSION, /**< Replace host compression mode. */
	FLECS_ENET_PING, /**< Schedule one peer ping. */
	FLECS_ENET_PING_INTERVAL, /**< Replace ping interval in ms. */
	FLECS_ENET_TIMEOUT, /**< Replace timeout limit/minimum/maximum. */
	FLECS_ENET_THROTTLE /**< Replace throttle interval/accel/decel. */
} flecs_enet_operation_t;

/** Input and retained request metadata. Zero unused fields. Payload is passed
 * separately to submit, never via a raw borrowed pointer component.
 * After admission the Request component is immutable/module-owned.
 */
typedef struct flecs_enet_request {
	flecs_enet_operation_t operation; /**< Exactly one operation. */
	ecs_entity_t peer; /**< Live same-host peer; 0 for host operations. */
	uint64_t correlation; /**< Caller identity; any value, 0 unspecified. */
	flecs_enet_address_t address; /**< CONNECT remote endpoint. */
	uint32_t channels; /**< CONNECT: 0=limit, else 1..limit. */
	uint32_t data; /**< CONNECT or DISCONNECT* protocol data. */
	uint8_t channel; /**< SEND/BROADCAST: 0..negotiated_count-1. */
	flecs_enet_delivery_t delivery; /**< SEND/BROADCAST packet mode. */
	flecs_enet_compression_t compression; /**< COMPRESSION replacement. */
	uint32_t incoming_bandwidth; /**< BANDWIDTH bytes/s, 0 unlimited. */
	uint32_t outgoing_bandwidth; /**< BANDWIDTH bytes/s, 0 unlimited. */
	uint32_t interval_ms; /**< PING_INTERVAL/THROTTLE: 1..UINT32_MAX. */
	uint32_t timeout_limit; /**< TIMEOUT: 1..UINT32_MAX. */
	uint32_t timeout_minimum_ms; /**< TIMEOUT: 1..maximum. */
	uint32_t timeout_maximum_ms; /**< TIMEOUT: minimum..UINT32_MAX. */
	uint32_t acceleration; /**< THROTTLE: 0..32. */
	uint32_t deceleration; /**< THROTTLE: 0..32. */
} flecs_enet_request_t;

/** Borrowed immutable Packet component on payload requests/receive events.
 * Byte storage outlives table movement. Request completion removes Packet;
 * cancellation/deletion also invalidates request views. Receive views last
 * until event deletion. Do not free or modify data.
 */
typedef struct flecs_enet_packet {
	const void *data; /**< NULL iff size==0. */
	size_t size; /**< 0..active max_packet_bytes. */
	flecs_enet_delivery_t delivery; /**< Actual upstream packet mode. */
} flecs_enet_packet_t;

/** Retained event type. Each event also has Result when kind==RESULT. */
typedef enum flecs_enet_event_kind {
	FLECS_ENET_EVENT_CONNECT = 0, /**< Peer becomes CONNECTED. */
	FLECS_ENET_EVENT_RECEIVE, /**< Event has Packet component. */
	FLECS_ENET_EVENT_DISCONNECT, /**< Peer becomes DISCONNECTED. */
	FLECS_ENET_EVENT_RESULT /**< Event has operation Result component. */
} flecs_enet_event_kind_t;

/** Module-written Event; process by sequence, not Flecs iteration order. */
typedef struct flecs_enet_event {
	flecs_enet_event_kind_t kind; /**< Transport or operation result. */
	uint64_t sequence; /**< Per-host monotonic, never reused on reopen. */
	uint64_t correlation; /**< Request/peer correlation. */
	ecs_entity_t request; /**< Result source; may be dead after deletion. */
	ecs_entity_t peer; /**< Transport peer or result peer, 0 host-only. */
	uint8_t channel; /**< Receive/send/broadcast channel; 0 for controls. */
	uint32_t data; /**< Connect/disconnect data, otherwise 0. */
	flecs_enet_result_t result; /**< OK or error; receive/connect are OK. */
} flecs_enet_event_t;

/** Result on completed request AND distinct retained result-event entity. */
typedef struct flecs_enet_operation_result {
	flecs_enet_result_t code; /**< Execution/acceptance result. */
	flecs_enet_operation_t operation; /**< Original operation. */
	ecs_entity_t peer; /**< CONNECT created peer; otherwise request peer. */
	uint32_t attempted; /**< SEND=1; BROADCAST connected eligible count. */
	uint32_t accepted; /**< Accepted packets; 0 for controls. */
} flecs_enet_operation_result_t;

/** World-local IDs; valid only until world fini. No global ECS macros. */
typedef struct flecs_enet_ids {
	ecs_entity_t module; /**< flecs_enet namespace. */
	ecs_entity_t host_config; /**< HostConfig: flecs_enet_host_config_t. */
	ecs_entity_t connection_config; /**< ConnectionConfig declaration. */
	ecs_entity_t peer_config; /**< PeerConfig defaults/override. */
	ecs_entity_t start; /**< Start: one-shot startup tag. */
	ecs_entity_t start_status; /**< StartStatus progress/result. */
	ecs_entity_t host_status; /**< HostStatus: flecs_enet_host_status_t. */
	ecs_entity_t peer_status; /**< PeerStatus: flecs_enet_peer_status_t. */
	ecs_entity_t host; /**< Exclusive Host; target Delete. */
	ecs_entity_t request; /**< Request: flecs_enet_request_t. */
	ecs_entity_t result; /**< Result: flecs_enet_operation_result_t. */
	ecs_entity_t event; /**< Event: flecs_enet_event_t. */
	ecs_entity_t packet; /**< Packet: flecs_enet_packet_t. */
	ecs_entity_t receive_phase; /**< Receive; before application update. */
	ecs_entity_t send_phase; /**< Send; after application post-update. */
} flecs_enet_ids_t;

/** Import idempotently into a real writable world; required world/out_ids.
 * Acquires ENet once per new world, without socket access. Repeat import
 * acquires nothing. Returns OK, ARGUMENT, SCHEMA, NOMEM or TRANSPORT.
 * On failure out_ids is zero. Caller must reserve the flecs_enet namespace.
 * Initialization failure leaves registration unchanged. Installs reflection
 * and Receive/Send tasks; world cleanup balances runtime initialization.
 */
flecs_enet_result_t flecs_enet_import(ecs_world_t *world,
	flecs_enet_ids_t *out_ids);

/** Create one host-child Request using immutable metadata and copied bytes.
 * Requires imported world/main stage, live HostConfig entity, desc/output.
 * data may be NULL only with size==0. SEND/BROADCAST accept empty packets;
 * all other operations require data==NULL,size==0. Copies before deferring.
 * OK means admitted, not executed. out_request becomes a live/reserved ID.
 * Errors: ARGUMENT/NOT_FOUND/STATE/LIMIT/BACKPRESSURE/NOMEM; zero output,
 * no child, accounting change or borrowed bytes retained. State/channel
 * conditions requiring current native state are checked again at execution.
 * Pending requests survive pressure. Deleting one cancels without a result.
 */
flecs_enet_result_t flecs_enet_submit(ecs_world_t *world,
	ecs_entity_t host, const flecs_enet_request_t *desc,
	const void *data, size_t size, ecs_entity_t *out_request);

/** Submit CLOSE with the same admission/output/thread rules as submit. */
flecs_enet_result_t flecs_enet_close(ecs_world_t *world, ecs_entity_t host,
	uint64_t correlation, ecs_entity_t *out_request);
/** Submit FLUSH with the same admission/output/thread rules as submit. */
flecs_enet_result_t flecs_enet_flush(ecs_world_t *world, ecs_entity_t host,
	uint64_t correlation, ecs_entity_t *out_request);

/** Submit SEND or BROADCAST with immediate owned byte copy; same admission
 * contract as submit. SEND requires a live peer; BROADCAST uses peer=0.
 * Acceptance is not a remote receipt or application acknowledgement.
 */
flecs_enet_result_t flecs_enet_send(ecs_world_t *world, ecs_entity_t host,
	ecs_entity_t peer, uint8_t channel, flecs_enet_delivery_t delivery,
	const void *data, size_t size, uint64_t correlation,
	ecs_entity_t *out_request);
/** Submit BROADCAST; copies bytes with the same admission rules as send. */
flecs_enet_result_t flecs_enet_broadcast(ecs_world_t *world,
	ecs_entity_t host, uint8_t channel, flecs_enet_delivery_t delivery,
	const void *data, size_t size, uint64_t correlation,
	ecs_entity_t *out_request);

/** Submit DISCONNECT/DISCONNECT_NOW/DISCONNECT_LATER/RESET only.
 * mode outside that set is ARGUMENT. RESET requires data=0. Disconnect
 * completion arrives as a retained event; peer entities remain alive.
 */
flecs_enet_result_t flecs_enet_disconnect(ecs_world_t *world,
	ecs_entity_t host, ecs_entity_t peer, flecs_enet_operation_t mode,
	uint32_t data, uint64_t correlation, ecs_entity_t *out_request);

/** Delete one live retained event belonging to host; idempotence not implied.
 * Requires writable world/main stage, valid same-host event. Returns OK,
 * ARGUMENT/NOT_FOUND/STATE; failures leave storage intact. Deferral releases
 * bytes/capacity at merge. ecs_delete(event) has the same release semantics.
 */
flecs_enet_result_t flecs_enet_consume(ecs_world_t *world,
	ecs_entity_t host, ecs_entity_t event);

#ifdef __cplusplus
}
#endif
#endif
