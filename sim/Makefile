# sim/Makefile
VERILATOR := verilator
CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

.PHONY: lint-core golden-test golden-trace decode-test alu-test clean

lint-core:
	$(VERILATOR) --lint-only -Wall ../src/pps41_core.v

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

clean:
	rm -rf obj_dir* /tmp/mm77la_model_test
