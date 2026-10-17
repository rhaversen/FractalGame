#include "PerturbationShader.h"

IMPLEMENT_GLOBAL_SHADER(FPerturbationComputeShader, "/FractalRendererShaders/PerturbationShader.usf", "PerturbationShader", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFractalUpscaleShader, "/FractalRendererShaders/FractalUpscale.usf", "FractalUpscale", SF_Compute);
