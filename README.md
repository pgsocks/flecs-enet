# Flecs ENet

An ECS-first transport for Flecs 4.1.1 and upstream ENet 1.3.18. Applications
create a world, import the module, load a Flecs script, and progress the world.
The C11 interface supports C++17 inclusion. Native ENet handles stay private.

```flecs
using flecs_enet

server {
	HostConfig: {
		listen: true, address: {ip: [127, 0, 0, 1], port: 9050}
	}
	Start
}
```

`using flecs_enet` resolves the world-local names registered by native import;
it does not import or initialize the native module itself. Configuration alone
creates no transport. `Start` requests one attempt; `StartStatus` reports
Waiting, Pending or Finished. Retry and restart require another explicit Start.
Import initializes ENet once per world; world cleanup releases it after native
owners. Applications need no ENet initialization calls for module-only use.
Independent ENet users retain their own balanced initialization lifetime.

Read the [public header](include/flecs_enet.h), [API contract](docs/api.md),
[usage guide](docs/usage.md), [rationale](docs/rationale.md), and
[testing record](docs/testing.md). The [script examples](examples/scripts) cover
listeners, clients, multiple connections, peer tuning and finite limits. The
[runnable loader](examples/loader.c) checks script errors, progresses the world,
inspects startup/events and finalizes it.

```sh
cmake -S ENet -B /tmp/flecs-enet-build \
	-DFLECS_ENET_CHECK_CXX=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/flecs-enet-build -j 4
/tmp/flecs-enet-build/flecs_enet_loader \
	ENet/examples/scripts/listener.flecs 120
ctest --test-dir /tmp/flecs-enet-build --output-on-failure
```

The library target is `FlecsEnet::flecs_enet`. CPM 0.43.1 is hash-pinned;
Flecs/ENet are tag-pinned. Local dependency sources can be supplied with
`CPM_flecs_SOURCE` and `CPM_enet_SOURCE`. The CPM bootstrap still downloads
its pinned release during configuration, so those overrides do not make the
build fully offline. Local sources must be the specified versions; CPM source
overrides do not verify an arbitrary checkout's version.
`BUILD_TESTING=OFF` builds the library and loader without test dependencies.
Cucumber requires Ruby >=3.2, RubyGems >=3.2.8, Bundler and the locked gems.

Runtime packets, lifecycle operations and tuning use immutable requests and
retained events. Finite limits report pressure without silently dropping module
work. Consume events and delete completed requests to retire storage/history.
Replication, codecs, authentication, encryption, automatic reconnect, custom
ENet callbacks and public native-handle escape APIs are outside this module.
The separate Sync adapter is unchanged.
