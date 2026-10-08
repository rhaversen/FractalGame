// Minimal Vulkan compute runner (used with Mesa lavapipe on CPU) to execute the real HLSL kernels.
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>

struct FVkBuffer
{
	VkBuffer Buffer = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	VkDeviceSize Size = 0;
	void* Mapped = nullptr;
	bool bUniform = false;
	template <typename T> T* As() { return reinterpret_cast<T*>(Mapped); }
};

class FVkContext
{
public:
	FVkContext();
	~FVkContext();
	FVkBuffer CreateBuffer(VkDeviceSize Size, bool bUniform = false);
	void DestroyBuffer(FVkBuffer& Buffer);

	VkInstance Instance = VK_NULL_HANDLE;
	VkPhysicalDevice Physical = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;
	VkQueue Queue = VK_NULL_HANDLE;
	uint32_t QueueFamily = 0;
	VkCommandPool CommandPool = VK_NULL_HANDLE;
	std::string DeviceName;
};

class FVkKernel
{
public:
	// Bindings 0..N-1 in descriptor set 0; IsUniform[i] selects uniform vs storage buffer.
	FVkKernel(FVkContext& Ctx, const std::string& SpirvPath, const std::string& Entry, const std::vector<bool>& IsUniform);
	~FVkKernel();
	// Returns wall time in milliseconds.
	double Dispatch(const std::vector<FVkBuffer*>& Buffers, uint32_t GX, uint32_t GY, uint32_t GZ);

private:
	FVkContext& Ctx;
	VkShaderModule Module = VK_NULL_HANDLE;
	VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
	VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkDescriptorPool Pool = VK_NULL_HANDLE;
	std::vector<bool> Uniform;
};
