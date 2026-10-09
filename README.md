# LYRA-4 for the SP-1

LYRA-4 is a four-voice, LYRA-8 / LIRA-4-inspired experimental synthesizer, built as custom firmware for the Teenage Engineering SP-1 stem player. It has a half-rate 24 kHz engine, a 600 ms delay, four pages of controls.

Unaffiliated with Teenage Engineering. **Flashing custom firmware is at your own risk.** The bootloader (T1 + T4 on power-up) is below this firmware and stays reachable.

## Flashing
1. Open <https://solderless.engineering>, hold **T1 + T4** while connecting the SP-1 by USB-C (four track lights solid).
2. Select the firmware `.bin` and flash, then unplug and replug.

Full checklist for a new image: [docs/TEST-CARD.md](docs/TEST-CARD.md).

## Building
See [docs/BUILD.md](docs/BUILD.md). Safety rules every image must meet: [docs/SAFETY.md](docs/SAFETY.md).

## Licence
MIT. This firmware builds on open-source projects; their licences and attribution must stay with any copy, see [LICENSE](LICENSE), [NOTICE](NOTICE) and [LICENSES/](LICENSES/).
