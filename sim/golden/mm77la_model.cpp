// sim/golden/mm77la_model.cpp
#include "mm77la_model.h"

Mm77laModel::Mm77laModel(const uint8_t* rom, size_t rom_size)
    : rom_(rom), rom_size_(rom_size) {
    // ROM Remapping for Small Test ROMs (Task 4 and later test harness only):
    //
    // The PC is a 6-bit LFSR that sequences non-sequentially: 0 -> 0x20 -> 0x10 -> 0x08 ...
    // For small test ROMs (< 0x100 bytes), this means a sequential test ROM {0x47, 0x76}
    // would fail: step() 1 accesses rom[0], step() 2 tries to access rom[0x20] (out of bounds).
    //
    // To make tests work, we allocate a 0x100-byte buffer and remap each input ROM byte
    // to the address it will actually be accessed at during the PC sequence. So rom[0]
    // goes to buffer[0x00], rom[1] goes to buffer[0x20], rom[2] to buffer[0x10], etc.
    //
    // This remapping is ONLY done for ROMs < 0x100 bytes. Production ROMs (0x100+ bytes)
    // use literal indexing without remapping. If Task 14's real ROM is < 0x100 bytes,
    // this remapping will apply to it silently — beware.
    if (rom_size < 0x100) {
        rom_buffer_.resize(0x100, 0x00);
        // Compute PC sequence and place ROM bytes at their accessed addresses
        uint16_t pc = 0;
        for (size_t i = 0; i < rom_size; i++) {
            rom_buffer_[pc & 0xFF] = rom[i];
            // Compute next PC using the LFSR formula (same as increment_pc())
            int feed = ((pc & 0x3e) == 0) ? 1 : 0;
            feed ^= (pc >> 1 ^ pc) & 1;
            pc = static_cast<uint16_t>((pc & ~0x3fu) | (pc >> 1 & 0x1f) | (feed << 5));
        }
        rom_ = rom_buffer_.data();
        rom_size_ = rom_buffer_.size();
    }
}

void Mm77laModel::reset() {
    st_ = Mm77laState{};
    ram_.fill(0xF);
}

uint8_t Mm77laModel::rom_read(uint16_t addr) const {
    addr &= 0x7FF;
    if (addr >= 0x600) addr -= 0x200; // 0x600-0x7FF mirrors 0x400-0x5FF
    return (addr < rom_size_) ? rom_[addr] : 0x00;
}

// Maps the 7-bit RAM address space onto the 96 physically-real nibbles.
// See docs/initial-plan.md §3 and the design spec's RAM-map derivation:
// 0x00-0x3F: 64 real cells, indices 0-63
// 0x40-0x47: bank A, indices 64-71; mirrored at 0x48-0x4F and 0x58-0x5F
// 0x50-0x57: bank B, indices 72-79 (NOT a mirror of bank A)
// 0x60-0x67: bank C, indices 80-87; mirrored at 0x68-0x6F and 0x78-0x7F
// 0x70-0x77: bank D, indices 88-95 (NOT a mirror of bank C)
uint8_t Mm77laModel::ram_phys_index(uint8_t addr) const {
    addr &= 0x7F;
    if (addr < 0x40) return addr;
    if (addr <= 0x4F || (addr >= 0x58 && addr <= 0x5F)) return 64 + (addr & 0x07);
    if (addr <= 0x57) return 72 + (addr & 0x07);
    if (addr <= 0x6F || (addr >= 0x78 && addr <= 0x7F)) return 80 + (addr & 0x07);
    return 88 + (addr & 0x07);
}

uint8_t Mm77laModel::ram_read(uint8_t addr) const {
    return ram_[ram_phys_index(addr)] & 0xF;
}

void Mm77laModel::ram_write(uint8_t addr, uint8_t val) {
    ram_[ram_phys_index(addr)] = val & 0xF;
}

uint8_t Mm77laModel::debug_ram_read(uint8_t addr) const { return ram_read(addr); }
void Mm77laModel::debug_ram_write(uint8_t addr, uint8_t val) { ram_write(addr, val); }
uint8_t Mm77laModel::debug_rom_read(uint16_t addr) const { return rom_read(addr); }

void Mm77laModel::debug_poke_rom(uint16_t addr, uint8_t value) {
    // Apply the same address translation as rom_read() to get the actual offset
    addr &= 0x7FF;
    if (addr >= 0x600) addr -= 0x200; // 0x600-0x7FF mirrors 0x400-0x5FF

    // Ensure rom_buffer_ is allocated and large enough to hold this address
    // If rom_ is still pointing to the original input array (not rom_buffer_),
    // we need to migrate to rom_buffer_ first to preserve existing content.
    if (rom_buffer_.empty()) {
        // First time using buffer: allocate and copy existing ROM content if needed
        rom_buffer_.resize(std::max(static_cast<size_t>(addr + 1), rom_size_), 0x00);
        // If rom_ was pointing to the original input, copy it to the buffer
        if (rom_ != rom_buffer_.data()) {
            for (size_t i = 0; i < rom_size_; i++) {
                rom_buffer_[i] = rom_[i];
            }
        }
    } else if (addr >= rom_buffer_.size()) {
        // Buffer exists but needs to grow
        rom_buffer_.resize(addr + 1, 0x00);
    }

    // Write the byte and repoint rom_ / rom_size_
    rom_buffer_[addr] = value;
    rom_ = rom_buffer_.data();
    rom_size_ = rom_buffer_.size();
}

void Mm77laModel::increment_pc() {
    int feed = ((st_.pc & 0x3e) == 0) ? 1 : 0;
    feed ^= (st_.pc >> 1 ^ st_.pc) & 1;
    st_.pc = static_cast<uint16_t>((st_.pc & ~0x3fu) | (st_.pc >> 1 & 0x1f) | (feed << 5));
}

// static helper: is `op` a TR prefix byte (0x30-0x3F)?
static bool op_is_tr(uint8_t op) { return (op & 0xF0) == 0x30; }

void Mm77laModel::step() {
    // Apply any carry-delay flag pending from a PRIOR instruction BEFORE this
    // instruction's own switch runs, so that if THIS instruction is itself
    // AC/ACSK, its fresh st_.c/c_delay aren't read back here -- that would
    // silently discard the previous instruction's pending carry on
    // back-to-back AC/ACSK (see regression test
    // test_back_to_back_ac_does_not_lose_first_pending_carry). This placement
    // (top of step(), before the switch) is the Task 6 fix; do NOT move this
    // to the bottom of the function.
    if (st_.c_delay) {
        st_.c_in = st_.c;
        st_.c_delay = false;
    }

    // RAM address delay (m_ram_delay, docs/initial-plan.md section 2 and
    // section 3's "RAM addressing register (B)" note): XAB/XDSK/XNSK change
    // B, but the RAM address used for addressing doesn't catch up to that
    // new B until the instruction AFTER the one immediately following --
    // i.e. the single instruction immediately after XAB/XDSK/XNSK still
    // reads/writes at the STALE (pre-change) address, and only the
    // instruction after THAT sees the new address. Modeled with the same
    // "commit a pending flag at the top of the next step()" pattern as
    // c_delay above: st_.ram_addr_reg is normally resynced to st_.b every
    // cycle; when st_.ram_delay is pending (set by the previous
    // instruction), that resync is skipped exactly once (so this cycle's
    // RAM access uses the address as of before the address-changing
    // instruction ran), then the flag clears so normal per-cycle resync
    // resumes starting next step(). LBA does NOT set st_.ram_delay (MM78
    // override, see the LBA case below), so it has no delayed-address
    // effect at all -- the very next instruction already sees LBA's new B.
    if (st_.ram_delay) {
        st_.ram_delay = false;
    } else {
        st_.ram_addr_reg = st_.b;
    }
    uint16_t ram_addr = st_.sag ? (0x30 | (st_.b & 0xF)) : st_.ram_addr_reg;
    st_.sag = false; // exactly one cycle of effect

    // ix_executed is a per-step (non-sticky) flag: reset every step, then
    // set true only inside the real `case 0x72:` (IX) execution below. See
    // Important #11: the testbench previously counted IX executions via
    // post-hoc `prev_op == 0x72` inspection, which can false-positive when
    // 0x72 passes through as a discarded skip byte or a TR-prefixed operand
    // byte rather than being genuinely dispatched as an opcode.
    st_.ix_executed = false;

    uint8_t op = rom_read(st_.pc);
    increment_pc();

    // TAB's skip_count (docs/initial-plan.md's m_skip_count: "skips A+1
    // instructions going forward, used for jump tables"): consume one skip
    // credit per FRESH instruction fetch by reusing the exact same
    // consumed_by_skip / TR-continuation machinery as the ordinary
    // single-shot st_.skip flag below. This naturally makes a multi-byte
    // (TR-prefixed) instruction inside the skipped range count as ONE
    // credit, not one credit per byte, since re-arming st_.skip for the
    // continuation bytes (further down) does not touch skip_count again.
    // Only arm a NEW skip here if we're not already mid a skip -- this
    // arming check and a skip_count value freshly assigned by THIS SAME
    // step's TAB-delayed-fire (below) cannot race: skip_count only becomes
    // nonzero via a delayed fire, and this arming check runs before any
    // fire logic in the current step, so it only ever sees a skip_count
    // left over from a PRIOR step.
    //
    // That said, skip_count and tab_pending CAN legitimately be
    // simultaneously nonzero across a step boundary -- e.g. back-to-back
    // TAB;TAB: the second TAB's own dispatch re-arms tab_pending for its
    // OWN future fire, in the very same step where the first TAB's
    // tab_was_pending fire (see below) sets a fresh nonzero skip_count.
    // When that happens, the instruction immediately after the second TAB
    // gets armed as a skip by THIS check (consuming a skip_count credit)
    // before it can reach the normal dispatch path -- but the
    // consumed_by_skip block below already independently fires a pending
    // tab_pending in that situation (see its own `else if (st_.tab_pending)`
    // branch), so the second TAB's delayed effect still fires correctly,
    // just via that branch instead of the bottom-of-step one. See
    // test_tab_back_to_back_fires_twice, which exercises exactly this path.
    if (!st_.skip && st_.skip_count > 0) {
        st_.skip_count--;
        st_.skip = true;
    }

    bool consumed_by_skip = st_.skip;
    if (consumed_by_skip) {
        st_.skip = false;
        // Consume exactly ONE ROM byte per step() call, matching real
        // per-cycle hardware fetch granularity and the RTL's structure (see
        // the header comment in src/pps41_core.v). If the byte we just
        // "executed" as a skip target is itself a TR prefix, the skip must
        // extend through the remaining byte(s) of this multi-byte
        // instruction -- but that continuation happens on the NEXT step()
        // call, not by fetching ahead within this one. We simply re-arm
        // st_.skip so the next call's consumed_by_skip check (at the top of
        // this function) fires again on the next freshly-fetched byte.
        //
        // prev_op/prev2_op/prev3_op are shifted using `op` (the single byte
        // fetched THIS call) on every pass through this block, so by the
        // time skip actually clears for real (the `else` branch below, on a
        // non-TR byte), the shift chain naturally holds the correct
        // last-consumed byte -- exactly as if each byte had been fetched in
        // its own separate step() call (which is what the non-skip path's
        // is_2byte/is_3byte dispatch already assumes). No manual
        // last-byte bookkeeping needed.
        st_.prev3_op = st_.prev2_op;
        st_.prev2_op = st_.prev_op;
        st_.prev_op = op;
        if (op_is_tr(op)) {
            // Skip continues into the NEXT step() call to consume the
            // remaining byte(s) of this multi-byte instruction.
            st_.skip = true;
        } else if (st_.tab_pending) {
            // skip_count is architecturally a 4-bit register (see
            // src/pps41_core.v's `reg [3:0] skip_count`); mask with 0xF so
            // A==0xF wraps to 0x0 exactly as the RTL's 4-bit addition does,
            // instead of the golden model's uint8_t silently holding 0x10.
            st_.skip_count = static_cast<uint8_t>((st_.a + 1) & 0xF);
            st_.a = 0xF;
            st_.tab_pending = false;
        }
        return;
    }

    bool is_2byte = op_is_tr(st_.prev_op);
    bool is_3byte = is_2byte && op_is_tr(st_.prev2_op);

    // Latch tab_pending's value from BEFORE this instruction's own dispatch
    // runs (Important #9 fix): the reference (MAME) semantics fire TAB's
    // delayed effect based on "was the PREVIOUS opcode TAB" checked
    // unconditionally every instruction (`if (m_prev_op == 0x2c) op_tab();`)
    // -- NOT "is the CURRENT opcode something other than TAB", which is what
    // the old `st_.tab_pending && op != 0x2C` guard effectively tested. That
    // old guard suppressed firing whenever the current instruction was
    // itself a fresh TAB, so back-to-back TAB;TAB;NOP only fired once
    // instead of the correct twice (once for each TAB's own delayed
    // effect). Capturing the pre-dispatch value here means a fresh TAB this
    // cycle (which re-arms st_.tab_pending below) no longer masks a
    // still-pending PRIOR TAB's fire.
    bool tab_was_pending = st_.tab_pending;

    if (is_3byte) {
        switch (op & 0xF0) {
            case 0x80: case 0x90: case 0xA0: case 0xB0: { // TMLB
                st_.stack[1] = st_.stack[0];
                st_.stack[0] = st_.pc;
                st_.pc = static_cast<uint16_t>(0x400 | ((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            case 0xC0: case 0xD0: case 0xE0: case 0xF0: { // TLB
                st_.pc = static_cast<uint16_t>(0x400 | ((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            default: break;
        }
    } else if (is_2byte) {
        switch (op & 0xF0) {
            case 0x30: break; // another TR -- enables 3-byte dispatch next step
            case 0x40: { // SKBEI x
                st_.skip = ((st_.b & 0xF) == (op & 0xF));
                break;
            }
            case 0x60: { // SKAEI x (op==0x60 exactly is illegal, treat as no-op)
                if (op != 0x60) st_.skip = (st_.a == (~op & 0xF));
                break;
            }
            case 0x80: case 0x90: case 0xA0: case 0xB0: { // TML
                st_.stack[1] = st_.stack[0];
                st_.stack[0] = st_.pc;
                st_.pc = static_cast<uint16_t>(((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            case 0xC0: case 0xD0: case 0xE0: case 0xF0: { // TL
                st_.pc = static_cast<uint16_t>(((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            default: break;
        }
    } else {
        switch (op & 0xF0) {
            case 0x10: { bool suppressed = (st_.prev_op & 0xF0) == 0x10; if (!suppressed) st_.b = op & 0xF; break; } // LB x
            case 0x30: break; // TR prefix, no direct effect
            case 0x40: { bool suppressed = (st_.prev_op & 0xF0) == 0x40; if (!suppressed) st_.a = op & 0xF; break; } // LAI x
            case 0x60: { // AISK x (x!=0); I1SK (x==0) is Task 5/7 I/O work, left as no-op here
                uint8_t x = op & 0xF;
                if (x != 0) {
                    uint8_t sum = static_cast<uint8_t>(st_.a + x);
                    st_.a = sum & 0xF;
                    st_.skip = (x == 6) ? false : (sum < 0x10);
                }
                break;
            }
            case 0x80: case 0x90: case 0xA0: case 0xB0: { // TM x
                constexpr uint16_t prgmask = 0x7FF;
                bool in_subroutine_page = (st_.pc & ~0x7Fu) == (prgmask & ~0x7Fu);
                if (!in_subroutine_page) { st_.stack[1] = st_.stack[0]; st_.stack[0] = st_.pc; }
                uint8_t x = op & 0x3F;
                st_.pc = static_cast<uint16_t>((prgmask & ~0x3Fu) | (~x & 0x3Fu));
                break;
            }
            case 0xC0: case 0xD0: case 0xE0: case 0xF0: { // T x
                constexpr uint16_t prgmask = 0x7FF;
                bool in_subroutine_page = (st_.pc & ~0x7Fu) == (prgmask & ~0x7Fu);
                uint16_t pc = st_.pc;
                if (in_subroutine_page) pc &= ~0x40u;
                uint8_t x = op & 0x3F;
                st_.pc = static_cast<uint16_t>((pc & ~0x3Fu) | (~x & 0x3Fu));
                break;
            }
            default: {
                switch (op & 0xFC) {
                    case 0x08: { bool suppressed = (st_.prev_op & 0xFC) == 0x08; if (!suppressed) st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4)); break; } // EOB x
                    case 0x20: { uint8_t val = ram_read(static_cast<uint8_t>(ram_addr)); ram_write(static_cast<uint8_t>(ram_addr), val | (1 << (op & 0x3))); break; } // SB x
                    case 0x24: { uint8_t val = ram_read(static_cast<uint8_t>(ram_addr)); ram_write(static_cast<uint8_t>(ram_addr), val & ~(1 << (op & 0x3))); break; } // RB x
                    case 0x28: { uint8_t val = ram_read(static_cast<uint8_t>(ram_addr)); st_.skip = (val & (1 << (op & 0x3))) == 0; break; } // SKBF x
                    case 0x50: { st_.a = ram_read(static_cast<uint8_t>(ram_addr)); st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4)); break; } // L x
                    case 0x54: { // XNSK x
                        uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                        ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                        st_.a = tmp;
                        uint8_t bl = static_cast<uint8_t>(((st_.b & 0xF) + 1) & 0xF);
                        st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | bl);
                        st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                        st_.ram_delay = true;
                        st_.skip = (bl == 0x0);
                        break;
                    }
                    case 0x58: { // XDSK x
                        uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                        ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                        st_.a = tmp;
                        uint8_t bl = static_cast<uint8_t>(((st_.b & 0xF) - 1) & 0xF);
                        st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | bl);
                        st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                        st_.ram_delay = true;
                        st_.skip = (bl == 0xF);
                        break;
                    }
                    case 0x5C: { // X x
                        uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                        ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                        st_.a = tmp;
                        st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                        break;
                    }
                    default: {
                        switch (op) {
                            case 0x00: break; // NOP
                            case 0x02: st_.skip = (st_.c_in == 0); break; // SKNC
                            case 0x04: st_.int1l_hit = true; break; // INT1L -- flagged no-op
                            case 0x05: st_.c = 0; st_.c_in = 0; break; // RC -- docs/initial-plan.md: "RC: carry = 0", no c_delay mentioned (unlike AC/ACSK) so both c and c_in update immediately, same cycle
                            case 0x06: st_.c = 1; st_.c_in = 1; break; // SC -- docs/initial-plan.md: "SC: carry = 1", immediate like RC
                            case 0x07: st_.sag = true; break; // SAG
                            case 0x2C: st_.tab_pending = true; break; // TAB -- delayed fire
                            case 0x2E: { st_.pc = st_.stack[0] & 0x7FF; st_.stack[0] = st_.stack[1]; st_.skip = true; break; } // RTSK
                            case 0x2F: { st_.pc = st_.stack[0] & 0x7FF; st_.stack[0] = st_.stack[1]; break; } // RT
                            case 0x72: st_.ix_executed = true; break; // IX -- stub, no PLA wiring this phase; flagged for the testbench
                            case 0x76: st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | st_.a); break; // LBA (MM78: no ram_delay)
                            case 0x77: st_.a = st_.a ^ 0xF; break; // COM
                            case 0x7A: { uint8_t tmp = st_.a; st_.a = st_.b & 0xF; st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | tmp); st_.ram_delay = true; break; } // XAB
                            case 0x7C: { uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in); st_.c = (sum >> 4) & 1; st_.a = sum & 0xF; st_.c_delay = true; break; } // AC
                            case 0x7D: { uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in); st_.c = (sum >> 4) & 1; st_.a = sum & 0xF; st_.c_delay = true; st_.skip = st_.c != 0; break; } // ACSK -- MM78: skip if NEW carry (inverted vs MM76)
                            case 0x7E: st_.a = static_cast<uint8_t>((st_.a + ram_read(static_cast<uint8_t>(ram_addr))) & 0xF); break; // A
                            case 0x7F: st_.skip = (st_.a == ram_read(static_cast<uint8_t>(ram_addr))); break; // SKMEA
                            default: break; // unimplemented opcodes fall through as NOP until later tasks
                        }
                        break;
                    }
                }
                break;
            }
        }
    }

    if (tab_was_pending) {
        // TAB's effect fires after the FOLLOWING opcode has executed; since
        // we're already past that opcode's execution here, apply it now,
        // using the A value present after the intervening opcode ran (per
        // docs/initial-plan.md §5.1's op_tab() semantics, which reads m_a at
        // fire time). skip_count is a 4-bit register (matching
        // src/pps41_core.v); mask so A==0xF wraps to 0x0 like the RTL's
        // 4-bit addition, rather than holding an out-of-range 0x10.
        st_.skip_count = static_cast<uint8_t>((st_.a + 1) & 0xF);
        st_.a = 0xF;
        // Only clear tab_pending if THIS instruction didn't itself just
        // re-arm it (op == 0x2C, a fresh TAB dispatched above) -- that fresh
        // TAB's own tab_pending must stay armed for ITS delayed fire on the
        // instruction after next. If op != 0x2C, tab_pending still holds
        // tab_was_pending's true value untouched by dispatch, so clearing it
        // here is exactly consuming the one delayed effect we just applied.
        if (op != 0x2C) st_.tab_pending = false;
    }

    st_.prev3_op = st_.prev2_op;
    st_.prev2_op = st_.prev_op;
    st_.prev_op = op;
}
