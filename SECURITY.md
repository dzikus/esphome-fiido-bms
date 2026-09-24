# Security Policy

## Supported versions

Latest release only.

## Reporting a vulnerability

Use GitHub private vulnerability reporting:
https://github.com/dzikus/esphome-fiido-bms/security/advisories/new

Do not open a public issue.

Include the component version or commit, the ESPHome version, the bike model,
the configuration and the node log.

There is no response-time commitment.

## Scope

The code in this repository.

## Out of scope

- ESPHome, ESP-IDF and Home Assistant. Report to those projects.
- The bike's BLE protocol. It has no pairing, authentication or encryption,
  and frames are checked with a one-byte XOR. Any BLE central in range can read
  the BMS and write its registers without this component.
- Access control on the entities. The `motor` switch starts the motor and the
  `speed_limit` select can remove the speed limit. Who can use them is set in
  Home Assistant.
