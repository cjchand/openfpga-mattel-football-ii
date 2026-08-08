# Mattel Football II openFPGA core — build entry points
# make sim       — run the full Verilator/golden-model test suite, plus the
#                  platform-icon check
# make bitstream — compile the Quartus project in Docker
# make package   — bit-reverse + stage the bitstream for the Pocket

QUARTUS_IMAGE ?= didiermalenfant/quartus:22.1-apple-silicon
QPF           ?= ap_core.qpf

RBF        ?= src/fpga/output_files/ap_core.rbf
RBF_R_DEST ?= dist/Cores/cjchand.Mattel Football II/bitstream.rbf_r

.PHONY: sim bitstream package clean

sim:
	$(MAKE) -C sim test
	python3 sim/test_platform_icon.py
	python3 sim/test_core_packaging.py

bitstream:
	docker run --platform linux/amd64 --rm -t \
		-v $(PWD)/src:/build -w /build/fpga \
		$(QUARTUS_IMAGE) quartus_sh --flow compile $(QPF)

package:
	python3 tools/reverse_rbf.py "$(RBF)" "$(RBF_R_DEST)"
	@echo "Staged: $(RBF_R_DEST)"
	@echo "Copy the contents of dist/ onto the Pocket SD card root."

clean:
	$(MAKE) -C sim clean
