# sim/Makefile
VERILATOR := verilator
CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

.PHONY: lint-core golden-test golden-trace decode-test alu-test core-pc-test core-ram-test core-test core-rom-lockstep-test tone-stuck-fuzz vectors-test display-vectors-long-test test clean
.PHONY: gameplay-test stimulus-test display-parity-test pro-switch-test core-top-test debug-probe-test mame-parity-test opla-test opla-rtl-test tone-test tone-rtl-test io-test io-rtl-test display-mux-test display-mux-rtl-test display-pwm-test display-pwm-rtl-test ce-gen-test rom-loader-test audio-gen-test video-renderer-test field-rom-test label-rom-test screenshot startup-state-test

lint-core:
	$(VERILATOR) --lint-only -Wall --Wno-UNUSEDSIGNAL -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v

opla-test:
	python3 ../tools/gen_opla_table.py ../development-assets/mm77la_mfootb2_output.pla ../src/pps41_opla_table.vh golden/mm77la_opla_table.h
	$(CXX) $(CXXFLAGS) golden/mm77la_opla.cpp golden/mm77la_opla_test.cpp -o /tmp/mm77la_opla_test
	/tmp/mm77la_opla_test

opla-rtl-test:
	$(VERILATOR) --cc -I../src ../src/pps41_opla.v --exe pps41_opla_tb.cpp \
		--Mdir obj_dir_opla -Wall -Wno-WIDTH
	$(MAKE) -C obj_dir_opla -f Vpps41_opla.mk
	./obj_dir_opla/Vpps41_opla

tone-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_tone.cpp golden/mm77la_tone_test.cpp -o /tmp/mm77la_tone_test
	/tmp/mm77la_tone_test

tone-rtl-test:
	$(VERILATOR) --cc ../src/pps41_tone.v --exe pps41_tone_tb.cpp \
		--Mdir obj_dir_tone -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_tone -f Vpps41_tone.mk
	./obj_dir_tone/Vpps41_tone

io-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_io.cpp golden/mm77la_io_test.cpp -o /tmp/mm77la_io_test
	/tmp/mm77la_io_test

display-mux-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_display_mux.cpp golden/mm77la_display_mux_test.cpp -o /tmp/mm77la_display_mux_test
	/tmp/mm77la_display_mux_test

display-pwm-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_display_pwm.cpp golden/mm77la_display_pwm_test.cpp -o /tmp/mm77la_display_pwm_test
	/tmp/mm77la_display_pwm_test

io-rtl-test:
	$(VERILATOR) --cc ../src/pps41_io.v --exe pps41_io_tb.cpp \
		--Mdir obj_dir_io -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_io -f Vpps41_io.mk
	./obj_dir_io/Vpps41_io

display-mux-rtl-test:
	$(VERILATOR) --cc ../src/pps41_display_mux.v --exe pps41_display_mux_tb.cpp \
		--Mdir obj_dir_display_mux -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_display_mux -f Vpps41_display_mux.mk
	./obj_dir_display_mux/Vpps41_display_mux

display-pwm-rtl-test:
	$(VERILATOR) --cc ../src/pps41_display_pwm.v --exe pps41_display_pwm_tb.cpp \
		--Mdir obj_dir_display_pwm -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_display_pwm -f Vpps41_display_pwm.mk
	./obj_dir_display_pwm/Vpps41_display_pwm

ce-gen-test:
	$(VERILATOR) --cc ../src/ce_gen.v --exe ce_gen_tb.cpp \
		--Mdir obj_dir_ce_gen -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_ce_gen -f Vce_gen.mk
	./obj_dir_ce_gen/Vce_gen

rom-loader-test:
	$(VERILATOR) --cc ../src/rom_loader.v --exe rom_loader_tb.cpp \
		--Mdir obj_dir_rom_loader -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_rom_loader -f Vrom_loader.mk
	./obj_dir_rom_loader/Vrom_loader

audio-gen-test:
	$(VERILATOR) --cc ../src/audio_gen.v --exe audio_gen_tb.cpp \
		--Mdir obj_dir_audio_gen -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_audio_gen -f Vaudio_gen.mk
	./obj_dir_audio_gen/Vaudio_gen

# video_renderer.v directly instantiates field_rom/label_rom in the
# Verilog hierarchy (unlike core-test's pps41_display_mux/pwm, which are
# only linked in at the C++ testbench level -- see the comment on
# core-test above), so Verilator needs their actual sources at
# elaboration time, not just prebuilt archives.
video-renderer-test:
	$(VERILATOR) --cc ../src/video_renderer.v ../src/field_rom.v ../src/label_rom.v --exe video_renderer_tb.cpp \
		--Mdir obj_dir_video_renderer -Wall --Wno-UNUSEDSIGNAL --Wno-WIDTH
	$(MAKE) -C obj_dir_video_renderer -f Vvideo_renderer.mk
	cd ../src && ../sim/obj_dir_video_renderer/Vvideo_renderer

field-rom-test:
	$(VERILATOR) --cc ../src/field_rom.v --exe field_rom_tb.cpp \
		--Mdir obj_dir_field_rom -Wall --Wno-UNUSEDSIGNAL --Wno-WIDTH
	$(MAKE) -C obj_dir_field_rom -f Vfield_rom.mk
	cd ../src && ../sim/obj_dir_field_rom/Vfield_rom

label-rom-test:
	$(VERILATOR) --cc ../src/label_rom.v --exe label_rom_tb.cpp \
		--Mdir obj_dir_label_rom -Wall --Wno-UNUSEDSIGNAL --Wno-WIDTH
	$(MAKE) -C obj_dir_label_rom -f Vlabel_rom.mk
	cd ../src && ../sim/obj_dir_label_rom/Vlabel_rom

# Debugging/verification aid, not part of `test` -- see screenshot_tb.cpp.
# Usage: make screenshot ROM=path/to.bin CYCLES=20000 OUT=/tmp/frame.ppm [STIM=stimulus/foo.txt]
ROM     ?= ../dist/Assets/mattel_fb_ii/common/b8000-12.bin
CYCLES  ?= 20000
OUT     ?= /tmp/screenshot.ppm
screenshot:
	$(VERILATOR) --cc ../src/pps41_display_mux.v --Mdir obj_dir_core_dmux -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dmux -f Vpps41_display_mux.mk Vpps41_display_mux__ALL.a
	$(VERILATOR) --cc ../src/pps41_display_pwm.v --Mdir obj_dir_core_dpwm -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dpwm -f Vpps41_display_pwm.mk Vpps41_display_pwm__ALL.a
	$(VERILATOR) --cc ../src/video_renderer.v ../src/field_rom.v ../src/label_rom.v --Mdir obj_dir_core_render -Wall --Wno-UNUSEDSIGNAL --Wno-WIDTH
	$(MAKE) -C obj_dir_core_render -f Vvideo_renderer.mk Vvideo_renderer__ALL.a
	$(VERILATOR) --cc -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v \
		--exe screenshot_tb.cpp \
		--Mdir obj_dir_screenshot -Wall --Wno-UNUSEDSIGNAL -Wno-WIDTH \
		-CFLAGS "-I.. -I../obj_dir_core_dmux -I../obj_dir_core_dpwm -I../obj_dir_core_render" \
		-LDFLAGS "../obj_dir_core_dmux/Vpps41_display_mux__ALL.a ../obj_dir_core_dpwm/Vpps41_display_pwm__ALL.a ../obj_dir_core_render/Vvideo_renderer__ALL.a"
	$(MAKE) -C obj_dir_screenshot -f Vpps41_core.mk
	cd ../src && ../sim/obj_dir_screenshot/Vpps41_core "$(ROM)" "$(CYCLES)" "$(OUT)" $(STIM)

# Regression check for the real, MAME-confirmed startup display state
# (see startup_state_tb.cpp). Requires the user-supplied ROM, same as
# `screenshot` -- not part of the default `test` target since CI/other
# clones won't have it.
startup-state-test:
	$(VERILATOR) --cc ../src/pps41_display_mux.v --Mdir obj_dir_core_dmux -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dmux -f Vpps41_display_mux.mk Vpps41_display_mux__ALL.a
	$(VERILATOR) --cc ../src/pps41_display_pwm.v --Mdir obj_dir_core_dpwm -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dpwm -f Vpps41_display_pwm.mk Vpps41_display_pwm__ALL.a
	$(VERILATOR) --cc -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v \
		--exe startup_state_tb.cpp \
		--Mdir obj_dir_startup_state -Wall --Wno-UNUSEDSIGNAL -Wno-WIDTH \
		-CFLAGS "-I.. -I../obj_dir_core_dmux -I../obj_dir_core_dpwm" \
		-LDFLAGS "../obj_dir_core_dmux/Vpps41_display_mux__ALL.a ../obj_dir_core_dpwm/Vpps41_display_pwm__ALL.a"
	$(MAKE) -C obj_dir_startup_state -f Vpps41_core.mk
	./obj_dir_startup_state/Vpps41_core "$(ROM)"

# Integration tests for the APF glue layer (core_top.v), which had no
# coverage at all until now -- and is where the reset-before-ROM and
# p_input CDC bugs both lived. Altera megafunctions are replaced by the
# behavioural stubs in stubs/. ROM_WAIT_MAX_P is shrunk so the timeout
# path is reachable without simulating 55 million clocks.
CORE_TOP_SRCS := ../src/fpga/core/core_top.v ../src/fpga/core/core_bridge_cmd.v \
	../src/fpga/apf/common.v stubs/mf_pllbase.v stubs/mf_datatable.v \
	../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v \
	../src/pps41_tone.v ../src/pps41_io.v ../src/pps41_display_mux.v \
	../src/pps41_display_pwm.v ../src/video_renderer.v ../src/field_rom.v \
	../src/label_rom.v ../src/rom_loader.v ../src/ce_gen.v ../src/audio_gen.v \
	../src/debug_probe.v

# Executes the real ROM's difficulty-read block and checks the PRO 1/PRO 2
# pin selects between its two constants -- see the header comment in
# golden/pro_switch_test.cpp.
# Whole-display parity against MAME's pwm_display levels -- see the header
# comment in golden/display_parity_test.cpp for how the reference was
# captured.
# End-to-end playability check: plays a real opening sequence and asserts
# what the scoreboard shows. See golden/gameplay_test.cpp.
gameplay-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/gameplay_test.cpp -o /tmp/gameplay_test
	/tmp/gameplay_test $(MAME_PARITY_ROM)

stimulus-test:
	$(CXX) $(CXXFLAGS) stimulus_test.cpp -o /tmp/stimulus_test
	/tmp/stimulus_test

display-parity-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/display_parity_test.cpp -o /tmp/display_parity_test
	/tmp/display_parity_test $(MAME_PARITY_ROM)

pro-switch-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/pro_switch_test.cpp -o /tmp/pro_switch_test
	/tmp/pro_switch_test $(MAME_PARITY_ROM)

core-top-test:
	$(VERILATOR) --cc $(CORE_TOP_SRCS) --exe core_top_tb.cpp \
		-I../src -I../src/fpga/core -I../src/fpga/apf \
		--top-module core_top --Mdir obj_dir_core_top \
		--public-flat-rw -Wno-fatal \
		-pvalue+ROM_WAIT_MAX_P=20000
	$(MAKE) -C obj_dir_core_top -f Vcore_top.mk
	cd ../src && ../sim/obj_dir_core_top/Vcore_top

debug-probe-test:
	$(VERILATOR) --cc ../src/debug_probe.v --exe debug_probe_tb.cpp \
		--Mdir obj_dir_debug_probe -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_debug_probe -f Vdebug_probe.mk
	./obj_dir_debug_probe/Vdebug_probe

golden-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/mm77la_model_test.cpp -o /tmp/mm77la_model_test
	/tmp/mm77la_model_test

# Whole-ROM parity against MAME's own MM77LA core -- see the header comment
# in golden/mame_parity_test.cpp, including how to regenerate its reference
# digests. Uses the real game ROM from development-assets/, which is the same
# image MAME's mfootb2 romset uses (identical SHA1).
MAME_PARITY_ROM ?= ../development-assets/b8000-12
mame-parity-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/mame_parity_test.cpp -o /tmp/mame_parity_test
	/tmp/mame_parity_test $(MAME_PARITY_ROM)

golden-trace: golden/mm77la_model.cpp golden/trace_main.cpp
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/trace_main.cpp -o /tmp/golden_trace

decode-test:
	$(VERILATOR) --cc ../src/pps41_decode.v --exe pps41_decode_tb.cpp \
		--Mdir obj_dir_decode -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_decode -f Vpps41_decode.mk
	./obj_dir_decode/Vpps41_decode

alu-test:
	$(VERILATOR) --cc ../src/pps41_alu.v --exe pps41_alu_tb.cpp \
		--Mdir obj_dir_alu -Wall
	$(MAKE) -C obj_dir_alu -f Vpps41_alu.mk
	./obj_dir_alu/Vpps41_alu

core-pc-test:
	$(VERILATOR) --cc -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v --exe pps41_core_pc_tb.cpp \
		--Mdir obj_dir_core_pc -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_pc -f Vpps41_core.mk
	./obj_dir_core_pc/Vpps41_core

core-ram-test:
	$(VERILATOR) --cc -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v --exe pps41_core_ram_tb.cpp \
		--Mdir obj_dir_core_ram -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_ram -f Vpps41_core.mk
	./obj_dir_core_ram/Vpps41_core

# pps41_display_mux.v and pps41_display_pwm.v are not instantiated inside
# pps41_core.v (the testbench drives them as separate DUTs, see
# pps41_core_tb.cpp) -- they are independent top-level modules, and
# Verilator's --cc only ever wraps ONE top per invocation (an unreached
# second/third top just triggers MULTITOP and gets silently dropped, not
# built). So each is verilated into its own Mdir to get its own Vfoo.h /
# Vfoo__ALL.a, and the final pps41_core executable links all three
# archives together via -LDFLAGS (only one copy of the verilated runtime,
# from obj_dir_core's own build, is linked in -- the other two archives
# are pure module object code, no verilated.o/verilated_threads.o inside).
core-test:
	$(VERILATOR) --cc ../src/pps41_display_mux.v --Mdir obj_dir_core_dmux -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dmux -f Vpps41_display_mux.mk Vpps41_display_mux__ALL.a
	$(VERILATOR) --cc ../src/pps41_display_pwm.v --Mdir obj_dir_core_dpwm -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dpwm -f Vpps41_display_pwm.mk Vpps41_display_pwm__ALL.a
	$(VERILATOR) --cc -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v \
		--exe pps41_core_tb.cpp golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp \
		--Mdir obj_dir_core -Wall --Wno-UNUSEDSIGNAL -Wno-WIDTH \
		-CFLAGS "-I.. -I../obj_dir_core_dmux -I../obj_dir_core_dpwm" \
		-LDFLAGS "../obj_dir_core_dmux/Vpps41_display_mux__ALL.a ../obj_dir_core_dpwm/Vpps41_display_pwm__ALL.a"
	$(MAKE) -C obj_dir_core -f Vpps41_core.mk

# RTL-vs-golden lockstep on the REAL ROM, through a full opening sequence.
#
# This exists because the synthetic vectors below are 30 cycles each and are
# not sensitive to a large class of RTL-only bugs: injecting a 3-bit -> 2-bit
# EOB immediate into pps41_core.v alone leaves vectors-test passing, while
# this catches it at cycle 148. Real ROM, real instruction mix, ~13 seconds
# of chip time.
#
# It runs TWICE. The ce=1 pass is the original one. The --ce-period=129 pass
# clocks the core the way the device does -- one clock enable every ~129.35
# core clocks, alternating 129/130 like ce_gen's fractional accumulator -- and
# exists because ce=1 hides every bug where a combinational instruction strobe
# reaches a register that is clocked on clk rather than qualified by ce. IOS
# was such a bug: at ce=1 it passed 1.2M cycles, at --ce-period=129 it diverges
# at cycle 220520 (tone_freq 0x44 vs 0x40, ios_state 0 vs 1). That was the
# hardware stuck-tone lockup. Do not drop this pass.
core-rom-lockstep-test: core-test
	@if [ ! -f "$(MAME_PARITY_ROM)" ]; then \
		echo "core-rom-lockstep-test: need the game ROM at $(MAME_PARITY_ROM)"; exit 2; fi
	./obj_dir_core/Vpps41_core "$(MAME_PARITY_ROM)" 1200000 stimulus/gameplay.txt
	./obj_dir_core/Vpps41_core "$(MAME_PARITY_ROM)" 1200000 stimulus/gameplay.txt --ce-period=129

# Fuzzes the golden model with randomised, human-plausible button traffic and
# asserts the invariant src/debug_probe.v checks on hardware: tone_on is never
# continuously high for 2 seconds. 400 trials x 30M cycles (35 hours of chip
# time) found nothing, which is what established that the hardware stuck-tone
# was NOT the ROM or the CPU semantics but the RTL's ce handling. Kept short
# here; raise the trial/cycle counts to re-run the full search.
tone-stuck-fuzz:
	@if [ ! -f "$(MAME_PARITY_ROM)" ]; then \
		echo "tone-stuck-fuzz: need the game ROM at $(MAME_PARITY_ROM)"; exit 2; fi
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/tone_stuck_fuzz.cpp -o /tmp/tone_stuck_fuzz
	/tmp/tone_stuck_fuzz $(MAME_PARITY_ROM) 8 5000000

vectors-test: core-test
	@set -e; \
	for v in vectors/*.bin; do \
		echo "-- $$v --"; \
		out=$$(./obj_dir_core/Vpps41_core "$$v" 30) || { echo "$$out"; echo "FAIL: $$v (nonzero exit)"; exit 1; }; \
		echo "$$out"; \
		case "$$out" in *PASS*) ;; *) echo "FAIL: $$v (no PASS in output)"; exit 1;; esac; \
	done; \
	echo "vectors-test: all vectors PASS"

# Every other vector above runs only 30 cycles -- far short of one
# pps41_display_pwm WINDOW (1583 cycles), so none of them ever exercise a
# settled (post-boundary) display level, let alone the window-boundary
# cycle itself. display_dp_via_d11.bin and display_multi_row_select.bin
# exist specifically to exercise the display pipeline, so run them for at
# least 2*WINDOW (3166) cycles here -- long enough to cross two window
# boundaries -- as part of the normal, repeatable test flow instead of
# requiring a human to remember to invoke Vpps41_core by hand with a
# different cycle count.
display-vectors-long-test: core-test
	@set -e; \
	for v in vectors/display_*.bin; do \
		echo "-- $$v (3200 cycles) --"; \
		out=$$(./obj_dir_core/Vpps41_core "$$v" 3200) || { echo "$$out"; echo "FAIL: $$v (nonzero exit)"; exit 1; }; \
		echo "$$out"; \
		case "$$out" in *PASS*) ;; *) echo "FAIL: $$v (no PASS in output)"; exit 1;; esac; \
	done; \
	echo "display-vectors-long-test: all display vectors PASS at 3200 cycles"

test: lint-core opla-test opla-rtl-test tone-test tone-rtl-test io-test display-mux-test display-pwm-test display-pwm-rtl-test ce-gen-test rom-loader-test audio-gen-test io-rtl-test display-mux-rtl-test decode-test alu-test core-pc-test core-ram-test golden-test mame-parity-test gameplay-test stimulus-test display-parity-test pro-switch-test debug-probe-test core-top-test core-test core-rom-lockstep-test tone-stuck-fuzz vectors-test display-vectors-long-test video-renderer-test field-rom-test label-rom-test

clean:
	rm -rf obj_dir* /tmp/mm77la_model_test /tmp/mm77la_opla_test /tmp/mm77la_tone_test /tmp/mm77la_io_test /tmp/mm77la_display_mux_test /tmp/mm77la_display_mux_rtl_test /tmp/mm77la_display_pwm_test
