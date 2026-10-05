# ENet API rationale

[API](api.md) · [Examples](usage.md) ·
[Wrapper study](../../knowledge-base/research/flecs-library-wrappers.md)

Decision date: **2026-10-05**. These choices describe the working module.

## Feature boundary and ECS vocabulary

The module owns ENet transport: hosts, peers, packet operations, tuning,
service scheduling, pressure and lifetimes. Replication and serialization
belong elsewhere. The existing EnetSync adapter continues to deliver its
replication frames unchanged.

Flecs' [design guide][design] recommends feature-oriented modules and separate
component contracts from their system implementations. We follow that
separation inside one transport module: public POD components/relationships
describe application data; native owners and processing systems remain
private. Separate installable component and backend modules were rejected
for now because there is only one ENet backend. The public contract remains
independent of private system names and ENet struct layouts.

## Request factory and retained events

Applications import, load script configuration, progress and query events.
Flecs requires [reflection][script] to assign component values from scripts.
Explicit C offsets and widths preserve the existing native configuration
layouts, including IPv4 arrays and byte limits. A one-shot Start tag separates
configuration from native effects. Waiting retains intent under prerequisites
or admission pressure; failures and disconnects need explicit retry. This
avoids surprising reconnects or restarts when configuration is edited.
Start admission composes the existing request factory and retained result
path, preserving FIFO, ownership and pressure accounting. A generic descriptor
covers all operations; the remaining runtime convenience functions call the
same factory. Raw borrowed payload pointer components
were rejected: staged writes can outlive caller storage and a component copy
hook cannot return admission pressure or allocation failure to the caller.
Synchronous factory copying makes that failure boundary explicit.

[Sokol's application path][sokol] translates native input into shared ECS
state but also selects a global application run callback. Our inference is
that the translation idea fits transport, while that global callback model
does not fit independently configured hosts in multiple worlds. ENet events
are retained entities instead of mutable singleton snapshots or callback-only
notifications: receive payloads and every transition remain available until
the consumer deletes them. The tradeoff is explicit consumer cleanup.

Request results are separate from transport events and from remote receipt.
CONNECT success means a native slot was allocated, not a completed handshake.
SEND success means upstream acceptance. BROADCAST uses per-peer sends to
report partial acceptance because upstream broadcast returns no aggregate
acceptance result. An all-or-nothing broadcast promise was rejected; ENet
does not supply such a primitive.

## Ownership, state and scheduling

World-local IDs follow Sync's convention and avoid process-global component
variables. Numeric IPv4 addresses avoid blocking DNS work in progress.
Each newly imported world acquires ENet once and releases it from module
context cleanup after native owners. Repeat import acquires nothing. Independent
native ENet users still acquire and release their own runtime lifetime. No
process-global ECS IDs or assumptions about external native resource ownership
are needed. Initialization precedes registration so failure is transactional.

Host peer defaults snapshot at OPEN, while each connection can replace them
as a whole at admission. Zero tuning retains upstream defaults; grouped range
validation precedes native effects. Generated peers and startup requests carry
private deletion relationships to their declaration, preserving host ownership
while making deletion cancel and release all declaration-owned work.

Native handles remain private and uniquely owned. Shallow-copyable host
pointers, shared/external host borrowing and native-handle escape functions
were rejected because they bypass accounting and exact cleanup obligations.
Private move/destruction hooks enforce unique ownership and exact cleanup.
Disconnected peers remain ECS records; a later connection gets a fresh peer
entity, avoiding confusion when ENet reuses native slots.

Receive before update and send after post-update let application systems
react to transport input and submit output in one frame. Custom phase IDs
permit pipeline integration; hidden threads and manual polling as the only
workflow were rejected. Explicit FIFO rules inside phases acknowledge that
a lifecycle request can supersede earlier outgoing work in another phase.

Finite limits preserve pending module work; pressure pauses dispatch while
upstream protocol service continues. The inspected 1.3.18 `protocol.c` accepts
a NULL event pointer. Admission reserves copied payloads, execution reserves
result slots, and callbacks retire accepted packets. Counting bytes only at
submission or retrying entire partial sends would misrepresent ownership.
Upstream waiting limits remain separate; no unlimited buffering or guaranteed
remote delivery is promised.

## Prior art limits and test tooling

The [study](../../knowledge-base/research/flecs-library-wrappers.md) distinguishes
documented design arguments from code observations. SDL2 implementation and
Sokol renderer/ownership-hook access remain gaps. They do not establish tested
multi-world isolation, ENet pressure semantics or current Flecs compatibility.
Sokol's own README identifies its renderer as demonstration/example code.

The module uses Flecs 4.1.1, upstream ENet 1.3.18 and CPM 0.43.1.
Cucumber-Ruby 11.1.1 was selected explicitly in the supplied plan; its
[gemspec][cucumber] requires Ruby >=3.2 and RubyGems >=3.2.8. Gems are test-only
and locked independently of CPM-managed C dependencies. The procedural stdin/
stdout fixture preserves a real world for each scenario and allows each step
to perform one action or inspection. Mapping feature names to whole C test
cases was rejected because it would not execute the stated steps.

Deterministic contract tests and actual script-file loading cover the implemented
workflow. The procedural fixture executes each Gherkin step as an individual
action or inspection. UDP and Cucumber checks passed after disabling the
sandbox and installing the pinned Ruby tooling locally. The full normal and
sanitizer suites each passed 70/70 checks. No previous module release exists;
real interoperability checks pair the production module with upstream ENet.

[design]: https://www.flecs.dev/flecs/DesignWithFlecs.html
[sokol]: https://raw.githubusercontent.com/flecs-hub/flecs-systems-sokol/master/src/main.c
[cucumber]: https://github.com/cucumber/cucumber-ruby/blob/v11.1.1/cucumber.gemspec

[script]: https://www.flecs.dev/flecs/FlecsScript.html#components
