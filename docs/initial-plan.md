# Mattel Football II — Analogue Pocket openFPGA Core Design Spec

Companion project to `cjchand/openfpga-mattel-football` (Football I, Rockwell
B6100/rw5000). This is a **new CPU core**, not an extension of the FB1 core —
Football II runs on fundamentally different silicon. Only the openFPGA
scaffolding (core-template, Makefile, Verilator sim harness, dist/ packaging,
bezel pipeline) carries over mechanically.

All ISA/architecture facts below are transcribed directly from MAME's
`src/devices/cpu/pps41/*` (license: BSD-3-Clause, copyright hap), current as
of MAME master, and from the `handheld/hh_pps41.cpp` driver's `mfootb2`
machine definition. MAME is the closest thing to authoritative documentation
here — no Rockwell datasheet for the B8000/MM77LA specifically is known to
exist; hap's driver comments say the part number and pinout are **inferred**,
not confirmed from a datasheet. Treat MAME's C++ model as the golden
reference for cycle/behavior verification throughout this project, the same
way it (indirectly) was for the FB1 rw5000 core.

---

## 1. Target hardware

| | |
|---|---|
| Machine | Mattel Football 2 / Football II, model 1050 (1978) |
| PCB label | MATTEL, 1050-4369D |
| CPU | Rockwell **MM77LA**, die label **B8000**, package marking `B8000-12` |
| CPU family | PPS-4/1 (Rockwell's single-chip evolution of the older multi-chip PPS-4; distinct lineage from the B5000/B6100 "calculator-derived" cores used in Football I) |
| Clock | ~380 kHz on real hardware, RC oscillator (R=56K to VC pin). MAME uses this as an approximation — expect part-to-part variance on real units. 4 clock phases per cycle (an external osc is divided by 2 first). |
| Program ROM | 0x600 bytes (1536 bytes) instruction ROM + separate 317-cell **output PLA** (segment/LED decode table, not part of the instruction stream) |
| Data RAM | 96 nibbles (4-bit), addressed as described in §3 |
| Display | 7 seven-segment digits + 30 discrete LEDs (the field position indicators), multiplexed as a 10×11 PWM matrix |
| Sound | 2-bit speaker level output (levels: 0, +1, −1, 0), driven by an internal tone generator |
| Inputs | Score button, Status button, 4-way D-pad (16-way per MAME's port def), Kick button, Pass button, PRO1/PRO2 difficulty slide switch |
| MAME driver | `src/mame/handheld/hh_pps41.cpp`, machine `mfootb2`, status: fully working, `MACHINE_SUPPORTS_SAVE`, no working-status caveats |

### Known-good ROM hashes (from MAME's `ROM_START(mfootb2)`)

We do not have permission to include or distribute these — same posture as
FB1's `mfootb.bin`: the end user must source their own legally-dumped copy.
Use these hashes only to verify a self-sourced dump is correct, exactly as
FB1's design doc used the CRC32 `5b27620f` for `mfootb.bin`.

```
Main program ROM ("b8000-12"):
  size: 0x600 (1536 bytes)
  CRC32:  5b65fc38
  SHA1:   4fafc9deb5609b16f09b18b7346ea96ffe8bf9e0

Output PLA ("mm77la_mfootb2_output.pla"):
  size: 317 bytes (Berkeley PLA text format, not raw binary — see §6)
  CRC32:  11c0bbfa
  SHA1:   939a0a6adeace8ca0f9e17290306a2e7ced21db3
```

---

## 2. Architecture overview

PPS-4/1 is a 4-bit-accumulator single-chip microcontroller. Class hierarchy
in MAME (which mirrors real silicon derivation — MM77LA descends from MM78,
not MM76 directly):

```
pps41_base_device      (shared execution engine, PC LFSR, RAM/carry-delay semantics)
 └─ mm76_device         (base ISA — 640B ROM / 48 nibble RAM variant)
     └─ mm78_device     (extends ISA: 2-level stack, X register, banked jumps,
                          new opcodes, several opcodes' behavior *changed* from mm76)
         └─ mm78la_device  (adds output PLA + tone generator + IOA/OX/IX/IOS overrides)
             └─ mm77la_device  (= our target chip; overrides IX again for a
                                 narrower 10-bit PLA output; everything else
                                 inherited from mm78la_device)
```

Practically: **implement MM76 base semantics, then apply MM78's opcode
remapping/overrides, then MM78LA's I/O overrides, then MM77LA's final IX
override.** Do not implement MM76 opcode encodings directly for this chip —
the opcode map itself changes at the MM78 tier (see §5). MM77LA never
implements MM77 or MM76-tier code directly.

### Registers (from `pps41_base_device` state + `mm78_device` additions)

| Reg | Width | Purpose |
|---|---|---|
| PC | 11 bits (`prgwidth`) | Program counter. **Low 6 bits are NOT a binary counter — see §4.** |
| A | 4 bits | Accumulator |
| B | up to 7 bits (`Bu`/`Bl` split, see §3) | RAM address register, split into upper (Bu, bits 4-6-ish) and lower (Bl, bits 0-3) nibble-addressing halves |
| X | 4 bits | Secondary register (MM78+only; exchanged with A via XAX, loaded via LXA) |
| C | 1 bit | Carry flag (has both immediate `m_c` and a "delayed" `m_c_in` — some opcodes visibly update carry one cycle late, see §7) |
| S | 4 bits | Serial shift register (not used by Football II — no serial I/O wired up in the `mfootb2` machine config) |
| Stack | 2 levels | Return-address stack (`m_stack[2]`), push/pop via TM/TML (call) and RT/RTSK (return) |

### Skip / delay mechanics — read this before writing the RTL FSM

The core has several "one cycle later" behaviors that are easy to get wrong
in a naive port:

- **Skip-next-instruction**: many opcodes set `m_skip`. The *next* fetched
  opcode is decoded far enough to detect if it's itself a 2-byte transfer
  prefix (`op_is_tr`, i.e. `(op & 0xf0) == 0x30`, the `TR` opcode) — if so,
  skipping continues through the whole multi-byte instruction, not just one
  byte. This chip supports up to 3-byte instructions when stacked TR
  prefixes appear (`op_is_tr(prev_op) && op_is_tr(prev2_op)` — see the 3-byte
  dispatch branch in `mm78_device::execute_one()`).
- **`m_skip_count`**: separate mechanism used by `TAB` (table lookup
  transfer) — skips `A+1` instructions going forward, used for jump tables.
- **RAM address delay (`m_ram_delay`)**: some opcodes (XAB, LBA on the MM76
  tier — though note MM78's `op_lba` override explicitly *disables* the
  delay: "LBA: no RAM delay") change `B` but the *previous* B value is used
  for RAM addressing during the cycle the change takes effect. Implemented
  in MAME via `m_ram_addr = m_b` at end of cycle, then corrected if
  `m_ram_delay` was set.
- **SAG (set-address-group)**: forces the upper RAM address bits to `3` for
  exactly the next cycle only (`m_sag` flag, MM78+ only opcode).
- **Carry delay (`m_c_delay`)**: `AC` (add-with-carry) sets carry, but
  `m_c_in` (what `SKNC`/etc. actually read) is only updated to the new value
  *after* the current instruction completes — i.e., a carry set by `AC` isn't
  visible to `SKNC` until the instruction after next. Get a cycle-accurate
  golden trace from MAME before trusting your own carry-delay implementation.
- **LB/EOB/LAI coalescing**: successive `LB`/`EOB` pairs, and successive
  `LAI`s, have "ignore repeats" logic (see `op_lb`/`op_eob`/`op_lai` bodies in
  §5.2) that depends on tracking not just the previous opcode but the
  previous-previous opcode too (`m_prev2_op`, `m_prev3_op`). This is because
  a `TR` (extended-opcode prefix) byte in between resets the "am I still in
  a coalescing run" state. Do not simplify this to "if prev op == LB, skip."

**Recommendation for the RTL:** build a cycle-accurate testbench that runs
identical instruction streams through (a) your Verilog core, (b) a
Python/C model transcribed directly from these MAME files, and (c) MAME
itself in debugger mode, and diff every architectural register + `m_skip`
value every cycle. Do this from day one — these delay mechanics are exactly
where naive ports rot.

---

## 3. Memory map

### Program ROM — `mm77la_device` uses `mm78_device::program_1_5k`

```cpp
void mm78_device::program_1_5k(address_map &map)
{
    map(0x000, 0x3ff).rom();
    map(0x400, 0x5ff).mirror(0x200).rom();
}
```
Total addressable: 11-bit PC (`prgwidth = 11`, i.e. 0x000–0x7FF), with the
0x400-0x5FF region **mirrored** at 0x600-0x7FF. Actual populated ROM is only
0x600 bytes (matches the ROM_LOAD size above) — addresses 0x000-0x5FF are
real content, 0x600-0x7FF just re-reads 0x400-0x5FF.

### Data RAM — `mm78_device::data_96x4`

```cpp
void mm78_device::data_96x4(address_map &map)
{
    map(0x00, 0x3f).ram();
    map(0x40, 0x47).mirror(0x18).ram(); // NOT mirrored up to 0x50
    map(0x50, 0x57).ram();
    map(0x60, 0x67).mirror(0x18).ram(); // NOT mirrored up to 0x70
    map(0x70, 0x77).ram();
}
```
7-bit data address space (`datawidth = 7`, 0x00-0x7F), but only 96 nibbles
are real RAM — note the deliberately irregular gaps (`0x48-0x4F` and
`0x40-0x47`'s mirror do NOT extend to `0x50`; same pattern at `0x68-0x6F`).
Get this exactly right; it's not a simple power-of-two RAM block.

RAM cells are initialized to `0xF` at reset (`device_start`: `for (i) write_byte(i, 0xf)`).

### RAM addressing register (B)

B is not a flat address — it's split conceptually into "Bu" (upper,
determines address group, values like `0x30`/`0x40` etc. appear as bit
patterns in the opcode handlers) and "Bl" (lower nibble, `m_b & 0xf`).
`m_ram_addr = m_b` (masked by `m_datamask`) is the actual RAM address used
each cycle, subject to the one-cycle delay/SAG rules in §2. Do not assume Bu
is a clean separate register — in the C++ model it's literally the same
`m_b` byte, sliced with bitmasks per-opcode.

---

## 4. PC addressing — the LFSR quirk

This is the single most important non-obvious hardware detail. The **low 6
bits of the PC are not a normal binary counter** — they cycle through a
linear-feedback-shift-register (LFSR) sequence, a real artifact of how the
silicon's ROM addressing was laid out. Verbatim from MAME:

```cpp
void pps41_base_device::increment_pc()
{
    // low part is LFSR
    int feed = ((m_pc & 0x3e) == 0) ? 1 : 0;
    feed ^= (m_pc >> 1 ^ m_pc) & 1;
    m_pc = (m_pc & ~0x3f) | (m_pc >> 1 & 0x1f) | (feed << 5);
}
```

The upper bits of PC (page/bank, `~0x3f` mask) behave as ordinary storage —
only the bottom 6 bits (one "page" = 64 instructions) follow this LFSR
stepping. This is also why the disassembler needs an explicit lookup table
(`m_l2r`/`m_r2l`, 64 entries each) to translate between "real" (silicon,
LFSR) PC ordering and "linear" (sequential, human-readable) ordering — see
`pps41_common_disassembler`'s constructor, which builds this table by
iterating `increment_pc()` 64 times from 0.

**Implementation implication:** your Verilog PC register must implement this
exact LFSR recurrence for its low 6 bits, not a plain adder. If you build any
tooling that maps "linear" ROM offsets (e.g. from a disassembly or a
re-ordered ROM image) back to real silicon addresses, you need the same
64-entry l2r/r2l table — reproduce it programmatically with the recurrence
above, don't hand-transcribe it.

Jump instructions (`T`, `TL`, `TM`, `TML`, and MM78's banked variants `TLB`/
`TMLB`) write the PC directly (not via `increment_pc()`), so they're
unaffected by the LFSR — only sequential execution steps through it.
Note also the "subroutine page" reset behavior in `op_t`/`op_tm`: on-page
jumps/calls from what MAME calls "subroutine pages" (the top page of the
address space, `(pc & mask) == mask` where `mask = prgmask & ~0x7f`) have
special-cased page-reset/no-push behavior — read `op_t` and `op_tm` bodies
in §5.2 carefully, this is another easy place to introduce subtle bugs.

---

## 5. Instruction set

### 5.1 Opcode map (as executed — MM78 tier, which MM77LA inherits wholesale for dispatch)

This is `mm78_device::execute_one()`'s dispatch tree, reproduced structurally
(not the MM76 tier — MM77LA never uses MM76's opcode map, only MM76's
*implementations* of unchanged opcodes via virtual-function inheritance).

**Standard (1-byte) opcodes**, first dispatched on `op & 0xf0`:

| `op&0xF0` | Mnemonic | Notes |
|---|---|---|
| `0x10` | LB x | Load B from immediate nibble x (successive LB/EOB coalescing applies) |
| `0x30` | TR | Prefix — begins a multi-byte (extended) opcode |
| `0x40` | LAI x | Load A from immediate x (successive-LAI coalescing applies) |
| `0x60` | AISK x (x≠0) / I1SK (x==0, i.e. op==0x60 exactly) | Add-immediate-skip, or "add channel 1 to A, skip" special case at x=0 |
| `0x80-0xB0` | TM x | Transfer-and-mark (call) on-page |
| `0xC0-0xF0` | T x | Transfer (jump) on-page |

**Then, if top nibble didn't match, dispatch on `op & 0xFC`:**

| `op&0xFC` | Mnemonic |
|---|---|
| `0x08`/`0x0C` | EOB x (2-bit immediate, XORs into B's upper nibble) |
| `0x20` | SB x — set RAM bit x |
| `0x24` | RB x — reset RAM bit x |
| `0x28` | SKBF x — skip if RAM bit x is false |
| `0x50` | L x — load A from RAM, XOR x into Bu |
| `0x54` | XNSK x — exchange+increment-Bl+skip-on-wrap |
| `0x58` | XDSK x — exchange+decrement-Bl+skip-on-wrap |
| `0x5C` | X x — exchange A with RAM, XOR x into Bu |

**Then single fully-decoded opcodes:**

| Op | Mnemonic | Op | Mnemonic |
|---|---|---|---|
| `0x00` | NOP | `0x2C` | (TAB — see delayed-execution note below) |
| `0x01` | SKISL | `0x2D` | IOS |
| `0x02` | SKNC | `0x2E` | RTSK |
| `0x03` | INT0H | `0x2F` | RT |
| `0x04` | INT1L | `0x70` | SOS |
| `0x05` | RC | `0x71` | ROS |
| `0x06` | SC | `0x72` | IX **(overridden again by MM77LA)** |
| `0x07` | SAG | `0x73` | OX |
| | | `0x74` | XAS |
| | | `0x75` | LXA |
| | | `0x76` | LBA |
| | | `0x77` | COM |
| | | `0x78` | I2C |
| | | `0x79` | XAX |
| | | `0x7A` | XAB |
| | | `0x7B` | IOA **(overridden by MM78LA)** |
| | | `0x7C` | AC |
| | | `0x7D` | ACSK |
| | | `0x7E` | A |
| | | `0x7F` | SKMEA |

**2-byte opcodes** (dispatched when `prev_op` was a `TR` prefix, on
`op & 0xf0`): `0x30`→TR (yet another prefix, enabling 3-byte forms),
`0x40`→SKBEI, `0x60`→SKAEI (unless `op==0x60` exactly, which is illegal),
`0x80-0xB0`→TML, `0xC0-0xF0`→TL.

**3-byte opcodes** (when both `prev_op` and `prev2_op` were TR prefixes), on
`op & 0xf0`: `0x80-0xB0`→TMLB, `0xC0-0xF0`→TLB.

**TAB special case:** `TAB` (opcode `0x2C`) is executed with a **one-opcode
delay** — `mm78_device::execute_one()` ends with:
```cpp
if (m_prev_op == 0x2c) op_tab();
```
i.e. TAB's actual effect (`m_skip_count = m_a + 1; m_a = 0xf;`) fires *after*
the following opcode has already been fetched/executed. Documentation
apparently discourages some usage patterns here too (see MAME's own TODO
comments) — treat this as a known-sharp-edge area to test heavily.

### 5.2 Full opcode semantics (verbatim transcription of MAME's C++)

The following is the complete, exact behavior for every opcode reachable on
MM77LA, organized by tier. **Apply MM76's implementation first, then apply
any MM78 override, then MM78LA override, then MM77LA override — in that
order, per opcode.** Where no override exists at a tier, the previous tier's
behavior stands unchanged.

#### MM76 tier (base implementations — inherited except where overridden below)

```cpp
// --- helpers ---
u8 ram_r() { return m_data->read_byte(m_ram_addr & m_datamask) & 0xf; }
void ram_w(u8 data) { m_data->write_byte(m_ram_addr & m_datamask, data & 0xf); }
void pop_pc() { m_pc = m_stack[0] & m_prgmask; shift stack down by 1 }
void push_pc() { shift stack up by 1; m_stack[0] = m_pc; }

// --- RAM addressing ---
XAB: swap A <-> Bl (low nibble of B); sets m_ram_delay = true
LBA: Bl = A; sets m_ram_delay = true   // NOTE: MM78 overrides this, see below
LB x: B = x, UNLESS prev op was "first executed LB" or prev was EOB
      (coalescing suppression — successive LB/EOB pairs collapse)
EOB x: Bu ^= (x << 4) & datamask, same coalescing-suppression logic as LB,
       but with extra tracking of "first_lb" to allow LB;EOB as a pair

// --- bit manipulation (MM76 combines mem-bit and output-pin ops; MM78 splits them) ---
SB x / SOS: if Bu was 3 -> pin-set (SOS) semantics on D-pin (Bl); else RAM-bit-set
RB x / ROS: mirror of SB, pin-clear / RAM-bit-clear
SKBF x / SKISL: mirror again, skip-if-pin-clear / skip-if-RAM-bit-clear
  (MM76 has "Bu rising is invalid" and "Bu==3 or falling edge -> pin op" logic;
   MM78 REMOVES this ambiguity - see MM78 section, opcodes are cleanly split)

// --- register-register ---
XAS: swap A <-> S (serial shift reg); also updates serial data-out pin
LSA: S = A; also updates serial data-out pin

// --- register-memory ---
L x:    A = RAM[ram_addr]; Bu ^= (x<<4) & 0x30
X x:    swap A <-> RAM[ram_addr]; Bu ^= (x<<4) & 0x30
XDSK x: X x, then Bl = Bl-1 (wrap 4-bit), skip if Bl wrapped to 0xF; ram_delay=true
XNSK x: X x, then Bl = Bl+1 (wrap 4-bit), skip if Bl wrapped to 0x0; ram_delay=true

// --- arithmetic ---
A:     A = (A + RAM[ram_addr]) & 0xF
AC:    A = A + RAM[ram_addr] + carry_in; new_carry = bit4 of sum; A &= 0xF;
       carry update is DELAYED one cycle (m_c_delay = true)
ACSK:  do AC, then skip if NOT new_carry           // MM78 inverts this! see below
ASK:   old_A = A; do A-op; skip if new_A >= old_A  (i.e. skip on NO overflow)
COM:   A = A ^ 0xF  (one's complement)
RC:    carry = 0
SC:    carry = 1
SKNC:  skip if carry_in == 0
LAI x: A = x, UNLESS prev op was a non-suppressed LAI (coalescing suppression)
AISK x:A = (A + x) & 0xF; skip if no overflow (A+x < 0x10)  // MM78 special-cases x==6

// --- ROM addressing ---
RT:     pop_pc()  (consumes one extra cycle() call first)
RTSK:   RT, then set skip=true
T x:    on-page jump. If currently in the top "subroutine page"
        (pc & ~0x7f == prgmask & ~0x7f), clear bit 0x40 of pc first.
        pc = (pc & ~0x3f) | (~x & 0x3f)   -- NOTE: operand is INVERTED (~op)
TL x:   long off-page jump: pc = (~prev_op & 0xF)<<6 | (~op & 0x3F)  -- 2-byte form
TM x:   call on-page. If NOT currently in the subroutine page, push_pc() first
        (i.e. calls FROM the subroutine page do not push - can't nest further)
        pc = (prgmask & ~0x3F) | (~x & 0x3F)
TML x:  long call: push_pc(); pc = (~prev_op & 0xF)<<6 | (~op & 0x3F)
TR:     prefix only, no direct effect (enables extended-opcode dispatch)
NOP:    no-op

// --- comparisons ---
SKMEA:  skip if A == RAM[ram_addr]
SKBEI x:skip if (B & 0xF) == x
SKAEI x:skip if A == (~x & 0xF)   -- operand inverted

// --- I/O (MM76-tier; several of these get overridden at MM78/MM78LA tier) ---
IBM:  A &= (R_input & r_output) >> 4          // input upper R nibble into A (AND-masked)
OB:   r_output = (r_output & 0xF) | (A << 4)  // output A to upper R nibble
IAM:  A &= R_input & r_output                 // input lower R nibble into A
OA:   r_output = (r_output & ~0xF) | A        // output A to lower R nibble
IOS:  start serial shift, m_sclock_count = 9  // MM78LA repurposes this entirely, see below
I1:   A = P_input & 0xF
I2C:  A = (~P_input >> 4) & 0xF
INT1H:skip if INT1 line is high
DIN1: skip if INT1 flip-flop is clear, then SET the flip-flop (test-and-set)
INT0L:skip if INT0 line is low
DIN0: skip if INT0 flip-flop is clear, then SET the flip-flop
SEG1: output (A, carry) through the output PLA to lower R nibble
      out = bitswap<8>(PLA[(carry<<4 | (RAM[ram_addr] & ~A)) ^ 0x1F], 7,5,3,1,0,2,4,6)
SEG2: same PLA lookup, but writes to UPPER R nibble
      (Football II does NOT use SEG1/SEG2 - it uses IX instead, see MM77LA override.
       Included here for completeness / in case any other PPS-4/1 core work reuses this file.)
```

#### MM78 tier — overrides (these REPLACE the MM76 behavior above for MM77LA)

```cpp
LBA:    Bl = A   (same as MM76, but NO ram_delay this time - MM78 removes it)
ACSK:   do AC, then INVERT the skip condition vs MM76 (skip if new_carry, not !new_carry)
AISK x: do AISK, but if operand x == 6 (the "DC" pseudo-op), FORCE skip=false regardless
SB x:   pure RAM-bit-set only (SB/SOS opcodes are cleanly split at this tier - no Bu ambiguity)
RB x:   pure RAM-bit-clear only
SKBF x: pure RAM-bit-test only
SOS:    pure D-pin/interrupt-flag-set. Requires (ram_addr & 0x40)==0 or it's invalid.
        If Bl < d_pins: set D-output pin Bl.
        Else if Bl < 12: set interrupt flip-flop (~Bl & 1) instead (!)
        Else: invalid.
ROS:    mirror of SOS but clearing
SKISL:  mirror of SOS but testing (skip if pin/flag clear)
SAG:    (NEW opcode) sets a flag that forces RAM addr upper bits to 3, next cycle ONLY
LXA:    (NEW) X = A
XAX:    (NEW) swap A <-> X
TLB x:  (NEW, 3-byte) same as TL, but also forces pc bit 0x400 set ("banked" long jump)
TMLB x: (NEW, 3-byte) same as TML, but also forces pc bit 0x400 set
TAB:    (NEW) table-lookup transfer. skip_count = A+1; A = 0xF.
        NOTE the delayed-by-one-opcode execution quirk described in §5.1.
IX:     (NEW at this tier) X = (R_input & r_output) >> 4 & 0xF
        -- MM77LA overrides this AGAIN below; MM78-tier IX is NOT what Football II uses.
OX:     (NEW at this tier) r_output = (r_output & 0xF) | (X << 4)
        -- also overridden again by MM78LA below.
IOA:    (NEW at this tier) exchange: tmp = R_input & r_output & 0xF;
        r_output = (r_output & ~0xF) | A; A = tmp.
        -- also overridden by MM78LA below.
I1SK:   (NEW) A += P_input & 0xF; skip if no overflow
INT0H:  (NEW) skip if INT0 line is HIGH
INT1L:  (NEW) skip if INT1 line is LOW
```

#### MM78LA tier — overrides (further replace the MM78 versions above)

```cpp
IOA:  output (A, carry) to the LOWER half of the R-output pins directly (no PLA):
      mask = (1 << (r_pins/2)) - 1
      r_output = (r_output & ~mask) | (carry<<4 | A)
OX:   same idea, but to the UPPER half of R-output pins:
      r_output = (r_output & mask) | ((carry<<4 | A) << (r_pins/2))
IX:   (MM78LA-specific version — NOT what MM77LA uses, see MM77LA override below)
      routes (A, carry) through the 12-input output PLA, produces up to 28 bits,
      bitswapped and PLA-decoded. Relevant only if you ever port MM78LA (Brain
      Baffler / Horoscope Computer use this chip) — Football II uses MM77LA's
      narrower IX override instead.
IOS:  COMPLETELY repurposed from "start serial shift" to "configure tone generator":
      tone_freq = (tone_freq >> 4) | (A << 4)   // builds an 8-bit freq value across 2 calls
      3-state state machine (m_ios_state, cycles 0->1->2->0):
        when transitioning state 1->2: tone_on = true, reset_tone_count()
        otherwise: tone_on = false
      (i.e. IOS must be executed a specific number of times in sequence to
       actually arm the tone generator - this is a real hardware state machine,
       not just "write a frequency register")
INT0H: repurposed to TOGGLE THE SPEAKER OUTPUT directly (not a skip-test anymore!)
INT1L: op_todo() in MAME — i.e. **UNIMPLEMENTED / UNKNOWN behavior**.
       MAME's driver doesn't need it for mfootb2 to run correctly, but if
       Football II's ROM ever executes this opcode, real hardware behavior
       is undocumented. Flag this as an open risk (see §9).
cycle():  every internal cycle, tone_count increments; if tone_on and
          tone_count == tone_freq, toggle the speaker output and reset the
          counter. This is the actual audio-frequency generation mechanism —
          your Verilog needs an equivalent free-running counter comparator.
```

#### MM77LA tier — final override (this IS Football II's chip)

```cpp
IX:   // "output A to RO pins through PLA (MM77LA)"
      u16 out = ~opla_read(A) & 0x3FF;              // 4-bit A -> 10-bit PLA output, inverted
      r_output = bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8);   // NOTE: this bitswap pattern
                                                            // only rearranges 9 of the
                                                            // 10 bits as literally listed
                                                            // in MAME source (bit 8 position
                                                            // implied/reused) - transcribe
                                                            // the bitswap<10> call exactly,
                                                            // do not "clean up" the pattern.
      write_r(r_output)

r_pins = 10 (set via set_r_pins(10) in mm77la_device::device_start(), overriding
             mm78la's default of 14)
d_pins = 12 (inherited from mm78la_device::device_start(), NOT overridden by mm77la
             -- but the driver's write_d comment only documents DIO0-DIO10 in active
             use for Football II; confirm against the actual ROM which D-pins are real)
```

This `IX` opcode — routing the accumulator through the 10-bit output PLA —
is almost certainly Football II's primary mechanism for driving segments and
the 30-LED field display, given `write_d`/`write_r` handlers in the driver
combine `m_d` and `m_r` into a combined PWM matrix. Get the PLA content and
this bitswap exactly right; it's the crux of display correctness.

### Confirmed field geometry: 9 segments (FB1) vs 10 segments (FB2)

Pixel-verified against a real Football II unit (not just the manual/README
claim): scanning across the field between the two blue end zones shows
**9 internal white divider lines, i.e. 10 field segments** — one more than
Football I's 9-segment field. This is a real, physical display difference to
implement in the renderer, not just a rules/gameplay difference. Confirm this
segment count against the sourced ROM once available (the PWM_DISPLAY
`set_size(10, 11)` call in the driver should correspond to this geometry),
and don't silently reuse FB1's field-rendering constants/geometry assuming
the two are dimensionally identical.

---

## 6. Output PLA

The 317-byte file `mm77la_mfootb2_output.pla` is **not a raw binary blob** —
it's MAME's Berkeley PLA text format (same format used throughout MAME's
`machine/pla.cpp` device for other chips' PLAs). Confirm the exact format by
inspecting a decoded copy once sourced; MAME's `PLA` device config for
MM77LA is:

```cpp
PLA(config, "opla", 4, 10, 16).set_format(pla_device::FMT::BERKELEY);
// 4 inputs (accumulator nibble), 10 outputs (R-pins), 16 terms (product terms)
```

This is a genuine combinational logic table (4-in/10-out, up to 16 minterms)
— in Verilog this becomes either a small ROM/case-statement lookup or a
literal AND-OR plane, your choice, as long as the truth table matches exactly.
Do not attempt to reverse-engineer this logically from gameplay observation —
extract the literal MAME PLA file/values once you have the ROM.

---

## 7. I/O summary (as wired in `mfootb2_state::mfootb2()`)

```cpp
MM77LA(config, m_maincpu, 380000);
m_maincpu->write_d().set(write_d);        // digit-select outputs (12-bit D bus)
m_maincpu->read_d().set_ioport("IN.1");   // difficulty switch read back on D bus
m_maincpu->write_r().set(write_r);        // LED-data outputs (10-bit R bus, via IX's PLA)
m_maincpu->read_p().set_ioport("IN.0");   // 8-bit P port = all the buttons/dpad
m_maincpu->write_spk().set(write_spk);    // speaker level out
```

### write_d (digit/LED-group select, driver comment verbatim):
```
DIO0-DIO2, DIO6-DIO9: digit select
DIO3-DIO5: led select
DIO10: 4th digit decimal point
```

### write_r (LED segment/data, driver comment verbatim):
```
RO01-RO10: led data
```

### Display matrix construction:
```cpp
void mfootb2_state::update_display() {
    m_display->matrix(m_d, (m_r << 1 & 0x700) | (m_d >> 4 & 0x80) | (m_r & 0x7f));
}
// PWM_DISPLAY sized 10 columns x 11 rows
// segmask 0x3c7 -> 7f (standard 7-seg groups)
// segmask 0x002 -> ff (only ONE digit position has a decimal point wired)
// brightness levels: 0.015 / 0.2  (the ball-position LEDs are deliberately brighter
//   than the 7-segment digits - preserve this relative brightness in your bezel/
//   display driver, it's presumably how the original hardware made the "ball" pop
//   visually against the score/clock digits)
```

### Input map (`IN.0`, the P-port, active-high):
```
bit0 (0x01): Score button   (START2)
bit1 (0x02): Status button  (START1)
bit2 (0x04): D-pad Up       (16-way)
bit3 (0x08): D-pad Right    (16-way)
bit4 (0x10): Kick button    (BUTTON2)
bit5 (0x20): Pass button    (BUTTON1)
bit6 (0x40): D-pad Down     (16-way)
bit7 (0x80): D-pad Left     (16-way)
```

### Input map (`IN.1`, read back on the D-bus, bit 0x400 = DIO11):
```
0x000: PRO 1 (difficulty easy)
0x400: PRO 2 (difficulty hard)
```

### Sound:
```cpp
static const double speaker_levels[4] = { 0.0, 1.0, -1.0, 0.0 };
// 2-bit level output, toggling via the tone-generator mechanism in §5.2 (MM78LA tier)
```
Map this to whatever DAC/PWM approach the Analogue Pocket core-template
expects — same class of "simple level-toggle to piezo" sound as Football I,
just gated by an actual programmable frequency divider this time instead of
FB1's presumed direct bit-bang loop (verify against FB1's design doc which
CPU-side sound mechanism it actually used, since this is one concrete
audio-architecture difference worth calling out to Chris/Claude Code
explicitly if it comes up).

---

## 8. Suggested Verilog module breakdown

Mirroring the FB1 project's structure (core-template + custom core logic),
propose:

- `pps41_core.v` — the CPU proper: PC (with LFSR low-6 logic), A/B/X/C/S
  registers, RAM address logic (with ram_delay/SAG one-cycle-late quirks),
  stack (2 levels), skip/skip_count logic, carry-delay logic.
- `pps41_alu.v` (or inline) — arithmetic ops (A/AC/ASK/ACSK/COM/AISK/I1SK).
- `pps41_decode.v` — opcode dispatch, implementing the MM78 opcode map from
  §5.1 (this chip never needs the raw MM76 map — bake the MM78-tier map in
  directly as the "base" for this core, rather than modeling MM76 dispatch
  and then re-dispatching, since MM77LA always executes through the MM78
  decode tree).
- `mm77la_io.v` — the MM78LA/MM77LA-specific I/O overrides: the tone
  generator (frequency counter + IOS 3-state arming sequence + INT0H
  speaker-toggle), and the IX-opcode PLA lookup + bitswap.
- `output_pla.v` — the 4-in/10-out output PLA as a lookup table (from the
  sourced `.pla` file's decoded truth table).
- `display_mux.v` — reconstructs the 10x11 PWM display matrix from the D/R
  bus outputs per the `update_display()` formula in §7, drives the
  Pocket's video output the way FB1's core presumably drove its bezel/LED
  representation.
- Test harness: reuse FB1's Verilator sim structure (`make sim`), but the
  golden-model comparison target should be a from-scratch Python/C
  transcription of exactly the opcode semantics in §5.2 (or, more directly,
  MAME itself run in `-debug` mode single-stepping the same ROM) — do NOT
  reuse any part of FB1's B6100 golden model, it's architecturally unrelated.

---

## 9. Open risks / unknowns (flag these explicitly, don't silently guess)

1. **No confirmed Rockwell datasheet for MM77LA/B8000 exists.** hap's own
   comment: *"no known documentation exists for MM77LA, mcu name is guessed
   (maybe it was designed in collaboration with Mattel, and later evolved
   into MM78LA)."* MAME's implementation is itself inferred from ROM
   behavior + die comparison with MM78LA, not from a datasheet. Treat MAME
   as "best available reverse-engineered reference," not gospel silicon
   truth — same epistemic status FB1's B6100 work started from, before
   real-hardware verification.
2. **`INT1L` on MM78LA/MM77LA is `op_todo()` in MAME** — completely
   unimplemented, unknown real effect. If disassembly of the actual
   Football II ROM shows this opcode is executed, this is a real gap we'd
   need to either reverse-engineer ourselves (via real-hardware testing) or
   accept as a known limitation.

   **RESOLVED (2026-08-04): not a gap for this game.** The opcode byte
   `0x04` occurs **zero** times in the ROM image, and the model's
   `int1l_hit` flag never fires across a 4,000,000-cycle run including a
   full kickoff. Our flag-only no-op is therefore unreachable here. Worth
   implementing properly for completeness (MAME's base tier does
   `m_skip = !m_int_line[1]`), but it cannot affect this title.
3. **Pinout for B8000 explicitly marked "might not be accurate"** in the
   MAME source pinout diagram comment. Irrelevant for a from-ROM emulation
   core (we don't care about physical pin numbers, only logical D/R/P bus
   semantics), but flag it if this project ever extends to physical
   reproduction hardware.
4. **Clock frequency (380kHz) is MAME's approximation** based on a
   resistor-value formula, not a measured/documented crystal-accurate value.
   Since this is an RC oscillator on real hardware (not crystal-locked), the
   "correct" emulated clock is inherently a target range, not one true
   number — same category of issue as FB1's B6100 clock approximation.
5. **ROM sourcing**: same posture as FB1 — this project cannot include or
   distribute a dump. The CRC32/SHA1 in §1 are for the end user to verify
   their own legally-obtained dump against, not for us to go find and
   redistribute.
6. **D-pin count discrepancy**: `mm78la_device` sets 12 D-pins by default,
   and `mm77la_device` does not override this — but the driver's `write_d`
   comment only documents meaningful use of DIO0-DIO10 (11 pins). Confirm
   against actual ROM behavior whether DIO11 carries real signal or is
   unused/tied off, before assuming an 11-bit vs 12-bit D-bus width in the
   RTL.

   **RESOLVED (2026-08-04): the D bus is genuinely 12 bits, and DIO11 is
   the meaningful one.** Over a 4,000,000-cycle run the union of all
   `d_output` values is `0xBFF` — bits 0-9 and **bit 11** are driven, and
   **bit 10 is never driven at all**.

   Note this contradicts the MAME driver's own comment ("DIO10: 4th digit
   DP"), but agrees with its *code*: `mfootb2_state::update_display()` folds
   the DP in via `(m_d >> 4 & 0x80)`, which selects `m_d` bit **11**, not
   bit 10. The code is authoritative and our display mux already matches it
   (see the `display_dp_via_d11.bin` regression vector). Treat the driver's
   pin-naming comments with suspicion; read the expressions.

---

## 10. Cross-reference to Football I core, for context

| | Football I (existing) | Football II (this spec) |
|---|---|---|
| CPU | Rockwell B6100 (rw5000 family) | Rockwell MM77LA (PPS-4/1 family, B8000 die) |
| MAME driver | `handheld/hh_rw5000.cpp` | `handheld/hh_pps41.cpp` |
| ROM size | 896 bytes | 1536 bytes program + 317-byte output PLA |
| Display | Simple LED field, no digits | 7 seven-seg digits + 30 discrete LEDs, PWM-muxed |
| Sound | Direct bit-bang beep (presumed) | Programmable tone generator w/ frequency register |
| Gameplay additions | — | Backward movement, passing, 10-yard field |
| Reusable from FB1 project | Build harness, core-template vendoring, packaging/dist convention, bezel pipeline approach | (CPU core, decode logic, and golden-model tooling are NOT reusable — new architecture) |

### Bezel/overlay: look at FB1's for inspiration, don't lift it directly

FB1's actual shipped overlay (`assets/bezel/overlay_400x360.png`) is worth
opening before starting FBII's bezel work — but as a reference for approach,
not as source material to reuse or extend:

- It's structured as a branding/frame image (logo text, color bands) wrapped
  around a single solid opaque black rectangle — the live field pattern
  (turf, end zones, yard dividers) is rendered by the core itself at
  runtime, not drawn into the static bezel PNG.
- FBII's bezel should very likely follow the same structural pattern
  (frame art + placeholder rect for the core's live output), but the frame
  art itself can't be copied wholesale: FB1's case is a white oval body,
  FBII's real unit is a green/black rounded-rectangle body — different
  physical shape to draw.
- The placeholder rect's position/size also isn't a given match — confirm it
  against FBII's actual PWM output geometry (10-segment field, 7-segment
  digit row, per §7) rather than assuming FB1's rectangle dimensions apply.
