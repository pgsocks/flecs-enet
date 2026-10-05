@contract
Feature: Script-defined transport startup
  Applications import ENet, load configuration and progress the world.
  Startup is explicit and each admitted attempt executes once.

  Background:
    Given world 0 imports the module

  Scenario: A connection waits until its configured host starts
    Given I load the following transport script:
      """
      using flecs_enet
      client {
        HostConfig: {}
        remote {
          ConnectionConfig: {
            address: {ip: [127, 0, 0, 1], port: 9050}, correlation: 17
          }
          Start
        }
      }
      """
    When I progress the world
    Then startup "client.remote" is "Waiting" with "OK"
    When I start transport declaration "client"
    And I progress the world
    Then startup "client" is "Finished" with "OK"
    And startup "client.remote" is "Waiting" with "OK"
    When I progress the world
    Then startup "client.remote" is "Finished" with "OK"
    And startup "client.remote" has a resulting peer
    When I progress the world
    Then startup "client.remote" is "Finished" with "OK"

  Scenario: A failed attempt requires an explicit retry
    Given I load the following transport script:
      """
      using flecs_enet
      server { HostConfig: {}; Start }
      """
    When the next "host" boundary call fails
    And I progress the world
    Then startup "server" is "Finished" with "TRANSPORT"
    When I progress the world
    Then startup "server" is "Finished" with "TRANSPORT"
    When I start transport declaration "server"
    And I progress the world
    Then startup "server" is "Finished" with "OK"
    When I start transport declaration "server"
    And I progress the world
    Then startup "server" is "Finished" with "STATE"

  Scenario: Deleting a declaration releases its generated peers
    Given I load the following transport script:
      """
      using flecs_enet
      client {
        HostConfig: {}
        Start
        remote {
          ConnectionConfig: {address: {ip: [127, 0, 0, 1], port: 9050}}
          Start
        }
      }
      """
    And named host "client" occupies fixture slot 0
    When I progress the world
    And I progress the world
    Then startup "client.remote" has a resulting peer
    When I delete transport declaration "client.remote"
    Then host 0 has no peer at slot 0
