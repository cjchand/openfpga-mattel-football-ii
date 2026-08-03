# sim/Makefile
VERILATOR := verilator
CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

.PHONY: lint-core golden-test golden-trace decode-test alu-test core-pc-test core-ram-test core-test vectors-test test clean
.PHONY: opla-test opla-rtl-test tone-test tone-rtl-test io-test io-rtl-test display-mux-test display-mux-rtl-test display-pwm-test

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

golden-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_model_test.cpp -o /tmp/mm77la_model_test
	/tmp/mm77la_model_test

golden-trace: golden/mm77la_model.cpp golden/trace_main.cpp
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/trace_main.cpp -o /tmp/golden_trace

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

core-test:
	$(VERILATOR) --cc -I../src ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v \
		--exe pps41_core_tb.cpp golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp \
		--Mdir obj_dir_core -Wall --Wno-UNUSEDSIGNAL -Wno-WIDTH -CFLAGS "-I.."
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

test: lint-core opla-test opla-rtl-test tone-test tone-rtl-test io-test display-mux-test io-rtl-test display-mux-rtl-test decode-test alu-test core-pc-test core-ram-test golden-test core-test vectors-test

clean:
	rm -rf obj_dir* /tmp/mm77la_model_test /tmp/mm77la_opla_test /tmp/mm77la_tone_test /tmp/mm77la_io_test /tmp/mm77la_display_mux_test /tmp/mm77la_display_mux_rtl_test /tmp/mm77la_display_pwm_test
