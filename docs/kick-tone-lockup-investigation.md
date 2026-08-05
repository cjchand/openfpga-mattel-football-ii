# Kick-button lockup: root cause and fix (2026-08-04)

## Outcome

**Three real bugs in this project's CPU implementation.** The game locked up
because our MM77LA emulation diverged from the hardware, not because of
anything in the 1978 ROM.

Fixed in both implementations (`sim/golden/mm77la_model.cpp` and
`src/pps41_core.v`, kept in lockstep):

1. **`EOB` decoded a 2-bit immediate instead of 3 bits.**
   MAME's `mm76op.cpp::op_eob()` is `m_b ^= m_op << 4 & m_datamask`, with
   `m_datamask == 0x7F` for this chip's 7-bit `B`, and the opcode occupies
   the whole `0x08-0x0F` range (`op_is_eob(op) == ((op & 0xf8) == 0x08)`).
   We masked the immediate to `op & 0x3`, which made **`EOB 4` (opcode
   `0x0C`) a silent no-op**. `EOB 4` is precisely how the ROM's own
   RAM-clearing init loop crosses from RAM banks 0-3 into banks 4-7, so a
   third of RAM was never cleared and was unreachable by that path.
   First divergence from MAME: retired instruction **147**.

2. **The carry delay published one instruction too early.**
   `AC`/`ACSK` set a one-instruction carry delay. MAME's `execute_run()`
   tail is `m_c_in = m_c_delay ? m_prev_c : m_c;` where `m_prev_c` is `m_c`
   sampled at the *start* of the current instruction -- so an `AC`'s new
   carry stays invisible for one more instruction and only lands on the one
   after. We republished the *new* carry immediately, i.e. no delay at all.
   A single instruction of carry skew is enough to flip a `SKNC` and send
   control flow down the wrong branch. Fixed by adding `prev_c` to both
   implementations. First divergence: retired instruction **511**.

3. **Bytes consumed by a skip polluted the `prev_op` history.**
   MAME replaces a skipped opcode with a fake NOP (`m_op = 0`) before it
   shifts into `m_prev_op`. We recorded the real byte. Because the
   `LAI`/`LB`/`EOB` "successive ... are ignored" coalescing rules are
   decided purely from `prev_op`, a skipped byte that happened to look like
   one of those opcodes wrongly suppressed the next genuine one. Real
   instance in this ROM: a `SKBF` at `0x02D` skips the byte `0x45`, which
   we mistook for `LAI 5` and used to suppress the genuine `LAI 6` at
   `0x03B`. First divergence: retired instruction **549**.

With all three fixed, the golden model matches MAME **instruction for
instruction over 200,000 retired instructions, for all 8 buttons plus
idle** -- and the RTL matches the golden model over a 2,000,000-cycle
lockstep run with Kick held. The tone now behaves like a real sound effect
(longest continuous tone ~0.72 s, ending off) instead of latching on
forever, and input keeps being serviced.

## Why the previous conclusion was wrong

An earlier session concluded this was **an authentic bug in the 1978 ROM**,
triggered by an `RC` at ROM address `0x038` corrupting a carry flag that
shared downstream code depended on, and shipped an audio watchdog that
force-silenced the stuck tone after ~2 s. That watchdog has been reverted.

Two lessons worth keeping:

- The reasoning was internally consistent and cited real sources (the
  Rockwell MM78 datasheet, MAME's `mm78op.cpp`, the game manual), but it
  rested on an unverified premise: that our CPU executed the ROM correctly.
  It did not. **Agreement between our own two implementations proved
  nothing** -- the RTL was a deliberate port of the golden model, so they
  share their bugs by construction. Two implementations only cross-check
  each other if they were derived independently.
- The claim implied a mass-produced product failed on its most basic
  interaction ("press KICK to start"). That implausibility was noted at the
  time and then argued around. It should have been treated as the strongest
  available signal that the emulation, not the ROM, was at fault.

The decisive experiment took about ten minutes once framed correctly: build
MAME's `mfootb2` driver and check whether *it* locks up. It does not --
`IOS` keeps firing long after the Kick press.

## Getting a MAME reference trace

This is now the highest-value tool for any future CPU discrepancy, and is
worth rebuilding rather than hand-tracing. `sim/golden/mame_parity_test.cpp`'s
header comment carries the full recipe; the essentials:

```bash
# Cut-down MAME with only this driver (much faster than a full build)
cd ~/Projects/mame/mame
make SUBTARGET=pps41 SOURCES=src/mame/handheld/hh_pps41.cpp \
     USE_LIBSDL=1 ARCHOPTS=-I/opt/homebrew/include REGENIE=1 -j10
```

macOS caveat: the generated link line omits SDL3 entirely. Add
`-L/opt/homebrew/lib -lSDL3` to the `LIBS` line of
`build/projects/sdl3/mamepps41/gmake-osx-clang/pps41.make` and re-run that
makefile directly. It must go in `LIBS` (which lands *after* the `.a`
archives), not `LDFLAGS` -- ordering matters or the symbols stay undefined.

The romset is built straight from `development-assets/`; both files there
already match MAME's expected SHA1s:

```bash
zip mfootb2.zip b8000-12 mm77la_mfootb2_output.pla
```

Tracing is non-interactive via `-debugscript` (the earlier session's report
that this hangs was mistaken -- it works with `-video none -sound none
-nothrottle -seconds_to_run N -skip_gameinfo`):

```
trace out.txt,0,noloop,{tracelog "| A=%X B=%02X C=%X S=%X ", a, b, c, s}
go
```

Buttons are driven from a Lua `-autoboot_script`. Note the D-pad fields are
named `P1 Up`/`P1 Right`/`P1 Down`/`P1 Left` -- `fields["Up"]` silently
does nothing, which briefly looked like "the D-pad has no effect".

Two properties of MAME's trace matter when comparing:
- it logs **only non-skipped** instructions, and
- it logs each instruction's registers as of **before** it executes, and
- it **omits the reset NOP at `0x3C0`**, so its index 0 is our index 1.

## Regression coverage

- `make mame-parity-test` (part of `make test`) replays the real ROM for
  200,000 instructions across 9 input scenarios and checks a digest of the
  register trace against values captured from MAME. Reintroducing any one of
  the three bugs above fails all 9 scenarios.
- Per-opcode unit tests in `sim/golden/mm77la_model_test.cpp`:
  `test_eob_immediate_is_three_bits_wide`, `test_eob_after_eob_is_suppressed`,
  `test_lb_after_eob_is_suppressed`,
  `test_eob_applies_after_the_first_lb_of_a_run`,
  `test_ac_carry_is_not_visible_until_two_instructions_later`,
  `test_back_to_back_ac_uses_the_older_carry`,
  `test_skipped_byte_enters_prev_op_history_as_a_nop`.
- `./obj_dir_core/Vpps41_core ../development-assets/b8000-12 2000000 <stim>`
  runs the RTL against the golden model on the real ROM with a held button.

Two earlier tests asserted the buggy behaviour and were corrected rather
than deleted: `test_back_to_back_ac_does_not_lose_first_pending_carry` (now
`test_back_to_back_ac_uses_the_older_carry`) encoded the off-by-one carry,
and `test_rc_clears_carry_immediately`/`test_sc_sets_carry_immediately`
asserted on `c_in`'s mid-instruction value; they now assert the behaviour
that actually matters (what the next `SKNC` observes).

## Still open

- **D-pad gameplay is not yet verified end to end.** At idle the D-pad
  produces no visible reaction -- but MAME does exactly the same thing, so
  this is the game's behaviour, not a bug in the core, and should not be
  re-investigated as one. Pressing Status (or Kick) and then a direction did
  not produce a divergence in MAME either, with the specific timings tried;
  finding the input sequence that actually moves the player is a
  game-behaviour question, best answered by playing MAME interactively.
- **`INT1L` (opcode `0x04`) is still a flag-only no-op** in both
  implementations. It is unused by this ROM (verified: zero occurrences), so
  it is not affecting anything today, but MAME implements it as a real skip
  (`m_skip = !m_int_line[1]`) and ours should too.
- **Not yet re-tested on real hardware.** The fixes are verified in
  simulation only; a bitstream rebuild and SD-card deploy is the next step.

---

# The second lockup: IOS applied ~129 times per instruction

The three CPU bugs above fixed the *first* lockup (the one at the kickoff).
On 2026-08-04 the core was played on real hardware for the first time since:
boot, kickoff and gameplay all worked, but after a while the game locked up
with a steady low tone, and the tones throughout were slightly flat.

Both symptoms are one bug.

## What the device reported

`src/debug_probe.v` had shipped in the bitstream for exactly this. The
photographed strip decoded to:

| slot | meaning | value |
|---|---|---|
| 0 | marker | 1 |
| 1 | `cause_tone` | **1** |
| 2 | `cause_pc` | 0 |
| 3 | unimplemented opcode seen | 0 |
| 4 | `INT1L` seen | 0 |
| 5-15 | latched PC, MSB first | `0 1110 1110 10` = **0x3BA** |

So the CPU was **still executing** (`cause_pc` clear -- the PC moved within
every 0.5s window) and had simply held `tone_on` high continuously for two
seconds. No sound effect in this game is longer than 0.72s.

## Why the ROM can be made to hold a tone forever

IOS is a three-state machine in the chip: state 1 turns the tone ON, states 0
and 2 turn it OFF (`mm78laop.cpp: op_ios`). The game keeps **its own mirror of
that state in a RAM bit**, and uses it to decide whether an extra
state-consuming IOS is needed. At page `0b`:

```
0b:2b SKBF 4     ; if the "tone running" flag is clear, skip...
0b:2a IOS        ; ...this state-consuming IOS
0b:35 RB   4     ; clear the flag
...
0b:3f LAI 15 / 0b:1f IOS / 0b:0f LAI 5 / 0b:07 A / 0b:03 IOS
```

If the chip's `ios_state` and the ROM's RAM-bit mirror ever fall out of step
by one, the IOS the game intends as "turn the tone off" lands on chip state 1
and turns it **on**, permanently, with the CPU running normally throughout.
That is the observed symptom exactly.

## Root cause

`op` is combinational off `rom_data`, and `rom_addr`/`pc` only move on `ce`.
On the device `ce_gen` pulses `ce` once every ~129.35 core clocks, so `op` --
and every combinational `*_fire` strobe derived from it -- sits stable for the
whole ~129-clock period. `pps41_tone` is clocked on `clk`, and its IOS branch
was not qualified by `ce`:

```verilog
if (ios_fire) begin
    tone_freq <= {ios_a, tone_freq[7:4]};   // a SHIFT
    tone_on   <= ios_arms;
    ios_state <= next_ios_state;            // a modulo-3 INCREMENT
end
```

So one IOS instruction was applied ~129 times:

- `tone_freq` ended up as **the same nibble duplicated** (`{a,a}`) instead of
  `{a_high, a_low}` -- e.g. `0x44` where the reference has `0x40`. That is a
  ~6% period error, i.e. every tone slightly flat. The reported pitch problem.
- `ios_state` advanced by `(period mod 3)`, and `ce_gen`'s fractional
  accumulator alternates the period between 129 and 130 -- so **0 or 1**,
  depending on accumulator phase at that instant. When it came out 0 the chip
  fell one state behind the ROM's RAM-bit mirror, and the next "tone off" IOS
  turned the tone on forever. Intermittent, timing-dependent, and unrelated to
  what the player did -- which is why it appeared only after minutes of play.

`pps41_io` has the same unqualified structure and is **fine**, because every
one of its assignments is idempotent (set/clear a D bit, load R from A/C).
A comment there now says so, and says what would break the assumption.

## Why nothing caught it

Every core-level testbench held `ce` high on every clock. At `ce = 1` the
buggy and correct designs are indistinguishable -- one clock per instruction
means one application per instruction either way. The 1.2M-cycle real-ROM
lockstep, which does compare `ios_state`, `tone_freq`, `tone_on` and
`spk_output` against the golden model, passed clean.

The instrument was wrong, not missing.

## Fix and the test that pins it

`ios_fire` and `int0h_fire` in `src/pps41_core.v` are now qualified with `ce`.

`sim/pps41_core_tb.cpp` takes `--ce-period=N`, which pulses `ce` once every N
clocks, alternating 129/130 to reproduce `ce_gen`'s fractional accumulator
(a bug whose effect depends on `period mod 3` can hide behind an exact
divider). `core-rom-lockstep-test` now runs the 1.2M-cycle real-ROM lockstep
**twice**, at `ce=1` and at `--ce-period=129`. Measured: with the fix
reverted, the `ce=1` pass still succeeds and the `--ce-period=129` pass
diverges at cycle 220520 (`tone_freq` 0x44 vs 0x40, `ios_state` 0 vs 1).

`sim/golden/tone_stuck_fuzz.cpp` (`make tone-stuck-fuzz`) fuzzes the golden
model with randomised, human-plausible button traffic -- including two-button
presses, which no scripted stimulus produces -- and asserts the same
invariant `debug_probe` checks on hardware: `tone_on` is never continuously
high for 2s. 400 trials x 30M cycles (35 hours of chip time) found nothing.
That is what established the fault was not the ROM or the CPU semantics but
the RTL's clock-enable handling, and it stays in the suite as the guard on
that boundary.
