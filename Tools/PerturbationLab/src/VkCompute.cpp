#include "VkCompute.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <chrono>

#define VKCHECK(x) do { VkResult R_ = (x); if (R_ != VK_SUCCESS) { std::fprintf(stderr, "Vulkan error %d at %s:%d (%s)\n", (int)R_, __FILE__, __LINE__, #x); std::exit(1); } } while (0)

FVkContext::FVkContext()
{
	VkApplicationInfo App{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	App.pApplicationName = "PerturbationLab";
	App.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ICI{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	ICI.pApplicationInfo = &App;
	VKCHECK(vkCreateInstance(&ICI, nullptr, &Instance));

	uint32_t Count = 0;
	vkEnumeratePhysicalDevices(Instance, &Count, nullptr);
	std::vector<VkPhysicalDevice> Devs(Count);
	vkEnumeratePhysicalDevices(Instance, &Count, Devs.data());
	if (Count == 0) { std::fprintf(stderr, "No Vulkan device\n"); std::exit(1); }
	Physical = Devs[0];
	VkPhysicalDeviceProperties Props;
	vkGetPhysicalDeviceProperties(Physical, &Props);
	DeviceName = Props.deviceName;

	uint32_t QCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(Physical, &QCount, nullptr);
	std::vector<VkQueueFamilyProperties> QF(QCount);
	vkGetPhysicalDeviceQueueFamilyProperties(Physical, &QCount, QF.data());
	for (uint32_t I = 0; I < QCount; I++)
		if (QF[I].queueFlags & VK_QUEUE_COMPUTE_BIT) { QueueFamily = I; break; }

	float Prio = 1.0f;
	VkDeviceQueueCreateInfo QCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	QCI.queueFamilyIndex = QueueFamily;
	QCI.queueCount = 1;
	QCI.pQueuePriorities = &Prio;
	VkDeviceCreateInfo DCI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	DCI.queueCreateInfoCount = 1;
	DCI.pQueueCreateInfos = &QCI;
	VKCHECK(vkCreateDevice(Physical, &DCI, nullptr, &Device));
	vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);

	VkCommandPoolCreateInfo CPI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	CPI.queueFamilyIndex = QueueFamily;
	CPI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VKCHECK(vkCreateCommandPool(Device, &CPI, nullptr, &CommandPool));
}

FVkContext::~FVkContext()
{
	vkDestroyCommandPool(Device, CommandPool, nullptr);
	vkDestroyDevice(Device, nullptr);
	vkDestroyInstance(Instance, nullptr);
}

FVkBuffer FVkContext::CreateBuffer(VkDeviceSize Size, bool bUniform)
{
	FVkBuffer B;
	B.Size = Size < 16 ? 16 : Size;
	B.bUniform = bUniform;
	VkBufferCreateInfo BCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	BCI.size = B.Size;
	BCI.usage = bUniform ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	VKCHECK(vkCreateBuffer(Device, &BCI, nullptr, &B.Buffer));
	VkMemoryRequirements Req;
	vkGetBufferMemoryRequirements(Device, B.Buffer, &Req);
	VkPhysicalDeviceMemoryProperties MP;
	vkGetPhysicalDeviceMemoryProperties(Physical, &MP);
	uint32_t TypeIndex = UINT32_MAX;
	for (uint32_t I = 0; I < MP.memoryTypeCount; I++)
	{
		const VkMemoryPropertyFlags Want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		if ((Req.memoryTypeBits & (1u << I)) && (MP.memoryTypes[I].propertyFlags & Want) == Want) { TypeIndex = I; break; }
	}
	VkMemoryAllocateInfo MAI{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	MAI.allocationSize = Req.size;
	MAI.memoryTypeIndex = TypeIndex;
	VKCHECK(vkAllocateMemory(Device, &MAI, nullptr, &B.Memory));
	VKCHECK(vkBindBufferMemory(Device, B.Buffer, B.Memory, 0));
	VKCHECK(vkMapMemory(Device, B.Memory, 0, B.Size, 0, &B.Mapped));
	std::memset(B.Mapped, 0, B.Size);
	return B;
}

void FVkContext::DestroyBuffer(FVkBuffer& B)
{
	if (B.Buffer == VK_NULL_HANDLE) return;
	vkUnmapMemory(Device, B.Memory);
	vkDestroyBuffer(Device, B.Buffer, nullptr);
	vkFreeMemory(Device, B.Memory, nullptr);
	B = FVkBuffer();
}

FVkKernel::FVkKernel(FVkContext& InCtx, const std::string& SpirvPath, const std::string& Entry, const std::vector<bool>& IsUniform)
	: Ctx(InCtx), Uniform(IsUniform)
{
	std::ifstream F(SpirvPath, std::ios::binary | std::ios::ate);
	if (!F) { std::fprintf(stderr, "Cannot open %s\n", SpirvPath.c_str()); std::exit(1); }
	size_t Size = (size_t)F.tellg();
	std::vector<uint32_t> Code((Size + 3) / 4);
	F.seekg(0);
	F.read(reinterpret_cast<char*>(Code.data()), (std::streamsize)Size);

	VkShaderModuleCreateInfo SMI{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	SMI.codeSize = Size;
	SMI.pCode = Code.data();
	VKCHECK(vkCreateShaderModule(Ctx.Device, &SMI, nullptr, &Module));

	std::vector<VkDescriptorSetLayoutBinding> Bindings(IsUniform.size());
	uint32_t NumUniform = 0, NumStorage = 0;
	for (size_t I = 0; I < IsUniform.size(); I++)
	{
		Bindings[I].binding = (uint32_t)I;
		Bindings[I].descriptorType = IsUniform[I] ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		Bindings[I].descriptorCount = 1;
		Bindings[I].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		(IsUniform[I] ? NumUniform : NumStorage)++;
	}
	VkDescriptorSetLayoutCreateInfo DLI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	DLI.bindingCount = (uint32_t)Bindings.size();
	DLI.pBindings = Bindings.data();
	VKCHECK(vkCreateDescriptorSetLayout(Ctx.Device, &DLI, nullptr, &SetLayout));

	VkPipelineLayoutCreateInfo PLI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	PLI.setLayoutCount = 1;
	PLI.pSetLayouts = &SetLayout;
	VKCHECK(vkCreatePipelineLayout(Ctx.Device, &PLI, nullptr, &PipelineLayout));

	VkComputePipelineCreateInfo CPI{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	CPI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	CPI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	CPI.stage.module = Module;
	CPI.stage.pName = Entry.c_str();
	CPI.layout = PipelineLayout;
	VKCHECK(vkCreateComputePipelines(Ctx.Device, VK_NULL_HANDLE, 1, &CPI, nullptr, &Pipeline));

	std::vector<VkDescriptorPoolSize> Sizes;
	if (NumUniform) Sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NumUniform * 64});
	if (NumStorage) Sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NumStorage * 64});
	VkDescriptorPoolCreateInfo DPI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	DPI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	DPI.maxSets = 64;
	DPI.poolSizeCount = (uint32_t)Sizes.size();
	DPI.pPoolSizes = Sizes.data();
	VKCHECK(vkCreateDescriptorPool(Ctx.Device, &DPI, nullptr, &Pool));
}

FVkKernel::~FVkKernel()
{
	vkDestroyDescriptorPool(Ctx.Device, Pool, nullptr);
	vkDestroyPipeline(Ctx.Device, Pipeline, nullptr);
	vkDestroyPipelineLayout(Ctx.Device, PipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(Ctx.Device, SetLayout, nullptr);
	vkDestroyShaderModule(Ctx.Device, Module, nullptr);
}

double FVkKernel::Dispatch(const std::vector<FVkBuffer*>& Buffers, uint32_t GX, uint32_t GY, uint32_t GZ)
{
	VkDescriptorSetAllocateInfo AI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	AI.descriptorPool = Pool;
	AI.descriptorSetCount = 1;
	AI.pSetLayouts = &SetLayout;
	VkDescriptorSet Set;
	VKCHECK(vkAllocateDescriptorSets(Ctx.Device, &AI, &Set));

	std::vector<VkDescriptorBufferInfo> Infos(Buffers.size());
	std::vector<VkWriteDescriptorSet> Writes(Buffers.size());
	for (size_t I = 0; I < Buffers.size(); I++)
	{
		Infos[I] = {Buffers[I]->Buffer, 0, VK_WHOLE_SIZE};
		Writes[I] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		Writes[I].dstSet = Set;
		Writes[I].dstBinding = (uint32_t)I;
		Writes[I].descriptorCount = 1;
		Writes[I].descriptorType = Uniform[I] ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		Writes[I].pBufferInfo = &Infos[I];
	}
	vkUpdateDescriptorSets(Ctx.Device, (uint32_t)Writes.size(), Writes.data(), 0, nullptr);

	VkCommandBufferAllocateInfo CAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	CAI.commandPool = Ctx.CommandPool;
	CAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	CAI.commandBufferCount = 1;
	VkCommandBuffer Cmd;
	VKCHECK(vkAllocateCommandBuffers(Ctx.Device, &CAI, &Cmd));
	VkCommandBufferBeginInfo BI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	BI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VKCHECK(vkBeginCommandBuffer(Cmd, &BI));
	vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
	vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &Set, 0, nullptr);
	vkCmdDispatch(Cmd, GX, GY, GZ);
	VKCHECK(vkEndCommandBuffer(Cmd));

	VkSubmitInfo SI{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	SI.commandBufferCount = 1;
	SI.pCommandBuffers = &Cmd;
	auto T0 = std::chrono::steady_clock::now();
	VKCHECK(vkQueueSubmit(Ctx.Queue, 1, &SI, VK_NULL_HANDLE));
	VKCHECK(vkQueueWaitIdle(Ctx.Queue));
	auto T1 = std::chrono::steady_clock::now();
	vkFreeCommandBuffers(Ctx.Device, Ctx.CommandPool, 1, &Cmd);
	vkFreeDescriptorSets(Ctx.Device, Pool, 1, &Set);
	return std::chrono::duration<double, std::milli>(T1 - T0).count();
}
