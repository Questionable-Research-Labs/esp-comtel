# How we taught an orphaned LED sign to speak again

*The story of reverse-engineering the Comtel sign, mid-2026. Written for
humans, not engineers. The technical spec this hunt produced is in
[PROTOCOL.md](PROTOCOL.md).*

---

## The patient

The sign is a relic: six tiles of red, green and blue LEDs bolted side by
side, 96 lights across and 16 high, made by a New Zealand company called
Comtel sometime before the internet had opinions. It worked — its original
controller box could still make it scroll — but that controller was the only
thing on Earth that knew how to talk to it. No manuals. No datasheets. The
main chip on each tile, an "LD71D1048", returns nothing useful from any
search engine. If we wanted this sign to display *our* words, we had to learn
its language by wiretap.

The plan: listen to the original controller talking to the sign, figure out
the language, then replace the controller with a $5 ESP32 microcontroller.

## False starts

The first guess was that the sign spoke HUB75, the lingua franca of hobbyist
LED panels. It doesn't. Weeks of confident wiring produced nothing.

The second tool was turning a spare ESP32 into a makeshift wiretap, sampling
the wires as fast as it could. This produced data — and the data lied. The
sign's clock signal ticks five million times a second, right at the edge of
what the ESP32 could see, and like a wagon wheel spinning backwards in an
old film, the samples showed convincing phantoms. Several detours were built
on those phantoms.

A real oscilloscope finally gave honest answers: the controller sends the
sign short **bursts** — 4,608 clock ticks, a fifth of a millisecond of
machine-gun data — and between bursts it just cycles the sign's rows so the
picture stays lit. We copied that faithfully. And got the single most
maddening result of the whole project:

**exactly two rows of the sign obeyed us. The other fourteen ignored
everything.**

## Two rows and a wall

You could write to the bottom row (and its partner row halfway up — the sign
addresses rows in pairs). Nothing else. Not with a faster clock, a slower
clock, a cleaner clock. Not by wiggling the address lines, the latch line,
or by any combination of the dozens of settings the test firmware grew that
winter of an evening. Every burst went to the same two rows like mail
delivered to the wrong house, forever.

An audit of the whole project produced a beautiful theory: our home-made
clock signal had tiny invisible gaps in it (the ESP32's operating system
would interrupt our transmission a thousand times a second), so maybe the
sign needed one perfectly unbroken burst. We rebuilt the transmitter around
a hardware clock — provably perfect, gap-free, exactly like the original's.

The sign was not impressed. Two rows.

## The night of small questions

What broke the case open wasn't a bigger theory. It was smaller, better
questions — and a human standing in front of the glass reporting what the
LEDs actually did.

*"If we can really write to those two rows… can we make them light up, not
just go dark?"* That simple control experiment failed — our "line 0" test
pattern produced nothing — and for one vertiginous hour it looked like we had
never written a single bit to the sign at all. Then the all-on test lit the
rows white, and the failure turned into a clue: some data lines mattered and
some seemed invisible.

The invisible ones turned out to be *dim*. The six "data" wires we'd assumed
were six separate streams were actually the six binary digits of a **single
brightness number**, delivered in parallel, one number per LED: wire 0 is
worth 1/63rd of full brightness (invisible to the eye), wire 5 is worth half.
Every "colour theory" we'd built was wrong; the panel does 64-level dimming
per LED, and human eyes just can't tell 13% duty from 100% across a room.

Then came the checkerboard. Every test until that night had sent *constant*
patterns — all-on, all-off — and any code looks the same when the input never
changes. The moment we sent data that flipped every single clock tick, the
glass answered with structure: alternating coloured pixels that revealed the
sign eats **one LED's worth of data per clock tick**. Follow-up probes — a
single lit pixel, a block of lit ticks that silently vanished — exposed a
conveyor belt inside the chip: a 576-stage bucket brigade. Data marches
through and falls off the end; when the latch line drops, **whatever 576
values are on the belt at that instant** get stamped into one pair of rows.

So writing worked perfectly. The only mystery left was: what chooses *which*
pair of rows gets stamped?

## The three grounded wires

Three wires on the sign's connector had been tied to ground since the
beginning, because an early scope reading said they were "always low." The
audit had flagged them as suspects — an earlier capture had seen them
twitch — and with everything else in the protocol now understood, they were
the only wires left. We unsoldered them from ground and gave the ESP32
control of them.

Then we spent hours confusing ourselves. Pulsing them did strange partial
things. Counting patterns on them made *different* rows respond on different
evenings. Rows lit up in ways that suggested a mysterious "pointer" stepping
around inside the chip — but only during long automated runs, never when we
fired single, careful test bursts. We invented pointers, timers, crosstalk
ghosts. (Some confusion was self-inflicted: the serial port resets the ESP32
every time a script opens it; the chip's boot-up glitches spray random writes
at the sign; and our own test scripts, stepping a value upward one key-press
at a time, fired a burst *at every intermediate value* — painting rows we
then attributed to magic.)

The fog lifted on a question from the project's owner, watching yet another
theory die: *"if we're missing header information, why would we get data on
those two rows at all?"* Exactly. A sign that demands a secret password
doesn't deliver mail without it — unless the "password" is simply an address
that's always present. Re-reading every result of the night through that
lens, one boring rule explained all of it:

**Those three wires are a house number.** The value sitting on them at the
moment the latch drops picks the destination: wired to ground they spell
"pair 0," which is why the sign spent weeks writing everything to the same
two rows. Set them to `(8 − p) mod 8` and the burst lands on pair *p*. No
pointer, no timer, no header. Three wires, one number.

Minutes later, all sixteen rows took data for the first time, and the sign
scrolled the message chosen for the occasion: **QUESTIONABLE RESEARCH**.

## Reading the glass

The text was garbled, of course — victory in this project was never allowed
to be tidy. The remaining puzzles were solved not with instruments but with
*test cards*: patterns designed so that whatever appeared on the glass would
spell out the answer. A staircase pattern exposed that rows were being
woven together; two thin vertical lines, landing in six wrong places,
revealed the real data order — the sign wants each of its six tiles served
one at a time, sixteen lower-row pixels then sixteen upper-row pixels, three
colours per pixel, tile after tile, left to right. A final one-pixel
misalignment turned out to be the sign's own quirk: the bottom half's row
decoder is offset by one from the top half's. One line of code compensates.

Clean text, all sixteen rows, any message typed over serial.

## What it took, and what it taught

- **Instruments lie at their edges.** The ESP32 wiretap aliased; the
  two-channel scope stitched together a picture of simultaneous signals that
  were never captured simultaneously; eyes can't tell 13% brightness from
  100%.
- **Constant test patterns teach you nothing.** Every encoding looks
  identical when the input never varies. The project turned the moment the
  data started *changing*.
- **Run the positive control.** "Can we make it light up, not just go dark?"
  nearly falsified two weeks of assumptions in one glance.
- **One careful shot beats a thousand automatic ones.** Auto-repeating
  bursts smeared every observation; single hand-fired bursts made the sign's
  behaviour deterministic and readable.
- **Write the wrong theories down too.** The archive of this repository is
  full of confident, dated, wrong conclusions — and the paper trail is
  exactly what made each next theory possible.
- And sometimes the breakthrough is someone who never stopped watching the
  actual lights asking the obvious question nobody else would.

The original controller has been retired with honour. The sign speaks ESP32
now.

## Postscript: the bright row (2026-07-26)

One row — the ninth from the bottom — came out brighter than its neighbours.
The natural suspicion was that it was being drawn twice, but the row mapping
was clean: every canvas row is sent exactly once per frame. The row was not
drawn twice, it was *lit* six times as long. And it had a twin: the very
bottom row was equally over-lit, easy to miss at the edge of the glass, which
is what named the culprit — those two rows are the two halves of one row-pair,
and a pair is what a single scan address lights.

Writing a row-pair requires parking the display scan address at 0, because the
latch destination is sampled at latch time. The driver parked it there for the
whole burst — 4608 clocks, interrupts masked, about 2.3 ms — and did that eight
times per frame. Address 0 therefore held the glass for roughly 19 ms of a
45 ms frame while the other seven addresses split the rest. Address 0 lights
pair 0: physical rows 0 and 8.

Nothing about the shift itself needs a fixed address — only the latching edge
consults ADDR — so the burst now multiplexes the display as it shifts, handing
each of the eight addresses an equal slice of its own clocks and restoring
ADDR = 0 with a settling margin just before the latch. The burst also shrank
from eight copies of the payload to two; the shift register only ever keeps the
last one, so the other six were display time thrown away — the frame went from
41.9 ms to 25.7 ms as a side effect.

The old behaviour survives as `scanDuringBurst = false`, and it earned its keep
immediately: flipping it back made the bright rows return, and flipping it
forward made them vanish. Shortening the burst on its own — a four-fold cut in
the freeze — was *not* enough; the rows were still clearly brighter. Only
refusing to stand still fixed it. A staircase test card confirmed that moving
ADDR mid-burst leaves every pair landing where it should.

The lesson is an old one in a new costume: with a multiplexed display, *time
is the pixel value*. Every millisecond spent not scanning is a millisecond
spent lighting one row.
