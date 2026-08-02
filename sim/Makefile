# sim/Makefile
VERILATOR := verilator
CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

.PHONY: lint-core golden-test golden-trace decode-test alu-test core-pc-test core-ram-test core-test vectors-test test clean

lint-core:
	$(VERILATOR) --lint-only -Wall --Wno-UNUSEDSIGNAL ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v

golden-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_model_test.cpp -o /tmp/mm77la_model_test
	/tmp/mm77la_model_test

golden-trace: golden/mm77la_model.cpp golden/trace_main.cpp
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/trace_main.cpp -o /tmp/golden_trace

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
	$(VERILATOR) --cc ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v --exe pps41_core_pc_tb.cpp \
		--Mdir obj_dir_core_pc -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_pc -f Vpps41_core.mk
	./obj_dir_core_pc/Vpps41_core

core-ram-test:
	$(VERILATOR) --cc ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v --exe pps41_core_ram_tb.cpp \
		--Mdir obj_dir_core_ram -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_core_ram -f Vpps41_core.mk
	./obj_dir_core_ram/Vpps41_core

core-test:
	$(VERILATOR) --cc ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v \
		--exe pps41_core_tb.cpp golden/mm77la_model.cpp \
		--Mdir obj_dir_core -Wall --Wno-UNUSEDSIGNAL -CFLAGS "-I.."
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

test: lint-core decode-test alu-test core-pc-test core-ram-test golden-test core-test vectors-test

clean:
	rm -rf obj_dir* /tmp/mm77la_model_test
