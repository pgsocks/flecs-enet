Feature: ECS transport requests and retained events
	Background:
		Given world 0 imports the module

	Scenario: Requests are admitted before execution
		Given host 0 has default configuration
		When I submit "open" on host 0 with correlation 41
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "OK"
		And host 0 is in state 1

	Scenario: Worlds isolate transport metadata
		Given host 0 has default configuration
		And world 1 imports the module
		And host 0 has default configuration
		Then no event exists on host 0

	Scenario: Deferred submissions own bytes before the merge
		Given host 0 has default configuration
		When I begin deferring ECS commands
		And I broadcast "616263" on host 0, channel 0, as "reliable"
		Then admission returns "OK"
		When I merge deferred ECS commands
		Then the last request retains bytes "616263"

	Scenario: Connecting differs from connected
		Given host 0 is open with 0 connected peers
		When I connect host 0 to an endpoint with 2 channels
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "OK"
		And peer 0 on host 0 is in state 0
		When ENet reports a connection for native peer 0
		And I progress the world
		Then peer 0 on host 0 is in state 1

	Scenario Outline: Packet modes and channels
		Given host 0 is open with 1 connected peers
		When host 0 sends "616263" to peer 0 on 1 as "<mode>"
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "OK"
		And the boundary has accepted 1 send calls on channel 1
		Examples:
			| mode                |
			| reliable            |
			| unreliable          |
			| unsequenced         |
			| unreliable_fragment |

	Scenario: Broadcast records partial acceptance without replay
		Given host 0 is open with 3 connected peers
		When transport send call 2 fails
		And I broadcast "616263" on host 0, channel 1, as "reliable"
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "PARTIAL"
		And the last request reports 3 attempted and 2 accepted
		When I progress the world
		Then the boundary has accepted 3 send calls on channel 1

	Scenario: Receive events retain bytes and ordering across frames
		Given host 0 is open with 1 connected peers
		When ENet receives "616263" on channel 1
		And ENet receives "646566" on channel 1
		And I progress the world
		Then host 0 retains 2 events
		And events on host 0 have increasing sequences
		When I progress the world
		Then event 0 on host 0 retains bytes "616263"
		When I consume the first event on host 0
		Then event 0 on host 0 retains bytes "646566"

	Scenario: Event pressure pauses dispatch but services protocol
		Given host 0 has limits 8 requests, 1 events and 4 bytes
		When I submit "open" on host 0 with correlation 1
		Then admission returns "OK"
		When I progress the world
		And I consume the first event on host 0
		And ENet reports a connection for native peer 0
		And I progress the world
		And I consume the first event on host 0
		And ENet receives "616263" on channel 1
		And ENet receives "646566" on channel 1
		And I progress the world
		Then host 0 retains 1 events
		And protocol service continues under pressure
		When I consume the first event on host 0
		And I progress the world
		Then event 0 on host 0 retains bytes "646566"

	Scenario: Request admission retries after cancellation
		Given host 0 has limits 1 requests, 8 events and 4 bytes
		When I submit "open" on host 0 with correlation 1
		Then admission returns "OK"
		When I submit "open" on host 0 with correlation 2
		Then admission returns "BACKPRESSURE"
		When I delete the last request
		And I submit "open" on host 0 with correlation 2
		Then admission returns "OK"

	Scenario Outline: Peer controls preserve terminal entities
		Given host 0 is open with 1 connected peers
		When I submit "<operation>" for peer 0 on host 0
		Then admission returns "OK"
		When I progress the world
		Then peer 0 on host 0 is in state <state>
		Examples:
			| operation        | state |
			| disconnect       | 2     |
			| disconnect_later | 2     |
			| disconnect_now   | 3     |
			| reset            | 3     |

	Scenario Outline: Tuning uses the same request model
		Given host 0 is open with 1 connected peers
		When I tune "<operation>" on host 0 with <a>, <b>, <c>
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "OK"
		Examples:
			| operation     | a    | b    | c    |
			| bandwidth     | 1000 | 2000 | 0    |
			| compression   | 1    | 0    | 0    |
			| ping          | 0    | 0    | 0    |
			| ping_interval | 100  | 0    | 0    |
			| timeout       | 4    | 1000 | 5000 |
			| throttle      | 500  | 3    | 4    |

	Scenario: Native send failure has no acceptance
		Given host 0 is open with 1 connected peers
		When the next "send" boundary call fails
		And host 0 sends "616263" to peer 0 on 0 as "reliable"
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "TRANSPORT"
		And the last request reports 1 attempted and 0 accepted

	Scenario: Host service faults do not remove peer records
		Given host 0 is open with 1 connected peers
		When the next "service" boundary call fails
		And I progress the world
		Then host 0 is in state 2
		And peer 0 on host 0 is in state 3

	Scenario: Host deletion releases pending, received and accepted packets
		Given host 0 is open with 1 connected peers
		When ENet receives "616263" on channel 0
		And host 0 sends "646566" to peer 0 on 0 as "reliable"
		Then admission returns "OK"
		When I progress the world
		And I broadcast "676869" on host 0, channel 0, as "reliable"
		Then admission returns "OK"
		When I delete host 0
		Then all native hosts and packets are released

	Scenario: World finalization releases native resources
		Given host 0 is open with 1 connected peers
		When host 0 sends "616263" to peer 0 on 0 as "reliable"
		Then admission returns "OK"
		When I progress the world
		And I finalize the world
		Then all native hosts and packets are released

	Scenario: Deleted peer generations cannot receive sends
		Given host 0 is open with 1 connected peers
		When I delete peer 0 on host 0
		And host 0 sends "616263" to peer 0 on 0 as "reliable"
		Then admission returns "NOT_FOUND"

	Scenario: Outgoing admission retries after native release
		Given host 0 has limits 8 requests, 8 events and 4 bytes
		When I submit "open" on host 0 with correlation 1
		Then admission returns "OK"
		When I progress the world
		And ENet reports a connection for native peer 0
		And I progress the world
		And host 0 sends "616263" to peer 0 on 0 as "reliable"
		Then admission returns "OK"
		When I progress the world
		And host 0 sends "646566" to peer 0 on 0 as "reliable"
		Then admission returns "BACKPRESSURE"
		When I release accepted transport packets
		And host 0 sends "646566" to peer 0 on 0 as "reliable"
		Then admission returns "OK"
		When I progress the world
		Then the boundary has accepted 2 send calls on channel 0

	Scenario: Open failure leaves no native host
		Given host 0 has default configuration
		When the next "host" boundary call fails
		And I submit "open" on host 0 with correlation 1
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "TRANSPORT"
		And all native hosts and packets are released

	Scenario: Compression failure preserves the open host
		Given host 0 is open with 0 connected peers
		When the next "compression" boundary call fails
		And I tune "compression" on host 0 with 1, 0, 0
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "NOMEM"
		And host 0 is in state 1

	Scenario: Closing retains disconnected peer records
		Given host 0 is open with 1 connected peers
		When I submit "close" on host 0 with correlation 55
		Then admission returns "OK"
		When I progress the world
		Then the last request completes with "OK"
		And peer 0 on host 0 is in state 3
		And host 0 is in state 0
