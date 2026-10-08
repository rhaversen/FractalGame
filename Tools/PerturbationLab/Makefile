# PerturbationLab: standalone validation of the FractalRenderer perturbation maths.
# Requires: g++ with libquadmath, Vulkan loader + a Vulkan driver (Mesa lavapipe works), DXC.
DXC ?= /opt/dxc/bin/dxc
DXC_LIB ?= /opt/dxc/lib
OUT := out
PLUGIN := ../../Plugins/FractalRenderer
CXXFLAGS := -O2 -std=gnu++20 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$(PLUGIN)/Source/FractalRenderer/Public -Isrc -DLAB_OUT_DIR=\"$(abspath $(OUT))\" -pthread
LIBS := -lquadmath -lvulkan -pthread
DXCFLAGS := -T cs_6_0 -spirv -fspv-target-env=vulkan1.1 -Zpr -HV 2021 -O3 -I $(PLUGIN)/Shaders -I shaders

TYPES := 0 1 2 3 4 5 6 7
KERNELS := FuncsMain StepMain
SPV := $(addprefix $(OUT)/,$(addsuffix .spv,$(KERNELS))) $(OUT)/MatrixMain.spv \
	$(foreach T,$(TYPES),$(OUT)/DEMain_T$(T).spv $(OUT)/RenderMain_T$(T).spv $(OUT)/RenderNaive_T$(T).spv)
BENCH_SPV := $(OUT)/BenchPerturbed.spv $(OUT)/BenchPerturbed8.spv $(OUT)/BenchDirect.spv $(OUT)/BenchDirect8.spv $(OUT)/BenchOldHW.spv
HEADERS := $(wildcard src/*.h) $(wildcard src/scenes/*.inc) $(wildcard $(PLUGIN)/Source/FractalRenderer/Public/FractalMath/Formulas/*.h) $(wildcard $(PLUGIN)/Source/FractalRenderer/Public/FractalMath/*.h)
SHADERS := $(wildcard shaders/*.hlsl) $(wildcard $(PLUGIN)/Shaders/*.ush)

all: $(OUT)/test_dd $(OUT)/test_gpu_math $(OUT)/test_perturbed_de $(OUT)/test_camera $(OUT)/render_lab $(OUT)/bench $(SPV) $(BENCH_SPV)

$(OUT):
	mkdir -p $(OUT)

# One SPIR-V per formula (FP_FRACTAL_TYPE = EFractalFormula index)
$(OUT)/DEMain_T%.spv: $(SHADERS) | $(OUT)
	LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) $(DXCFLAGS) -D FP_FRACTAL_TYPE=$* -E DEMain shaders/LabKernels.hlsl -Fo $@

$(OUT)/RenderMain_T%.spv: $(SHADERS) | $(OUT)
	LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) $(DXCFLAGS) -D FP_FRACTAL_TYPE=$* -E RenderMain shaders/RenderLab.hlsl -Fo $@

$(OUT)/RenderNaive_T%.spv: $(SHADERS) | $(OUT)
	LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) $(DXCFLAGS) -D FP_FRACTAL_TYPE=$* -D FP_FORCE_NAIVE=1 -E RenderMain shaders/RenderLab.hlsl -Fo $@

$(OUT)/MatrixMain.spv: $(SHADERS) | $(OUT)
	LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) $(DXCFLAGS) -E MatrixMain shaders/MatrixLab.hlsl -Fo $@

$(OUT)/BenchPerturbed.spv $(OUT)/BenchDirect.spv $(OUT)/BenchOldHW.spv: $(SHADERS) | $(OUT)
	LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) $(DXCFLAGS) -E $(basename $(notdir $@)) shaders/BenchLab.hlsl -Fo $@
$(OUT)/BenchPerturbed8.spv $(OUT)/BenchDirect8.spv: $(SHADERS) | $(OUT)
	LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) $(DXCFLAGS) -D FP_STATIC_INTEGER_POWER=8 -E $(subst 8,,$(basename $(notdir $@))) shaders/BenchLab.hlsl -Fo $@

$(OUT)/bench: src/Bench.cpp src/VkCompute.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) src/Bench.cpp src/VkCompute.cpp -o $@ $(LIBS)

$(OUT)/%.spv: $(SHADERS) | $(OUT)
	LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) $(DXCFLAGS) -E $* shaders/LabKernels.hlsl -Fo $@

$(OUT)/test_dd: src/TestDD.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBS)

$(OUT)/test_gpu_math: src/TestGpuMath.cpp src/VkCompute.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) src/TestGpuMath.cpp src/VkCompute.cpp -o $@ $(LIBS)

$(OUT)/test_perturbed_de: src/TestPerturbedDE.cpp src/VkCompute.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) src/TestPerturbedDE.cpp src/VkCompute.cpp -o $@ $(LIBS)

$(OUT)/test_camera: src/TestCamera.cpp src/VkCompute.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) src/TestCamera.cpp src/VkCompute.cpp -o $@ $(LIBS)

$(OUT)/render_lab: src/Render.cpp src/VkCompute.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) src/Render.cpp src/VkCompute.cpp -o $@ $(LIBS)

# Compile the real Unreal entry point (engine include stripped) for every permutation the plugin uses
# (each formula; the static power-8 Mandelbulb), DXIL + SPIR-V, HLSL 2018 and 2021.
ue-shader-check: | $(OUT)
	sed 's|#include "/Engine/Public/Platform.ush"||' $(PLUGIN)/Shaders/PerturbationShader.usf > $(OUT)/PerturbationShader.check.hlsl
	for PERM in "0 0" "0 1" "1 0" "2 0" "3 0" "4 0" "5 0" "6 0" "7 0"; do set -- $$PERM; for HV in 2018 2021; do \
		LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) -T cs_6_0 -E PerturbationShader -Zpr -O3 -HV $$HV -I $(PLUGIN)/Shaders \
			-D THREADS_X=8 -D THREADS_Y=8 -D THREADS_Z=1 -D FP_FRACTAL_TYPE=$$1 -D FP_STATIC_POWER_8=$$2 $(OUT)/PerturbationShader.check.hlsl -Fo $(OUT)/ue_$$1_$$2_$$HV.dxil || exit 1; \
		LD_LIBRARY_PATH=$(DXC_LIB) $(DXC) -T cs_6_0 -E PerturbationShader -Zpr -O3 -HV $$HV -spirv -I $(PLUGIN)/Shaders \
			-D THREADS_X=8 -D THREADS_Y=8 -D THREADS_Z=1 -D FP_FRACTAL_TYPE=$$1 -D FP_STATIC_POWER_8=$$2 $(OUT)/PerturbationShader.check.hlsl -Fo $(OUT)/ue_$$1_$$2_$$HV.spv || exit 1; \
	done; done
	@echo "PerturbationShader.usf compiles (DXIL + SPIR-V, HLSL 2018/2021, all permutations)"

test: all ue-shader-check
	$(OUT)/test_dd
	$(OUT)/test_gpu_math
	python3 scripts/verify_step_mpmath.py $(OUT)/step_tiny_cases.csv
	$(OUT)/test_perturbed_de all 600
	$(OUT)/test_camera

images: all
	$(OUT)/render_lab 192 120 1 all
	python3 scripts/make_panel.py $(OUT)/images $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all test clean ue-shader-check images
