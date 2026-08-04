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
