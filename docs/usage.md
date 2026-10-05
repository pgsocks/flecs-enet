# Usage guide

[API rules](api.md) · [C examples](../examples/workflows.c) ·
[Runnable loader](../examples/loader.c)

## Import, load and progress

All calls and ECS access run on the world-owning thread. Import registers
reflection, components, tags, phases and transport tasks, and acquires ENet.
Scripts require that native import before `using flecs_enet` resolves names.

```c
ecs_world_t *world = ecs_init();
flecs_enet_ids_t ids;
flecs_enet_result_t rc = flecs_enet_import(world, &ids);
if (rc != FLECS_ENET_OK) {
	ecs_fini(world);
	return 1;
}
if (ecs_script_run_file(world, "ENet/examples/scripts/client.flecs")) {
	ecs_fini(world);
	return 1;
}
ecs_progress(world, 0.01f);
ecs_progress(world, 0.01f);
/* Inspect status/events and keep progressing as needed. */
ecs_fini(world);
```

The [loader](../examples/loader.c) accepts a script path and optional frame
count. It processes retained events and deletes completed request metadata.
It needs no application `enet_initialize` or `enet_deinitialize` calls.

## Script declarations

[listener.flecs](../examples/scripts/listener.flecs) binds loopback port 9050.
[client.flecs](../examples/scripts/client.flecs) starts an outbound-only host
and one child connection. Place every ConnectionConfig directly beneath its
HostConfig entity. Multiple children work independently, as shown in
[multiple.flecs](../examples/scripts/multiple.flecs).

```flecs
using flecs_enet
client {
	HostConfig: {}
	Start
	server {
		ConnectionConfig: {
			address: {ip: [127, 0, 0, 1], port: 9050},
			channels: 2, data: 7, correlation: 17
		}
		Start
	}
}
```

Host startup is admitted first at the beginning of Receive. A child waits
until its host is open, so an initially closed host and child start across
successive progress calls. Config without Start is inert. Host startup creates
no peer; successful connection startup creates a CONNECTING peer. Wait for its
retained CONNECT event before sending.

## Startup status, retry and restart

Read `ids.start_status` on the host or connection declaration. Its request and
peer IDs are snapshots and may refer to deleted entities.

| State | Meaning | Start tag |
| --- | --- | --- |
| Waiting | Host prerequisite is not open (`OK`), or admission capacity is full (`BACKPRESSURE`). | Retained for automatic admission retry. |
| Pending | Admitted request awaits execution or result-event capacity. | Consumed; duplicate tags are ignored and removed. |
| Finished | Local execution completed, or admission failed permanently. | Consumed; another explicit Start is required. |

An admitted startup also publishes Result on its request and a distinct
retained result event. Finished/OK on a connection means a native slot was
allocated, not that the handshake completed. Disconnection never automatically
restarts that declaration. To retry a failed attempt, add `ids.start` again.
To restart a successful attempt, first CLOSE its host or disconnect/reset its
peer, then add Start. Starting an active host or active generated peer reports
STATE. Retained terminal peers remain inspectable until deletion.

Keep configuration unchanged while startup is Pending. HostConfig and host
PeerConfig defaults snapshot at OPEN execution; connection configuration and
its selected peer tuning snapshot at admission. Edits while open affect the
next startup. Deleting a connection declaration cancels its pending request
and releases all peers it generated, including terminal history. Deleting the
host cascades through its declarations, requests, peers and events.

## Tuning and limits

[tuning.flecs](../examples/scripts/tuning.flecs) sets bandwidth and compression
in HostConfig and peer defaults in PeerConfig. Configure compression
consistently at both endpoints. A connection's PeerConfig replaces the whole
host default configuration. Zero settings/groups retain ENet defaults; an
explicit empty PeerConfig suppresses all host tuning defaults. Timeout groups
must specify nonzero limit/minimum/maximum with minimum <= maximum. Throttle
uses a nonzero interval and acceleration/deceleration in 0..32. Invalid tuning
fails before host creation or connection native side effects.

[limits.flecs](../examples/scripts/limits.flecs) demonstrates count and byte
limits. max_packet_bytes must fit every other byte limit. Consume retained
events to restore event capacity. Accepted native packets release outgoing
capacity separately; FLUSH is not an acknowledgement and need not free it.
Runtime BANDWIDTH, COMPRESSION, PING, PING_INTERVAL, TIMEOUT and THROTTLE remain
available through the generic request factory. Runtime host tuning changes
HostStatus.active; the next OPEN uses HostConfig again.

## Runtime packets and retained events

The [compiled runtime examples](../examples/workflows.c) send reliable control,
unreliable state and broadcasts. Retain request IDs/correlations or consume
their result events. BROADCAST can return PARTIAL with attempted/accepted
counts; ENet acceptance is not a remote application acknowledgement.

Process events by per-host sequence, not ECS table order. Process/copy receive
bytes before consumption. Receive bytes survive until their event is deleted;
component pointers must be re-fetched after structural changes. Finish query
iteration before deleting entities, or use main-thread deferral. Delete
completed requests separately to retire their metadata. Startup status survives
that deletion, with the now-dead request ID retained as history.

If packet admission returns BACKPRESSURE with no request ID, keep the message
and retry later. Once admitted, keep the request; the module waits for capacity.
Do not replay accepted sends or whole partial broadcasts. Event pressure pauses
dispatch while ENet's nonblocking protocol service continues.

## Scheduling and cleanup

Receive/startup/lifecycle/tuning run before EcsOnUpdate; Send runs after
EcsPostUpdate. Custom pipelines must include both exported phases in that
order, with merges before consumers. Requests submitted during update are
eligible for Send this frame and Receive next frame.

CLOSE cancels outstanding requests with STATE results and can wait for event
capacity. DISCONNECT_LATER drains native accepted packets; module-pending
requests are separate. RESET/DISCONNECT_NOW discard native queues. Host
deletion provides prompt cleanup without cancellation events. `ecs_fini`
releases every native owner, then balances the module's ENet initialization.
Independent native ENet users must balance their own initialization calls.
