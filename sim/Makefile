# sim/Makefile
VERILATOR := verilator
CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

.PHONY: lint-core golden-test golden-trace decode-test alu-test core-pc-test core-ram-test core-test vectors-test display-vectors-long-test test clean
.PHONY: opla-test opla-rtl-test tone-test tone-rtl-test io-test io-rtl-test display-mux-test display-mux-rtl-test display-pwm-test display-pwm-rtl-test ce-gen-test rom-loader-test audio-gen-test display-render-test screenshot

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

display-render-test:
	$(VERILATOR) --cc ../src/display_render.v --exe display_render_tb.cpp \
		--Mdir obj_dir_display_render -Wall --Wno-UNUSEDSIGNAL --Wno-WIDTH
	$(MAKE) -C obj_dir_display_render -f Vdisplay_render.mk
	./obj_dir_display_render/Vdisplay_render

# Debugging/verification aid, not part of `test` -- see screenshot_tb.cpp.
# Usage: make screenshot ROM=path/to.bin CYCLES=20000 OUT=/tmp/frame.ppm [STIM=stimulus/foo.txt]
ROM     ?= ../dist/Assets/mattel_football_ii/common/b8000-12.bin
CYCLES  ?= 20000
OUT     ?= /tmp/screenshot.ppm
screenshot:
	$(VERILATOR) --cc ../src/pps41_display_mux.v --Mdir obj_dir_core_dmux -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dmux -f Vpps41_display_mux.mk Vpps41_display_mux__ALL.a
	$(VERILATOR) --cc ../src/pps41_display_pwm.v --Mdir obj_dir_core_dpwm -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_dpwm -f Vpps41_display_pwm.mk Vpps41_display_pwm__ALL.a
	$(VERILATOR) --cc ../src/display_render.v --Mdir obj_dir_core_render -Wall --Wno-UNUSEDSIGNAL --Wno-WIDTH
	$(MAKE) -C obj_dir_core_render -f Vdisplay_render.mk Vdisplay_render__ALL.a
	$(VERILATOR) --cc -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v \
		--exe screenshot_tb.cpp \
		--Mdir obj_dir_screenshot -Wall --Wno-UNUSEDSIGNAL -Wno-WIDTH \
		-CFLAGS "-I.. -I../obj_dir_core_dmux -I../obj_dir_core_dpwm -I../obj_dir_core_render" \
		-LDFLAGS "../obj_dir_core_dmux/Vpps41_display_mux__ALL.a ../obj_dir_core_dpwm/Vpps41_display_pwm__ALL.a ../obj_dir_core_render/Vdisplay_render__ALL.a"
	$(MAKE) -C obj_dir_screenshot -f Vpps41_core.mk
	./obj_dir_screenshot/Vpps41_core "$(ROM)" "$(CYCLES)" "$(OUT)" $(STIM)

golden-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp golden/mm77la_model_test.cpp -o /tmp/mm77la_model_test
	/tmp/mm77la_model_test

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

test: lint-core opla-test opla-rtl-test tone-test tone-rtl-test io-test display-mux-test display-pwm-test display-pwm-rtl-test ce-gen-test rom-loader-test audio-gen-test io-rtl-test display-mux-rtl-test decode-test alu-test core-pc-test core-ram-test golden-test core-test vectors-test display-vectors-long-test display-render-test

clean:
	rm -rf obj_dir* /tmp/mm77la_model_test /tmp/mm77la_opla_test /tmp/mm77la_tone_test /tmp/mm77la_io_test /tmp/mm77la_display_mux_test /tmp/mm77la_display_mux_rtl_test /tmp/mm77la_display_pwm_test
