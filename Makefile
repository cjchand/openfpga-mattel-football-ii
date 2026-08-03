# Mattel Football II openFPGA core — build entry points
# make sim       — run the full Phase 1-3 Verilator/golden-model test suite
# make bitstream — compile the Quartus project in Docker (this plan)
# make package   — bit-reverse + stage the bitstream for the Pocket (Task 3)

QUARTUS_IMAGE ?= didiermalenfant/quartus:22.1-apple-silicon
QPF           ?= ap_core.qpf

.PHONY: sim bitstream package clean

sim:
	$(MAKE) -C sim test

bitstream:
	docker run --platform linux/amd64 --rm -t \
		-v $(PWD)/src/fpga:/build -w /build \
		$(QUARTUS_IMAGE) quartus_sh --flow compile $(QPF)

package:
	@echo "package target not yet implemented (Task 3)"

clean:
	$(MAKE) -C sim clean
