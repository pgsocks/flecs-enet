@udp
Feature: Real ENet UDP delivery between ECS hosts
	Background:
		Given world 0 imports the module
		And host 0 listens on loopback
		And host 1 has default configuration
		When I submit "open" on host 0 with correlation 1
		Then admission returns "OK"
		When I submit "open" on host 1 with correlation 2
		Then admission returns "OK"
		When I progress the world
		And I connect host 1 to listening host 0
		Then admission returns "OK"
		When I progress until host 1 has a connected peer

	Scenario: Reliable packets reach the listening host
		When host 1 sends "616263" to peer 0 on 1 as "reliable"
		Then admission returns "OK"
		And host 0 receives bytes "616263" within the UDP deadline

	Scenario: Fragmented packets are reassembled by upstream ENet
		When I send a fragmented message from host 1
		Then admission returns "OK"
		And host 0 receives the fragmented message before timeout
