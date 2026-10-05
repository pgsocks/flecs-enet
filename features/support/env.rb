require 'open3'
require 'timeout'

Before do |scenario|
	name = scenario.source_tag_names.include?('@udp') ?
		'FLECS_ENET_UDP_FIXTURE' : 'FLECS_ENET_FIXTURE'
	path = ENV.fetch(name)
	@input, @output, @error, @process = Open3.popen3(path)
	@stderr = Thread.new { @error.read }
	@last = {}
	@request_count = 0
end

After do
	begin
		if @process&.alive?
			@input.puts 'quit'
			@input.flush
		end
		status = Timeout.timeout(5) { @process&.value }
		raise "fixture exited #{status}: #{@stderr.value}" if
			status && !status.success?
	rescue Timeout::Error
		Process.kill('KILL', @process.pid)
		@process.join
		raise 'fixture cleanup timed out'
	ensure
		[@input, @output, @error].compact.each(&:close)
		@stderr&.join
	end
end

# Procedural helpers use the scenario's single, persistent C process.
def rpc(command)
	@input.puts command
	@input.flush
	line = Timeout.timeout(5) { @output.gets }
	raise "fixture EOF for #{command}" unless line
	raise "fixture command failed: #{line}" unless line.start_with?('ok ')
	line.strip.split.drop(1).to_h { |field| field.split('=', 2) }
end

def expect_value(actual, expected, detail)
	return if actual == expected
	raise "#{detail}: expected #{expected.inspect}, got #{actual.inspect}"
end

OPERATIONS = {
	'open' => 0, 'close' => 1, 'connect' => 2, 'send' => 3,
	'broadcast' => 4, 'flush' => 5, 'disconnect' => 6,
	'disconnect_now' => 7, 'disconnect_later' => 8, 'reset' => 9,
	'bandwidth' => 10, 'compression' => 11, 'ping' => 12,
	'ping_interval' => 13, 'timeout' => 14, 'throttle' => 15
}.freeze
MODES = {
	'reliable' => 0, 'unreliable' => 1, 'unsequenced' => 2,
	'unreliable_fragment' => 3
}.freeze
RESULTS = {
	'OK' => 0, 'ARGUMENT' => 1, 'STATE' => 2, 'NOT_FOUND' => 3,
	'BACKPRESSURE' => 4, 'NOMEM' => 5, 'TRANSPORT' => 6,
	'PARTIAL' => 7, 'SCHEMA' => 8, 'LIMIT' => 9
}.freeze

def submit(host, operation, peer: 0, channel: 0, mode: 'reliable',
	correlation: 0, a: 0, b: 0, c: 0, bytes: '-')
	@last = rpc("submit #{host} #{OPERATIONS.fetch(operation)} #{peer} " \
		"#{channel} #{MODES.fetch(mode)} #{correlation} " \
		"#{a} #{b} #{c} " \
		"#{bytes}")
	if @last.fetch('code') == '0'
		@request_slot = @request_count
		@request_count += 1
	end
end
