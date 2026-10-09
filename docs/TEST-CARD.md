# LYRA-4 first image: test card

**Status: built and size/safety-checked only. Never run on an SP-1. You are the first test.**

Files
- `lyra4-first-image.bin`: LYRA-4 build (built from the base firmware at 67f80bd). 125,832 B (v0.7.2). `check_image` passes (it reports "fallback config" because it only knows the Plaits layout).
- `stock-67f80bd-knowngood.bin`: stock LYRA-4+Plaits built from the same tree with the same toolchain, `check_image` passes. Use it to go back. (Any release you already trust works as well.)

Flash (LYRA-4 docs/BUILD.md): solderless.engineering, USB-C connected, hold **T1 + T4** while plugging in (four track lights solid), select the `.bin`, flash, unplug and replug. The same combination is the recovery path; it lives in the bootloader, below this firmware.

Do the checks in this order. Stop at the first failure and flash the known-good image.

1. **Power on:** hold `••` about 1.5 s. Expect the dark window, then a fill. Silent, play row shows one LED, the TOP side LED (page 1; pages then step downward, page 4 is the bottom LED). Track LEDs all dark and steady (every fader is disarmed but untouched, and voices are off).
2. **Power off:** hold `••` 3 s with nothing else touched, even with sound playing. Expect the shutdown animation (about 4.3 s in all) and off. Release early: cancels. Try it with voices ringing and the delay swelling: sound level must not cancel it.
3. **`••` as shift does not power off:** hold `••`, move a fader, keep holding past 3 s. Must not shut down. The four side LEDs then blink fast ("cancelled, let go and hold again"). Release and hold again with no fader or button touched: it must shut down. If it ever fails, capture the console and send the `PWR  shutdown suppressed:` line. The 30 s backstop is unchanged and still powers off.
4. **Charge LEDs:** off, plug USB. Expect only the side (play) row to show the charge bar; **T1-T4 dark**.
5. **Headphones:** on, plug headphones; audio on both ears and the speaker mutes. Unplug: speaker back.
6. **Sound:** press T1 (voice 1 latches on). Turn volume down first (VOL-) if unsure; default is -12 dBFS. Move F1: while it is not yet picked up, T1 blinks as you move it (and for about a second after). Sweep F1 through its stored value to arm; the blinking stops and the fader tunes voice 1.
7. **Faders are inert across pages:** RWD/FFWD change page; moving a fader that is not armed must change nothing audible.
8. **CPU:** from a serial console, read the `AUD ... cyc avg= max=` line every 5 s with all four voices on, page 2 cross-FM up, delay feedback up. The gate is max under 85 %. If it is over 100 % the audio will stutter; report the numbers.
9. **No battery overlay while ON:** tap `••` briefly; the play row must keep showing the page indicator and nothing else.
10. **Shift layer LEDs (page 1, `••` held):** T1-T4 show fast 1+2, fast 3+4, vibrato, quantize as steady on/off, never blinking. `••`+T1 then T1 lights; press again, it goes dark. The top side LED is dim, and blinks only while the pitch or hold fader is not yet picked up.
11. **Momentary mode (page 1):** hold `••` and press FFWD: the second side LED lights. Release `••`; a T now sounds only while held, and PLAY sounds all four while held. Hold `••` and press FFWD again: the LED goes dark and the Ts latch again. Changing mode silences all voices. Combine with fast (`••` + T1/T2) for short notes. The mode resets to latch at power-on.
12. **Three-way selectors (pages 2 and 3):** page 2 T1/T2 (source) and page 3 T2 (delay source) step through three states on each press: bright (other pair / LFO), dim (LFO-or-feedback / own tap), dark (off), then bright again. Every other T on those pages is plain on/off.
13. **Bootloader:** unplug, hold T1+T4, plug in. Four track lights solid.

Known limits: voices off at every power-on (by design); delay feedback default 0.30; USB audio and MIDI are not in this build.
