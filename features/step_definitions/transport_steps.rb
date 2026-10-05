Given('world {int} imports the module') do |world|
	@last = rpc("world #{world}")
	expect_value(@last.fetch('imported'), '1', 'world import')
end

When('I send a fragmented message from host {int}') do |host|
	@fragment = (0...16_384).map { |i| format('%02x', i % 251) }.join
	submit(host, 'send', channel: 1, bytes: @fragment)
end

Then(
	'host {int} receives the fragmented message before timeout'
) do |host|
	step "host #{host} receives bytes \"#{@fragment}\" " \
		'within the UDP deadline'
end

Given('host {int} has default configuration') do |host|
	rpc("host #{host} 0 0 0 0 0 0 0 0")
end

Given('host {int} listens on loopback') do |host|
	rpc("host #{host} 1 4 2 0 0 0 0 0")
end

Given(
	'host {int} has limits {int} requests, {int} events and {int} bytes'
) do |host, requests, events, bytes|
	rpc("host #{host} 0 4 2 #{requests} #{events} #{bytes} " \
		"#{bytes} #{bytes}")
end

Given('host {int} is open with {int} connected peers') do |host, peers|
	rpc("host #{host} 0 4 2 0 0 0 0 0")
	submit(host, 'open')
	expect_value(@last.fetch('code'), '0', 'FIXTURE OPEN prerequisite')
	rpc('progress')
	peers.times { |peer| rpc("inject #{peer} 1 0 0 -") }
	rpc('progress')
	# Consume setup events so pressure scenarios count their own work.
	loop do
		event = rpc("query event #{host} 0")
		break if event.fetch('exists') == '0'
		rpc("delete event #{host} 0")
	end
end

When(
	'I submit {string} on host {int} with correlation {int}'
) do |operation, host, correlation|
	submit(host, operation, correlation: correlation)
end

When(
	'I connect host {int} to an endpoint with {int} channels'
) do |host, channels|
	submit(host, 'connect', a: channels, correlation: 17)
end

When('I connect host {int} to listening host {int}') do |source, target|
	@last = rpc("connect #{source} #{target}")
	if @last.fetch('code') == '0'
		@request_slot = @request_count
		@request_count += 1
	end
end

When(
	'host {int} sends {string} to peer {int} on {int} as {string}'
) do |host, bytes, peer, channel, mode|
	submit(host, 'send', peer: peer, channel: channel, mode: mode,
		bytes: bytes, correlation: 23)
end

When(
	'I broadcast {string} on host {int}, channel {int}, as {string}'
) do |bytes, host, channel, mode|
	submit(host, 'broadcast', channel: channel, mode: mode, bytes: bytes)
end

When('I submit {string} for peer {int} on host {int}') do |op, peer, host|
	submit(host, op, peer: peer)
end

When(
	'I tune {string} on host {int} with {int}, {int}, {int}'
) do |op, host, a, b, c|
	submit(host, op, a: a, b: b, c: c)
end

When('I begin deferring ECS commands') { rpc('defer begin') }
When('I merge deferred ECS commands') { rpc('defer end') }
When('I progress the world') { rpc('progress') }
When('I release accepted transport packets') { rpc('release') }
When('the next {string} boundary call fails') do |kind|
	rpc("fail #{kind} 1")
end
When('transport send call {int} fails') { |n| rpc("fail send #{n}") }

When('ENet reports a connection for native peer {int}') do |peer|
	rpc("inject #{peer} 1 0 0 -")
end
When('ENet reports a disconnection for native peer {int}') do |peer|
	rpc("inject #{peer} 2 0 0 -")
end
When('ENet receives {string} on channel {int}') do |bytes, channel|
	rpc("inject 0 3 #{channel} 1 #{bytes}")
end
When('I consume the first event on host {int}') do |host|
	@last = rpc("consume event #{host} 0")
	expect_value(@last.fetch('code'), '0', 'event consumption')
end
When('I delete the last request') do
	rpc("delete request 0 #{@request_slot}")
end
When('I delete peer {int} on host {int}') do |peer, host|
	rpc("delete peer #{host} #{peer}")
end
When('I delete host {int}') { |host| rpc("delete host #{host} 0") }
When('I finalize the world') { rpc('fini') }

Then('admission returns {string}') do |result|
	expect_value(@last.fetch('code'), RESULTS.fetch(result).to_s,
		'admission')
end
Then('the last request completes with {string}') do |result|
	actual = rpc("query request 0 #{@request_slot}")
	expect_value(actual.fetch('complete'), '1', 'request completion')
	expect_value(actual.fetch('code'), RESULTS.fetch(result).to_s, 'result')
end
Then(
	'the last request reports {int} attempted and {int} accepted'
) do |attempted, accepted|
	actual = rpc("query request 0 #{@request_slot}")
	expect_value(actual.fetch('attempted'), attempted.to_s, 'attempted')
	expect_value(actual.fetch('accepted'), accepted.to_s, 'accepted')
end
Then('the last request retains bytes {string}') do |bytes|
	actual = rpc("query request 0 #{@request_slot}")
	expect_value(actual.fetch('bytes'), bytes, 'owned request bytes')
end
Then('host {int} is in state {int}') do |host, state|
	actual = rpc("query host #{host} 0")
	expect_value(actual.fetch('state'), state.to_s, 'host state')
end
Then('peer {int} on host {int} is in state {int}') do |peer, host, state|
	actual = rpc("query peer #{host} #{peer}")
	expect_value(actual.fetch('exists'), '1', 'retained peer')
	expect_value(actual.fetch('state'), state.to_s, 'peer state')
end
Then('host {int} retains {int} events') do |host, count|
	actual = rpc("query host #{host} 0")
	expect_value(actual.fetch('events'), count.to_s, 'retained events')
end
Then('event {int} on host {int} retains bytes {string}') do |n, host, bytes|
	actual = rpc("query event #{host} #{n}")
	expect_value(actual.fetch('exists'), '1', 'retained event')
	expect_value(actual.fetch('bytes'), bytes, 'received bytes')
end
Then('events on host {int} have increasing sequences') do |host|
	first = rpc("query event #{host} 0").fetch('sequence').to_i
	second = rpc("query event #{host} 1").fetch('sequence').to_i
	raise 'event sequence order' unless first.positive? && second > first
end
Then(
	'the boundary has accepted {int} send calls on channel {int}'
) do |count, channel|
	actual = rpc('counters')
	expect_value(actual.fetch('sends'), count.to_s, 'send count')
	expect_value(actual.fetch('channel'), channel.to_s, 'channel')
end
Then('protocol service continues under pressure') do
	count = rpc('counters').fetch('protocol').to_i
	raise 'no protocol service' unless count > 0
end
Then('all native hosts and packets are released') do
	# counters needs an initialized world; use another isolated empty world.
	rpc('world 1')
	actual = rpc('counters')
	expect_value(actual.fetch('hosts'), '0', 'host cleanup')
	expect_value(actual.fetch('packets'), '0', 'packet cleanup')
end
Then('no event exists on host {int}') do |host|
	actual = rpc("query event #{host} 0").fetch('exists')
	expect_value(actual, '0', 'isolation')
end
Then(
	'host {int} receives bytes {string} within the UDP deadline'
) do |host, bytes|
	deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 8
	matched = false
	until matched
		now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
		break if now >= deadline
		rpc('progress')
		64.times do |index|
			event = rpc("query event #{host} #{index}")
			break if event.fetch('exists') == '0'
			match = event['kind'] == '1' && event['bytes'] == bytes
			matched ||= match
		end
		sleep 0.001 unless matched
	end
	raise 'UDP receive deadline exceeded' unless matched
end
When('I progress until host {int} has a connected peer') do |host|
	deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 8
	loop do
		rpc('progress')
		peer = rpc("query peer #{host} 0")
		break if peer['state'] == '1'
		now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
		raise 'UDP connection deadline exceeded' if now >= deadline
		sleep 0.001
	end
end

Given('I load the following transport script:') do |script|
	actual = rpc("script #{script.unpack1('H*')}")
	expect_value(actual.fetch('loaded'), '1', 'script loading')
end
Given('named host {string} occupies fixture slot {int}') do |name, slot|
	actual = rpc("bind #{slot} #{name}")
	expect_value(actual.fetch('bound'), '1', 'named host')
end
When('I start transport declaration {string}') do |name|
	actual = rpc("startup #{name}")
	expect_value(actual.fetch('started'), '1', 'explicit startup')
end
When('I delete transport declaration {string}') do |name|
	actual = rpc("delete_name #{name}")
	expect_value(actual.fetch('deleted'), '1', 'declaration deletion')
end
Then('startup {string} is {string} with {string}') do |name, state, code|
	states = {'Waiting' => 0, 'Pending' => 1, 'Finished' => 2}
	actual = rpc("status #{name}")
	expect_value(actual.fetch('exists'), '1', 'startup status')
	expect_value(actual.fetch('state'), states.fetch(state).to_s, 'state')
	expect_value(actual.fetch('code'), RESULTS.fetch(code).to_s, 'code')
	expect_value(actual.fetch('start'), state == 'Waiting' ? '1' : '0',
		'one-shot startup tag')
end
Then('startup {string} has a resulting peer') do |name|
	actual = rpc("status #{name}")
	expect_value(actual.fetch('peer'), '1', 'startup peer')
end
Then('host {int} has no peer at slot {int}') do |host, slot|
	actual = rpc("query peer #{host} #{slot}")
	expect_value(actual.fetch('exists'), '0', 'peer cleanup')
end
