# sim/Makefile
VERILATOR := verilator
CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

.PHONY: lint-core golden-test clean

lint-core:
	$(VERILATOR) --lint-only -Wall ../src/pps41_core.v

golden-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_model_test.cpp -o /tmp/mm77la_model_test
	/tmp/mm77la_model_test

clean:
	rm -rf obj_dir* /tmp/mm77la_model_test
