# Testing and validation

[Module](../README.md) · [Contract](api.md) · [Rationale](rationale.md)

Tests assert the working transport and script configuration contract. Existing
behavioral assertions are preserved; removed OPEN/CONNECT convenience callers
now use test-only wrappers around the generic factory. No expected-failure
flags, disabled behavioral cases or weakened expectations are used. The Sync
adapter and contributor rules are unchanged.

## Build and execution

From the parent workspace, use the README commands for an online build. To
reuse the local dependency sources used on 2026-10-05, configure as follows.
The current CPM bootstrap still needs network access for its pinned download:

```sh
cmake -S ENet -B /tmp/flecs-enet-milestone \
	-DCPM_flecs_SOURCE=/tmp/sync-deps/flecs \
	-DCPM_enet_SOURCE=/tmp/enet-1.3.18 \
	-DFLECS_ENET_CHECK_CXX=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/flecs-enet-milestone -j 4
ctest --test-dir /tmp/flecs-enet-milestone --output-on-failure
```

Focused suites use `-L infrastructure`, `-L unit`, `-L cucumber` or `-L udp`.
For deterministic checks without environment-dependent suites:

```sh
ctest --test-dir /tmp/flecs-enet-milestone \
	-LE 'udp|cucumber' --output-on-failure
```

Cucumber remains registered by default. Missing Ruby/Bundler produce failed,
diagnostic tests rather than silent omissions. Consumers can set
`FLECS_ENET_CUCUMBER=OFF`; `BUILD_TESTING=OFF` builds the library and loader
without test dependencies. C++17 inclusion checks are optional.

## Deterministic boundary and script tests

The private instrumented ENet boundary uses real Flecs worlds and upstream
packet allocation/destruction, including module packet callbacks. It counts
native hosts/packets and runtime acquisitions/releases, records tuning/sends,
holds accepted packets until release, injects ordered events and fails selected
native calls. Final runtime release asserts native resources are gone.
Production links upstream ENet directly; no dependency source is modified.

The initial 52 deterministic/infrastructure checks passed before changes. A
new script assignment test then failed with missing reflection metadata. After
reflection registration it passed. Startup contract checks subsequently failed
because no startup behavior existed, then passed after implementation.

`tests/script.c` adds twelve contract cases with separate CTest registrations:

| Case | Observable coverage |
| --- | --- |
| runtime | Initialization failure leaves registration unchanged and zeroes every ID; repeat import acquires nothing; two live worlds balance cleanup and the survivor continues native operations. |
| workflow | Config without Start, host-before-child waiting, one-shot execution, retained results, active duplicate STATE and declaration cascade cleanup. |
| examples | All five real example files via ecs_script_run_file in two worlds; nested addresses, named compression, omitted defaults, multiple connections and limits. |
| pressure | Request admission pressure retains Start; event pressure holds Pending; duplicate Start is ignored; execution occurs after event consumption. |
| deletion | Deleting an event-pressure-blocked declaration cancels its admitted request and frees accounting. |
| host_restart | Native OPEN failure stays terminal until explicit retry; active Start fails STATE; CLOSE and Start apply edited settings. |
| connection_restart | Native CONNECT failure needs retry; disconnected peers remain terminal; explicit restart creates fresh peers; declaration deletion releases all generated history. |
| tuning | Host default snapshot remains in use after edits; connection override replaces the whole default group; inbound tuning and ENet defaults are preserved. |
| invalid | Invalid timeout/throttle groups fail before native effects; wrong parent and invalid connect address fail terminally. |
| cancellation | Deleting a pending startup request publishes Finished/STATE; explicit restart works; host deletion cascades. |
| schema | Incompatible reflection is rejected without initialization or partial registration. |
| byte_width | On platforms with 64-bit size_t, script byte limits above UINT32_MAX survive assignment and OPEN normalization. |

Existing cases retain packet modes, ownership, table movement, stale identity,
FIFO/phase behavior, event order, tuning, native failures and pressure checks.
Instrumentation does not inject every Flecs/OS allocator failure.

## Procedural features and UDP

Cucumber uses one persistent C fixture process per scenario. Ruby steps perform
individual commands and inspect actual ECS components. `features/script.feature`
loads scripts, progresses, inspects waiting/completion, retries failures and
checks declaration cleanup. It does not map scenarios to whole unit cases.
The new `script_protocol` CTest smoke check executes script commands directly
through the same persistent process, independently of Ruby availability.

UDP checks pair the production module with an independent upstream ENet host,
exchange packets both ways, and test 16 KiB reliable fragmentation on channel
one. There is no previous FlecsEnet release. Deterministic tests do not establish
real retransmission, packet loss or upstream fragmentation behavior.

The pinned [Cucumber 11.1.1 gemspec][cucumber] requires Ruby >=3.2 and RubyGems
>=3.2.8. Bundler 2.6.9 and the test-only gem graph are in `Gemfile.lock`.

```sh
cd ENet
gem install bundler -v 2.6.9
BUNDLE_FROZEN=true bundle _2.6.9_ install
FLECS_ENET_FIXTURE=/tmp/flecs-enet-milestone/tests/flecs_enet_fixture \
	FLECS_ENET_UDP_FIXTURE=/tmp/flecs-enet-milestone/tests/flecs_enet_udp_fixture \
	bundle _2.6.9_ exec cucumber --publish-quiet features
```

CTest verifies tool versions and supplies fixture paths. Setup does not install
gems automatically. Verification used Ruby 3.3.6 and Bundler 2.6.9 installed
under `/tmp`, with `BUNDLE_PATH=/tmp/enet-test-gems`. Frozen installation found
and prompted correction of the missing sys-uname dependency on memoist3 1.0.0;
existing gem versions were preserved. Frozen install/check and Ruby syntax
checks now pass. From the parent workspace, reproduce CTest with this local
tooling as follows:

```sh
cmake -S ENet -B /tmp/flecs-enet-milestone \
	-DFLECS_ENET_RUBY=/tmp/enet-ruby/bin/ruby \
	-DFLECS_ENET_BUNDLE=/tmp/enet-ruby/bin/bundle
PATH=/tmp/enet-ruby/bin:$PATH BUNDLE_PATH=/tmp/enet-test-gems \
	ctest --test-dir /tmp/flecs-enet-milestone --output-on-failure
```

## Executed validation on 2026-10-05

Toolchain: GCC/G++ 14.2.1, CMake/CTest 3.31.4, cached Flecs 4.1.1 and ENet 1.3.18.
The initial build checked the previous CPM 0.43.2 bootstrap hash; the final
retry used the current hash-pinned CPM 0.43.1 bootstrap. Build trees/logs stayed
under `/tmp`.

| Check | Observed result |
| --- | --- |
| Strict Debug C11/C++17 build, compiled examples and runnable loader | Passed. |
| Release build with BUILD_TESTING=OFF | Library and loader passed. |
| Deterministic contract/infrastructure suite | 65/65 passed (60 contract cases and five infrastructure checks). |
| AddressSanitizer + UndefinedBehaviorSanitizer, leak detection disabled | 65/65 passed. |
| AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer, sandbox disabled | Full suite passed: 70/70, including UDP and Cucumber. |
| UDP probe/interoperability/fragmentation, sandbox disabled | All three passed. |
| Cucumber contract/UDP | Both passed with locally installed Ruby/Bundler and the corrected frozen lockfile. |
| Full CTest, sandbox disabled | 70/70 passed. |

The initial sandboxed run blocked UDP sockets and LeakSanitizer process
inspection. Disabling the sandbox removed those restrictions. Missing Ruby
required a separate local tooling installation. No tests were skipped or
marked as expected failures. The retry rebuilt against the workspace's revised
CPM 0.43.1 bootstrap after clearing a stale CPM_DIRECTORY cache entry; repository
bootstrap edits were left unchanged. Benchmarks, lossy-link experiments and
compatibility with a previous module release were not run.

[cucumber]: https://github.com/cucumber/cucumber-ruby/blob/v11.1.1/cucumber.gemspec
