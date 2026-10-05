# Public transport contract

[Header](../include/flecs_enet.h) · [Examples](usage.md) · [Rationale](rationale.md)

These rules describe the implemented transport and script startup contract.

## ECS boundary

`flecs_enet_import(world,&ids)` returns world-local IDs. IDs may have equal
numeric values in different worlds and are never interchangeable. Repeat
import is idempotent. The caller reserves `flecs_enet`; incompatible existing
component layouts produce `SCHEMA`. Valid import requires a writable real
world, outside deferral and progress. Import acquires ENet once per new world;
repeat import acquires nothing. Initialization failure returns TRANSPORT,
zeros output IDs, and leaves registration unchanged. Context cleanup releases
ENet after all native owners are destroyed. Independent native ENet users
balance their own initialization calls. Import also checks reflected schemas,
reserved private names and cyclic phase dependencies before registration.

Set `ids.host_config` on any ordinary entity. This does not open a socket.
Add `Start` on the configured host to request one OPEN. The generic factory
is the low-level ECS submission API: every operation uses
the same Request component, host ownership, result path and scheduling.
Raw `ecs_set_id(Request)` is not a supported admission path: it cannot return
a payload allocation or pressure error before Flecs defers the write.

HostConfig, ConnectionConfig and PeerConfig are application-owned. HostStatus,
PeerStatus, StartStatus, Request, Packet, Result and Event are module-owned
and read-only. Peers/requests/events are
`ChildOf(host)` and carry exclusive `(ids.host,host)` associations. ChildOf
and Host target-deletion policies remove their dependents. Do not reparent,
clone, inherit, overwrite or remove managed components. It is valid to add
application tags/data and to delete a managed entity on the owning thread.
Deleting an active peer resets its native association, discards its accepted
queues, and publishes no local disconnect event for that deleted entity.

Submission borrows its descriptor/data only during the call and copies both
before returning, including when the main stage defers ECS commands. An
accepted deferred request ID is reserved and becomes inspectable after merge.
Only main-thread deferral is supported; worker stages are prohibited. All
outputs must be disjoint from inputs. Required pointer outputs are non-NULL,
and failures zero them. A failed admission creates no request, retains no
input bytes, and changes no accounting or native state.

## Script startup

Import registers complete configuration/helper/status reflection using C
member offsets and integer widths, including four-byte IPv4 arrays and size_t
byte limits. `using flecs_enet` resolves names only after native import. Named
compression values are `None` and `RangeCoder`.

| Interface | Contract |
| --- | --- |
| HostConfig | Host address, capacity, bandwidth, compression and limits. Zero is valid. |
| ConnectionConfig | Address, channels, protocol data and correlation; direct ChildOf(configured host). |
| PeerConfig | Optional host defaults or whole connection override. |
| Start | One explicit host OPEN or outbound CONNECT attempt. |
| StartStatus | Module-written state, code, admitted request ID and resulting peer ID. |

Startup admission precedes existing Receive work. Hosts are admitted before
connections; a connection waits for its host to be open and can start on a
subsequent progress call. Waiting/OK means the configured parent is closed;
Waiting/BACKPRESSURE means admission capacity is full. Both retain Start.
A missing/wrong parent produces Finished/NOT_FOUND. A faulted/closing host or
an already active transport produces Finished/STATE. Other permanent admission
errors also finish without a request or result event.

Successful admission consumes Start and publishes Pending/OK. Duplicate Start
while Pending is removed and ignored. Admitted work follows existing FIFO,
pressure, Result and retained-event rules. Completion publishes Finished and
the execution code/peer ID. Deleting a pending request cancels it and publishes
Finished/STATE without a result event. IDs in StartStatus may later be dead.
Finished/OK for CONNECT means slot allocation, not handshake completion.
Failure or disconnection needs another explicit Start; active transports must
first be closed or disconnected. Configuration never implicitly restarts work.

HostConfig and host default PeerConfig snapshot at OPEN execution. Connection
settings and selected tuning snapshot at admission. A connection PeerConfig
replaces host defaults as a whole. Zero peer fields/groups retain ENet defaults.
A nonzero timeout group requires all three fields nonzero and minimum <=
maximum. A throttle group requires a nonzero interval and acceleration and
deceleration in 0..32. Invalid peer tuning is rejected before native effects.
Inbound peers use the host default snapshot; outbound peers use their selected
snapshot. Keep configuration unchanged while startup is Pending.

Private target-deletion relationships tie startup requests and generated peers
to their connection declaration. Deleting the declaration cancels pending work
and releases every generated peer, including terminal peers. Result events
remain retained children of the host. Host/world cascade cleanup is preserved.
Generic OPEN/CONNECT remain available through `flecs_enet_submit`; the removed
`flecs_enet_open` and `flecs_enet_connect` helpers have no replacement functions.

## Configuration and ranges

Zero-initialized configuration means outbound-only, 32 peers, 2 channels,
unlimited upstream bandwidth and no compression. `listen=true` binds the
numeric IPv4 address; all-zero octets mean ANY and port zero is ephemeral.
Connect uses a nonzero address and port and rejects 255.255.255.255. No DNS
work occurs in progress. Address octets are wire-order bytes; ports are host
order. The native conversion is private.

| Field | Zero/default | Explicit range |
| --- | --- | --- |
| peer_limit | 32 | 1..4095 |
| channel_limit | 2 | 1..255 |
| incoming/outgoing bandwidth | Unlimited ENet bandwidth | uint32_t bytes/s |
| max_requests | 1024 pending | 1..UINT32_MAX |
| max_events | 1024 retained including results | 1..UINT32_MAX |
| max_outgoing_packets | 8192 native packets | 1..UINT32_MAX |
| events_per_progress | 64 per host per Receive | 1..UINT32_MAX |
| max_event_bytes | 32 MiB | 1..SIZE_MAX |
| max_outgoing_bytes | 32 MiB | 1..SIZE_MAX |
| max_packet_bytes | 1 MiB | 1..32 MiB |
| max_waiting_bytes | 32 MiB per ENet peer | 1..SIZE_MAX |

Normalize zeros, then require max_packet_bytes no greater than each of the
three other byte limits. These finite limits bound payload storage, not all
ENet allocator overhead. Native waiting limits apply per allocated peer,
so total potential waiting storage scales with peer_limit. A privately held
already-dispatched event can add at most max_packet_bytes beyond retained
event bytes. Completed request metadata and disconnected peers remain until
application deletion; applications must clean up these histories.

Config is read at OPEN execution. Closed-host admission normalizes the current
HostConfig limits to reserve pending storage; an open host uses its effective
HostStatus.active.limits. Changing limits while requests are pending
is prohibited. Config can be edited while open with no pending requests, but
changes apply only on reopen. Host tuning requests change effective `active`
status, not application HostConfig. Reopen starts from HostConfig again.

## Operations and results

Only documented fields for an operation are nonzero; enum values outside
the declared sets are `ARGUMENT`. correlation is always permitted and copied
verbatim. data/size passed to submit are required to be NULL/zero except for
SEND/BROADCAST. Their zero-length packets are valid; size>0 requires readable
data and cannot exceed max_packet_bytes. Peer inputs must be live,
generation-correct, same-world and associated with the specified host;
missing/deleted/wrong-host peers are `NOT_FOUND` at admission or execution.

| Operation | Required state / fields | Execution outcome and failure effects |
| --- | --- | --- |
| OPEN | CLOSED; HostConfig | Normalize config, allocate host, configure waiting/packet bounds and compression. OK opens it; bad config ARGUMENT, create failure TRANSPORT, compressor failure NOMEM. No partial native owner survives failure; host stays CLOSED. |
| CLOSE | Any host state | Idempotent when CLOSED. Cancel pending requests with STATE results, release native queues/host, leave peer/event entities inspectable and peers DISCONNECTED. No drain or receipt promise. |
| CONNECT | OPEN; address, channels, data | channels=0 means active limit, otherwise 1..limit. OK creates CONNECTING peer and puts its ID in Result; slot/connection failure TRANSPORT creates no peer. Eventual CONNECT is distinct. |
| SEND | OPEN; CONNECTED peer, channel, delivery | Channel is less than negotiated channel_count. OK means ENet accepted one packet; native send failure TRANSPORT or packet allocation failure NOMEM accepts zero. Peer remains connected. |
| BROADCAST | OPEN; peer=0, channel, delivery | Channel below active host limit; snapshot all CONNECTED peers in entity-ID order. Peers with too few negotiated channels fail individually. All accepted OK, subset PARTIAL, none accepted TRANSPORT; empty recipient set OK with zero attempts. No atomicity or automatic replay. |
| FLUSH | OPEN | Push native queues, no wait or receipt guarantee; accepted count zero. |
| DISCONNECT | OPEN; CONNECTED peer, data | Graceful protocol close; drops queues as upstream specifies, transitions DISCONNECTING. Eventual DISCONNECT completes peer state. |
| DISCONNECT_LATER | OPEN; CONNECTED peer, data | Drain already accepted ENet packets then graceful close; DISCONNECTING. Pending module SENDs do not become part of the drain. |
| DISCONNECT_NOW | OPEN; nonterminal peer, data | Immediate reset/discard, synthesize exactly one local DISCONNECT; remote notification not guaranteed. |
| RESET | OPEN; nonterminal peer, data=0 | Local immediate reset/discard, synthesize exactly one local DISCONNECT; remote may time out. |
| BANDWIDTH | OPEN; incoming/outgoing_bandwidth | Replace effective host limits; zero unlimited. |
| COMPRESSION | OPEN; compression | NONE or RANGE_CODER; allocation failure NOMEM preserves old compression. |
| PING | OPEN; CONNECTED peer | Request one protocol ping. |
| PING_INTERVAL | OPEN; CONNECTED peer, interval_ms | 1..UINT32_MAX milliseconds. |
| TIMEOUT | OPEN; CONNECTED peer, timeout_limit/minimum/maximum | limit>0 and 0<minimum<=maximum, all uint32_t. |
| THROTTLE | OPEN; CONNECTED peer, interval_ms/acceleration/deceleration | interval>0 ms; acceleration/deceleration 0..32. |

State conflicts are `STATE`; validation failures have no effect. A native
host service failure is sticky `TRANSPORT`, releases its native host and
associations, sets HostStatus FAULTED, and retains peers as DISCONNECTED.
Diagnostics for affected peers are published as DISCONNECT events with
TRANSPORT and are retained as pending work under event pressure. CLOSE then
OPEN recovers; no automatic reconnect occurs. Healthy hosts keep progressing.

Admitted requests acquire `Result` only after execution and result-event
reservation. Result is also copied to a distinct retained Event entity;
deleting the request does not delete its completed result event. Result peer
is the new peer for CONNECT, the request peer for peer operations, otherwise
zero. attempted/accepted are zero for controls, 1/0-or-1 for SEND and aggregate
recipient counts for BROADCAST. The Event's request ID may be dead later.
Result events carry the request's send/broadcast channel, otherwise zero.
OK admission, OK control execution, ENet send acceptance, remote receipt and
remote application processing are separate milestones. There is no transport
delivery acknowledgement API; applications can send their own messages.

## Ordering and pressure

Default ordering is OnLoad -> Receive -> PreUpdate -> OnUpdate -> PostUpdate
-> Send -> PreStore. Import exposes phase IDs; a custom pipeline must retain
these dependencies, merge staged admission before processing, and make
published data visible before application reads. Native calls are on the
world-owning thread in immediate, single-threaded systems; service is always
nonblocking. No ENet worker or global application callback is introduced.

Request tickets increase per host, with FIFO head preservation in each phase.
Lifecycle/tuning precede native dispatch in Receive. SEND/BROADCAST/FLUSH run
in Send. Phase order takes precedence over submission order across phases.
Commands submitted in OnUpdate are available to Send the same frame after
merge and to Receive the next frame. Transport events preserve native dispatch
order; operation results interleave at execution. No cross-host total order
is promised. Native packets can be accepted internally before their public
event is dispatched; PeerStatus describes the module's published state.

Admission reserves pending request count and owned outgoing bytes. At a
temporary bound return BACKPRESSURE without an ID; callers retry unchanged
data later. An oversize packet is LIMIT, not temporary pressure. For admitted
work, insufficient result-event/native-packet capacity leaves the request
pending with no Result. Later work in that phase cannot overtake it.

Every executed request requires a result-event slot before native side effects.
SEND transfers pending-byte accounting to accepted-byte accounting; release
happens only on the ENet packet free callback. BROADCAST reserves possible
per-recipient copies before the first send; an impossible aggregate byte or
packet demand completes LIMIT with zero accepted, and temporary shortage
waits. Partial acceptance is terminal; retry requires an explicit new request.
Accepted packets are never sent again merely to publish a delayed result.

When retained events reach the count limit, stop native dispatch and call
`enet_host_service(host,NULL,0)` to continue protocol work. If a dispatched
receive packet exceeds remaining byte capacity, retain that single native
event privately and retry it before later events. Never silently drop this
pending event under pressure. Explicit peer or declaration deletion releases
its privately buffered native dispatch; retained events remain until consumed
or deleted. Upstream unreliable packets may still drop and native waiting
limits may reject traffic; the module does not promise unlimited reliability
under a consumer that never frees capacity.

CLOSE cancellation also needs result slots. It may span multiple frames under
pressure and blocks other host work until finished, while protocol service
continues. Results of cancellation precede the CLOSE result. Host deletion
instead cancels everything without publishing results and releases immediately
at the applicable ECS merge. Event sequence starts at 1 and survives reopen.
At sequence exhaustion fault with LIMIT and do not wrap or duplicate IDs;
teardown remains possible even if no diagnostic event can receive a sequence.

## Ownership and cleanup

Payload submission always copies bytes before deferral. Request Packet views
are valid only while pending; completion removes them and releases/transfers
their private storage. Receive Packet views remain valid until event deletion,
consumption or host/world destruction. Copying view structs copies borrowing,
never ownership. Do not retain component pointers across structural changes;
re-fetch by entity ID. Bytes themselves survive a table move.

Private components own hosts, associations, pending/received packets and
accounting contexts. Copy/copy-construction must be illegal; move hooks
transfer and clear source, with destructors releasing once. Destruction order
must keep accounting contexts alive through native free callbacks. Native
handles cannot escape publicly. Private hooks enforce unique ownership and
are covered by deterministic movement, cancellation and cleanup tests.

Delete pending requests to cancel without an event. Delete completed requests
to retire metadata. Consume/delete events to free receive bytes and event
slots; deferred deletion frees at merge. Disconnect retains peers; deleting a
host deletes all its children and native resources. `ecs_fini` does the same
for every host in that world, then releases that world's ENet runtime. There
is no custom allocator/checksum/intercept/compressor callback escape API.
