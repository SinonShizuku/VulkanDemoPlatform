#pragma once
#include "../../Start.h"
#include "../VulkanCore.h"
#include "VulkanCommand.h"



class VulkanDeviceMemory {
    VkDeviceMemory handle = VK_NULL_HANDLE;
    VkDeviceSize allocation_size = 0;
    VkMemoryPropertyFlags memory_properties = 0;

    // 该函数用于在映射内存区时，调整非host coherent的内存区域的范围
    VkDeviceSize adjust_non_coherent_memory_size(VkDeviceSize &size, VkDeviceSize &offset) const {
        const VkDeviceSize& non_coherent_atom_size = VulkanCore::get_singleton().get_vulkan_device().get_physical_device_properties().limits.nonCoherentAtomSize;
        VkDeviceSize _offset = offset;
        VkDeviceSize range_end = size + offset;
        offset = offset / non_coherent_atom_size * non_coherent_atom_size;
        range_end = (range_end + non_coherent_atom_size - 1) / non_coherent_atom_size * non_coherent_atom_size;
        range_end = std::min(range_end, allocation_size);
        size = range_end - offset;
        return _offset - offset;
    }
protected:
    class {
        friend class VulkanBufferMemory;
        friend class VulkanImageMemory;
        bool value = false;
        operator bool() const {return value;}
        auto& operator =  (bool value) {this->value = value; return *this;}
    } are_bound;
public:
    VulkanDeviceMemory() = default;
    VulkanDeviceMemory(VkMemoryAllocateInfo & allocate_info) {
        allocate(allocate_info);
    }
    VulkanDeviceMemory(VulkanDeviceMemory &&other) noexcept {
        MoveHandle;
        allocation_size = other.allocation_size;
        memory_properties = other.memory_properties;
        other.memory_properties = 0;
        other.allocation_size = 0;
    }
    ~VulkanDeviceMemory() {
        DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkFreeMemory);
        allocation_size = 0;
        memory_properties = 0;
    }

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    [[nodiscard]] VkDeviceSize get_allocation_size() const {
        return allocation_size;
    }

    [[nodiscard]] VkMemoryPropertyFlags get_memory_properties() const {
        return memory_properties;
    }

    // const function
    result_t map_memory(void*& pData, VkDeviceSize size, VkDeviceSize offset = 0) const {
        VkDeviceSize inverse_delta_offset;
        if (!(memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            inverse_delta_offset = adjust_non_coherent_memory_size(size, offset);
        if (VkResult result = vkMapMemory(VulkanCore::get_singleton().get_vulkan_device().get_device(),handle,offset, size,0,&pData)) {
            outstream << std::format("[ VulkanDeviceMemory ] ERROR\nFailed to map the memory!\nError code: {}\n", int32_t(result));
            return result;
        }
        if (!(memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            pData = static_cast<uint8_t*>(pData) + inverse_delta_offset;
            VkMappedMemoryRange mapped_memory_range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = handle,
                .offset = offset,
                .size = size
            };
            if (VkResult result = vkInvalidateMappedMemoryRanges(VulkanCore::get_singleton().get_vulkan_device().get_device(),1,&mapped_memory_range)) {
                outstream << std::format("[ VulkanDeviceMemory ] ERROR\nFailed to flush the memory!\nError code: {}\n", int32_t(result));
                return result;
            }
        }
        return VK_SUCCESS;
    }

    // unmap
    result_t unmap_memory(VkDeviceSize size, VkDeviceSize offset = 0) const {
        if (!(memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            adjust_non_coherent_memory_size(size, offset);
            VkMappedMemoryRange mapped_memory_range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = handle,
                .offset = offset,
                .size = size
            };
            if (VkResult result = vkFlushMappedMemoryRanges(VulkanCore::get_singleton().get_vulkan_device().get_device(),1,&mapped_memory_range)) {
                outstream << std::format("[ VulkanDeviceMemory ] ERROR\nFailed to flush the memory!\nError code: {}\n", int32_t(result));
                return result;
            }
        }
        vkUnmapMemory(VulkanCore::get_singleton().get_vulkan_device().get_device(),handle);
        return VK_SUCCESS;
    }
    // buffer_data(...)用于方便地更新设备内存区，适用于用memcpy(...)向内存区写入数据后立刻取消映射的情况
    result_t buffer_data(const void* pData_src, VkDeviceSize size, VkDeviceSize offset = 0) const {
        void* pData_dst;
        if (VkResult result = map_memory(pData_dst, size, offset))
            return result;
        memcpy(pData_dst, pData_src, size_t(size));
        return unmap_memory(size, offset);
    }
    result_t buffer_data(const auto& data_src) const {
        return buffer_data(&data_src, sizeof data_src);
    }
    //retrieve_data(...)用于方便地从设备内存区取回数据，适用于用memcpy(...)从内存区取得数据后立刻取消映射的情况
    result_t retrieve_data(void* pData_dst, VkDeviceSize size, VkDeviceSize offset = 0) const {
        void* pData_src;
        if (VkResult result = map_memory(pData_src, size, offset))
            return result;
        memcpy(pData_dst, pData_src, size_t(size));
        return unmap_memory(size, offset);
    }
    //Non-const Function
    result_t allocate(VkMemoryAllocateInfo& allocate_info) {
        if (allocate_info.memoryTypeIndex >= VulkanCore::get_singleton().get_vulkan_device().get_physical_device_memory_properties().memoryTypeCount) {
            outstream << std::format("[ VulkanDeviceMemory ] ERROR\nInvalid memory type index!\n");
            return VK_RESULT_MAX_ENUM; //没有合适的错误代码，别用VK_ERROR_UNKNOWN
        }
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        if (VkResult result = vkAllocateMemory(VulkanCore::get_singleton().get_vulkan_device().get_device(), &allocate_info, nullptr, &handle)) {
            outstream << std::format("[ VulkanDeviceMemory ] ERROR\nFailed to allocate memory!\nError code: {}\n", int32_t(result));
            return result;
        }
        //记录实际分配的内存大小
        allocation_size = allocate_info.allocationSize;
        //取得内存属性
        memory_properties = VulkanCore::get_singleton().get_vulkan_device().get_physical_device_memory_properties().memoryTypes[allocate_info.memoryTypeIndex].propertyFlags;
        return VK_SUCCESS;
    }
};


class VulkanBuffer {
    VkBuffer handle = VK_NULL_HANDLE;
public:
    VulkanBuffer() = default;
    VulkanBuffer(VkBufferCreateInfo &create_info) {
        create(create_info);
    }
    VulkanBuffer(VulkanBuffer &&other) noexcept {MoveHandle;}
    ~VulkanBuffer() {DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyBuffer);}

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // const function
    [[nodiscard]] VkMemoryAllocateInfo memory_allocate_info(VkMemoryPropertyFlags desired_memory_properties) const {
        VkMemoryAllocateInfo memory_allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        };
        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(VulkanCore::get_singleton().get_vulkan_device().get_device(),handle,&memory_requirements);
        memory_allocate_info.allocationSize = memory_requirements.size;
        memory_allocate_info.memoryTypeIndex = UINT32_MAX;
        auto& physical_device_memory_properties = VulkanCore::get_singleton().get_vulkan_device().get_physical_device_memory_properties();
        for (size_t i = 0; i < physical_device_memory_properties.memoryTypeCount; i++) {
            if (memory_requirements.memoryTypeBits & 1 << i &&
                (physical_device_memory_properties.memoryTypes[i].propertyFlags & desired_memory_properties) == desired_memory_properties) {
                memory_allocate_info.memoryTypeIndex = i;
                break;
            }
        }
        //不在此检查是否成功取得内存类型索引，因为会把memoryAllocateInfo返回出去，交由外部检查
        //if (memoryAllocateInfo.memoryTypeIndex == UINT32_MAX)
        //    outStream << std::format("[ buffer ] ERROR\nFailed to find any memory type satisfies all desired memory properties!\n");
        return memory_allocate_info;
    }

    result_t bind_memory(VkDeviceMemory memory, VkDeviceSize offset = 0) const {
        VkResult result = vkBindBufferMemory(VulkanCore::get_singleton().get_vulkan_device().get_device(), handle, memory, offset);
        if (result)
            outstream<<std::format("[ VulkanBuffer ] ERROR\nFailed to attach the memory!\nError code: {}\n", int32_t(result));
        return result;
    }

    // non-const function
    result_t create(VkBufferCreateInfo &create_info) {
        create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        VkResult result = vkCreateBuffer(VulkanCore::get_singleton().get_vulkan_device().get_device(), &create_info, nullptr, &handle);
        if (result)
            outstream<< std::format("[ VulkanBuffer ] ERROR\nFailed to create a buffer!\nError code: {}\n", int32_t(result));
        return result;
    }
};

class VulkanBufferMemory: VulkanBuffer, VulkanDeviceMemory {
public:
    VulkanBufferMemory() = default;
    VulkanBufferMemory(VulkanBufferMemory &&other) noexcept :
    VulkanBuffer(std::move(other)), VulkanDeviceMemory(std::move(other)){
        are_bound = other.are_bound;
        other.are_bound = false;
    }
    ~VulkanBufferMemory() { are_bound = false; }

    // getter
    //不定义到VkBuffer和VkDeviceMemory的转换函数，因为32位下这俩类型都是uint64_t的别名，会造成冲突（虽然，谁他妈还用32位PC！）
    VkBuffer Buffer() const { return static_cast<const VulkanBuffer&>(*this); }
    const VkBuffer* AddressOfBuffer() const { return VulkanBuffer::Address(); }
    VkDeviceMemory Memory() const { return static_cast<const VulkanDeviceMemory&>(*this); }
    const VkDeviceMemory* AddressOfMemory() const { return VulkanDeviceMemory::Address(); }
    //若areBond为true，则成功分配了设备内存、创建了缓冲区，且成功绑定在一起
    bool AreBound() const { return are_bound; }
    using VulkanDeviceMemory::get_allocation_size;
    using VulkanDeviceMemory::get_memory_properties;
    //Const Function
    using VulkanDeviceMemory::map_memory;
    using VulkanDeviceMemory::unmap_memory;
    using VulkanDeviceMemory::buffer_data;
    using VulkanDeviceMemory::retrieve_data;

    // non-const function
    result_t create_buffer(VkBufferCreateInfo &create_info) {
        return VulkanBuffer::create(create_info);
    }

    result_t allocate_memory(VkMemoryPropertyFlags memory_property_flags) {
        VkMemoryAllocateInfo allocate_info = memory_allocate_info(memory_property_flags);
        if (allocate_info.memoryTypeIndex >= VulkanCore::get_singleton().get_vulkan_device().get_physical_device_memory_properties().memoryTypeCount) {
            return VK_RESULT_MAX_ENUM;
        }
        return allocate(allocate_info);
    }

    result_t bind_memory() {
        if (VkResult result = VulkanBuffer::bind_memory(Memory()))
            return result;
        are_bound = true;
        return VK_SUCCESS;
    }

    result_t create(VkBufferCreateInfo &create_info, VkMemoryPropertyFlags memory_property_flags) {
        VkResult result;
        false ||
            (result = create_buffer(create_info)) || //用||短路执行
            (result = allocate_memory(memory_property_flags)) ||
            (result = bind_memory());
        return result;
    }

};

class VulkanBufferView {
    VkBufferView handle = VK_NULL_HANDLE;
public:
    VulkanBufferView() = default;
    VulkanBufferView(VkBufferViewCreateInfo &create_info) {
        create(create_info);
    }
    VulkanBufferView(VkBuffer buffer, VkFormat format, VkDeviceSize offset = 0, VkDeviceSize range = 0) {
        create(buffer, format, offset, range);
    }
    VulkanBufferView(VulkanBufferView &&other) noexcept { MoveHandle;}
    ~VulkanBufferView() { DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(), vkDestroyBufferView); }

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // non-const function
    result_t create(VkBufferViewCreateInfo &create_info) {
        create_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        VkResult result = vkCreateBufferView(VulkanCore::get_singleton().get_vulkan_device().get_device(), &create_info, nullptr, &handle);
        if (result)
            outstream << std::format("[ VulkanBufferView ] ERROR\nFailed to create a buffer view!\nError code: {}\n", int32_t(result));
        return result;
    }

    result_t create(VkBuffer buffer, VkFormat format, VkDeviceSize offset = 0, VkDeviceSize range = 0) {
        VkBufferViewCreateInfo create_info = {
            .buffer = buffer,
            .format = format,
            .offset = offset,
            .range = range
        };
        return create(create_info);
    }
};

class VulkanImage {
    VkImage handle = VK_NULL_HANDLE;
public:
    VulkanImage() = default;
    VulkanImage(VkImageCreateInfo &create_info) {
        create(create_info);
    }
    VulkanImage(VulkanImage &&other) noexcept {MoveHandle;}
    ~VulkanImage() {DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyImage);}

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // const function
    VkMemoryAllocateInfo memory_alloc_info(VkMemoryPropertyFlags memory_property_flags) const {
        VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        VkMemoryRequirements memory_requirements;
        vkGetImageMemoryRequirements(VulkanCore::get_singleton().get_vulkan_device().get_device(), handle, &memory_requirements);
        alloc_info.allocationSize = memory_requirements.size;
        auto get_memory_type_index = [](uint32_t memory_type_bits, VkMemoryPropertyFlags memory_property_flags) {
            auto& physical_device_memory_properties = VulkanCore::get_singleton().get_vulkan_device().get_physical_device_memory_properties();
            for (size_t i = 0; i < physical_device_memory_properties.memoryTypeCount; ++i) {
                if (memory_type_bits & (1 << i) &&
                    (physical_device_memory_properties.memoryTypes[i].propertyFlags & memory_property_flags) == memory_property_flags)
                    return static_cast<uint32_t>(i);
            }
            return UINT32_MAX;
        };
        alloc_info.memoryTypeIndex = get_memory_type_index(memory_requirements.memoryTypeBits,memory_property_flags);
        if (alloc_info.memoryTypeIndex == UINT32_MAX &&
            memory_property_flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
            alloc_info.memoryTypeIndex = get_memory_type_index(memory_requirements.memoryTypeBits, memory_property_flags & ~VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
        //不在此检查是否成功取得内存类型索引，因为会把memoryAllocateInfo返回出去，交由外部检查
        //if (memoryAllocateInfo.memoryTypeIndex == -1)
        //    outStream << std::format("[ image ] ERROR\nFailed to find any memory type satisfies all desired memory properties!\n");
        return alloc_info;
    }

    result_t bind_memory(VkDeviceMemory memory, VkDeviceSize offset = 0) const {
        VkResult result = vkBindImageMemory(VulkanCore::get_singleton().get_vulkan_device().get_device(), handle, memory, offset);
        if (result)
            outstream << std::format("[ VulkanImage ] ERROR\nFailed to attach the memory!\nError code: {}\n", int32_t(result));
        return result;
    }

    // non-const function
    result_t create(VkImageCreateInfo &create_info) {
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        VkResult result = vkCreateImage(VulkanCore::get_singleton().get_vulkan_device().get_device(), &create_info, nullptr, &handle);
        if (result)
            outstream<< std::format("[ VulkanImage ] ERROR\nFailed to create an image!\nError code: {}\n", int32_t(result));
        return result;
    }
};

class VulkanImageMemory : VulkanImage, VulkanDeviceMemory {
    VkImage handle = VK_NULL_HANDLE;
public:
    VulkanImageMemory() = default;
    VulkanImageMemory(VkImageCreateInfo &create_info, VkMemoryPropertyFlags memory_property_flags) {
        create(create_info, memory_property_flags);
    }
    VulkanImageMemory(VulkanImageMemory &&other) noexcept :
        VulkanImage(std::move(other)), VulkanDeviceMemory(std::move(other)){
        are_bound = other.are_bound;
        other.are_bound = false;
    }
    ~VulkanImageMemory() { are_bound = false; }

    // getter
    [[nodiscard]] VkImage Image() const { return static_cast<const VulkanImage&>(*this); }
    [[nodiscard]] const VkImage* get_address_of_image() const { return VulkanImage::Address(); }
    [[nodiscard]] VkDeviceMemory Memory() const { return static_cast<const VulkanDeviceMemory&>(*this); }
    const VkDeviceMemory* get_address_of_memory() const { return VulkanDeviceMemory::Address(); }
    bool AreBound() const { return are_bound; }
    using VulkanDeviceMemory::get_allocation_size;
    using VulkanDeviceMemory::get_memory_properties;

    // non-const function
    result_t create_image(VkImageCreateInfo &create_info) {
        return VulkanImage::create(create_info);
    }

    result_t allocate_memory(VkMemoryPropertyFlags memory_property_flags) {
        VkMemoryAllocateInfo allocate_info = memory_alloc_info(memory_property_flags);
        if (allocate_info.memoryTypeIndex >= VulkanCore::get_singleton().get_vulkan_device().get_physical_device_memory_properties().memoryTypeCount) {
            return VK_RESULT_MAX_ENUM;
        }
        return allocate(allocate_info);
    }

    result_t bind_memory() {
        if (VkResult result = VulkanImage::bind_memory(Memory()))
            return result;
        are_bound = true;
        return VK_SUCCESS;
    }

    result_t create(VkImageCreateInfo &create_info, VkMemoryPropertyFlags memory_property_flags) {
        VkResult result;
        false ||
            (result = create_image(create_info)) || //用||短路执行
            (result = allocate_memory(memory_property_flags)) ||
            (result = bind_memory());
        return result;
    }
};

class VulkanImageView {
    VkImageView handle = VK_NULL_HANDLE;
public:
    VulkanImageView() = default;
    VulkanImageView(VkImageViewCreateInfo &create_info) {

    }
    VulkanImageView(VulkanImageView &&other) noexcept { MoveHandle;}
    ~VulkanImageView() { DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyImageView); }

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // non-const function
    result_t create(VkImageViewCreateInfo& create_info) {
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        VkResult result = vkCreateImageView(VulkanCore::get_singleton().get_vulkan_device().get_device(),&create_info, nullptr, &handle);
        if (result)
            outstream << std::format("[ VulkanImageView ] ERROR\nFailed to create an image view!\nError code: {}\n", int32_t(result));
        return result;
    }

    result_t create(VkImage image, VkImageViewType view_type, VkFormat format, const VkImageSubresourceRange& subresource_range, VkImageViewCreateFlags flags = 0) {
        VkImageViewCreateInfo create_info = {
            .flags = flags,
            .image = image,
            .viewType = view_type,
            .format = format,
            .subresourceRange = subresource_range
        };
        return create(create_info);
    }
};


class VulkanStagingBuffer {
    // static inline class {
    //     VulkanStagingBuffer* pointer = create();
    //     VulkanStagingBuffer* create() {
    //         static VulkanStagingBuffer staging_buffer;
    //         pointer = &staging_buffer;
    //         VulkanCore::get_singleton().get_vulkan_device().add_callback_destory_device([] { staging_buffer.~VulkanStagingBuffer(); });
    //     }
    // public:
    //     [[nodiscard]] VulkanStagingBuffer& Get() const { return *pointer; }
    // } staging_buffer_main_thread;
    class Holder {
        static VulkanStagingBuffer* Create() {
            static VulkanStagingBuffer staging_buffer;
            VulkanCore::get_singleton().get_vulkan_device().add_callback_destory_device([] { staging_buffer.~VulkanStagingBuffer(); });
            return &staging_buffer;
        }
        VulkanStagingBuffer* pointer;
    public:
        Holder() : pointer(Create()) {}
        VulkanStagingBuffer& Get() const { return *pointer; }
    };
    static inline Holder staging_buffer_main_thread; // 静态成员实例

protected:
    VulkanBufferMemory buffer_memory;
    VkDeviceSize memory_usage = 0;
    VulkanImage aliased_image;
public:
    VulkanStagingBuffer() = default;
    VulkanStagingBuffer(VkDeviceSize size) {
        expand(size);
    }

    // getter
    operator VkBuffer() const { return buffer_memory.Buffer(); }
    const VkBuffer* Address() const { return buffer_memory.AddressOfBuffer(); }
    [[nodiscard]] VkDeviceSize AllocationSize() const { return buffer_memory.get_allocation_size(); }
    VkImage AliasedImage() const { return aliased_image; }

    // const function
    void retrieve_data(void* pData_src, VkDeviceSize size) const {
        buffer_memory.retrieve_data(pData_src, size);
    }

    // non-const function
    void expand(VkDeviceSize size) {
        if (size <= AllocationSize())
            return;
        release();
        VkBufferCreateInfo create_info = {
            .size = size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        };
        buffer_memory.create(create_info,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }

    void release() {
        buffer_memory.~VulkanBufferMemory();
    }

    void* map_memory(VkDeviceSize size) {
        expand(size);
        void *pData_dst = nullptr;
        buffer_memory.map_memory(pData_dst, size);
        memory_usage = size;
        return pData_dst;
    }

    void unmap_memory() {
        buffer_memory.unmap_memory(memory_usage);
        memory_usage = 0;
    }

    void write_buffer_data(const void* pData_src, VkDeviceSize size) {
        expand(size);
        buffer_memory.buffer_data(pData_src, size);
    }

    //该函数创建线性布局的混叠2d图像
    [[nodiscard]]
    VkImage aliased_image2d(VkFormat format, VkExtent2D extent) {
        if (!(VulkanCore::get_singleton().get_vulkan_device().get_format_properties(format).linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
            return VK_NULL_HANDLE;
        }
        VkDeviceSize image_data_size = VkDeviceSize(VulkanCore::get_singleton().get_vulkan_device().get_format_info(format).sizePerPixel)*extent.width*extent.height;
        if (image_data_size>AllocationSize()) return VK_NULL_HANDLE;
        VkImageFormatProperties image_format_properties = {};
        vkGetPhysicalDeviceImageFormatProperties(VulkanCore::get_singleton().get_vulkan_device().get_physical_device(),format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&image_format_properties);
        if (extent.width > image_format_properties.maxExtent.width ||
        extent.height > image_format_properties.maxExtent.height ||
        image_data_size > image_format_properties.maxResourceSize)
            return VK_NULL_HANDLE;
        VkImageCreateInfo image_create_info = {
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED
        };
        aliased_image.~VulkanImage();
        aliased_image.create(image_create_info);
        VkImageSubresource subresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0};
        VkSubresourceLayout subresource_layout = {};
        vkGetImageSubresourceLayout(VulkanCore::get_singleton().get_vulkan_device().get_device(), aliased_image, &subresource, &subresource_layout);
        if (subresource_layout.size != image_data_size)
            return VK_NULL_HANDLE;
        aliased_image.bind_memory(buffer_memory.Memory());
        return aliased_image;

    }

    //Static Function
    static VkBuffer get_buffer_main_thread() {
        return staging_buffer_main_thread.Get();
    }
    static void expand_main_thread(VkDeviceSize size) {
        staging_buffer_main_thread.Get().expand(size);
    }
    static void release_main_thread() {
        staging_buffer_main_thread.Get().release();
    }
    static void* map_memory_main_thread(VkDeviceSize size) {
        return staging_buffer_main_thread.Get().map_memory(size);
    }
    static void unmap_memory_main_thread() {
        staging_buffer_main_thread.Get().unmap_memory();
    }
    static void buffer_data_main_thread(const void* pData_src, VkDeviceSize size) {
        staging_buffer_main_thread.Get().write_buffer_data(pData_src, size);
    }
    static void retrieve_data_main_thread(void* pData_src, VkDeviceSize size) {
        staging_buffer_main_thread.Get().retrieve_data(pData_src, size);
    }
    [[nodiscard]]
    static VkImage aliased_image2d_main_thread(VkFormat format, VkExtent2D extent) {
        return staging_buffer_main_thread.Get().aliased_image2d(format, extent);
    }
};

class VulkanDeviceLocalBuffer {
protected:
    VulkanBufferMemory buffer_memory;
public:
    VulkanDeviceLocalBuffer() = default;
    VulkanDeviceLocalBuffer(VkDeviceSize size, VkBufferUsageFlags desired_usages_without_transfer_dst) {
        create(size, desired_usages_without_transfer_dst);
    }

    // getter
    operator VkBuffer() const { return buffer_memory.Buffer(); }
    const VkBuffer* Address() const { return buffer_memory.AddressOfBuffer(); }
    [[nodiscard]] VkDeviceSize AllocationSize() const { return buffer_memory.get_allocation_size(); }

    // const function
    void transfer_data(const void* pData_src, VkDeviceSize size, VkDeviceSize offset = 0) const {
        if (buffer_memory.get_memory_properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            buffer_memory.buffer_data(pData_src, size, offset);
            return;
        }
        VulkanStagingBuffer::buffer_data_main_thread(pData_src, size);
        auto& command_buffer = VulkanCommand::get_singleton().get_command_buffer_transfer();
        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VkBufferCopy region = { 0, offset, size };
        vkCmdCopyBuffer(command_buffer, VulkanStagingBuffer::get_buffer_main_thread(), buffer_memory.Buffer(), 1, &region);
        command_buffer.end();
        VulkanCommand::get_singleton().execute_command_buffer_graphics(command_buffer);
    }
    //适用于更新不连续的多块数据，stride是每组数据间的步长，这里offset当然是目标缓冲区中的offset
    void transfer_data(const void* pData_src, uint32_t elementCount, VkDeviceSize elementSize, VkDeviceSize stride_src, VkDeviceSize stride_dst, VkDeviceSize offset = 0) const {
        if (buffer_memory.get_memory_properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            void* pData_dst = nullptr;
            buffer_memory.map_memory(pData_dst, stride_dst * elementCount, offset);
            for (size_t i = 0; i < elementCount; i++)
                memcpy(stride_dst * i + static_cast<uint8_t*>(pData_dst), stride_src * i + static_cast<const uint8_t*>(pData_src), size_t(elementSize));
            buffer_memory.unmap_memory(elementCount * stride_dst, offset);
            return;
        }
        VulkanStagingBuffer::buffer_data_main_thread(pData_src, stride_src * elementCount);
        auto& command_buffer = VulkanCommand::get_singleton().get_command_buffer_transfer();
        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        std::unique_ptr<VkBufferCopy[]> regions = std::make_unique<VkBufferCopy[]>(elementCount);
        for (size_t i = 0; i < elementCount; i++)
            regions[i] = { stride_src * i, stride_dst * i + offset, elementSize };
        vkCmdCopyBuffer(command_buffer, VulkanStagingBuffer::get_buffer_main_thread(), buffer_memory.Buffer(), elementCount, regions.get());
        command_buffer.end();
        VulkanCommand::get_singleton().execute_command_buffer_graphics(command_buffer);
    }
    //适用于从缓冲区开头更新连续的数据块，数据大小自动判断
    void transfer_data(const auto& data_src) const {
        transfer_data(&data_src, sizeof data_src);
    }

    // non-const function
    void create(VkDeviceSize size, VkBufferUsageFlags desired_usages_without_transfer_dst) {
        VkBufferCreateInfo create_info = {
            .size = size,
            .usage = desired_usages_without_transfer_dst | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        };
        buffer_memory.create_buffer(create_info) ||
        buffer_memory.allocate_memory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && //&&运算符优先级高于||
        buffer_memory.allocate_memory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        buffer_memory.bind_memory();
    }

    void recreate(VkDeviceSize size, VkBufferUsageFlags desired_usages_without_transfer_dst) {
        VulkanCore::get_singleton().wait_idle();
        buffer_memory.~VulkanBufferMemory();
        create(size, desired_usages_without_transfer_dst);
    }
};

class VulkanVertexBuffer: public VulkanDeviceLocalBuffer {
public:
    VulkanVertexBuffer() = default;
    VulkanVertexBuffer(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) : VulkanDeviceLocalBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |other_usages) {}

    // non-const function
    void create(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) {
        VulkanDeviceLocalBuffer::create(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |other_usages);
    }

    void recreate(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) {
        VulkanDeviceLocalBuffer::recreate(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |other_usages);
    }
};

class VulkanIndexBuffer : public VulkanDeviceLocalBuffer {
public:
    VulkanIndexBuffer() = default;
    VulkanIndexBuffer(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) : VulkanDeviceLocalBuffer(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT  |other_usages) {}

    // non-const function
    void create(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) {
        VulkanDeviceLocalBuffer::create(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT  |other_usages);
    }

    void recreate(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) {
        VulkanDeviceLocalBuffer::recreate(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT  |other_usages);
    }
};

class VulkanUniformBuffer : public VulkanDeviceLocalBuffer {
public:
    VulkanUniformBuffer() = default;
    VulkanUniformBuffer(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) : VulkanDeviceLocalBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT   |other_usages) {}

    // non-const function
    void create(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) {
        VulkanDeviceLocalBuffer::create(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT   |other_usages);
    }

    void recreate(VkDeviceSize size, VkBufferUsageFlags other_usages = 0) {
        VulkanDeviceLocalBuffer::recreate(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT   |other_usages);
    }

    // static function
    static VkDeviceSize calculate_aligned_size(VkDeviceSize data_size) {
        const VkDeviceSize& alignment = VulkanCore::get_singleton().get_vulkan_device().get_physical_device_properties().limits.minUniformBufferOffsetAlignment;
        return data_size + alignment - 1 & ~(alignment - 1);
    }
};

class VulkanAttachment {
protected:
    VulkanImageView image_view;
    VulkanImageMemory image_memory;
    VulkanAttachment() = default;
public:
    // 显式释放附件占用的 image / view / memory。
    // VulkanPipelineManager 里的附件是单例成员，它们的析构发生在 VkDevice 销毁之后；
    // 若不在销毁设备之前调用 release()，这些资源会被 validation 记为 "leaked objects"。
    // 重复调用安全：内部句柄在释放后都已被置空。
    void release() {
        image_view.~VulkanImageView();
        image_memory.~VulkanImageMemory();
    }

    // getter
    VkImageView get_image_view() const {
        return image_view;
    }
    VkImage get_image() const {
        return image_memory.Image();
    }
    const VkImageView* get_address_of_image_view() {
        return  image_view.Address();
    }
    const VkImage* get_address_of_image() {
        return  image_memory.get_address_of_image();
    }

    // const function
    VkDescriptorImageInfo get_descriptor_image_info(VkSampler sampler) const {
        return {
            .sampler = sampler,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }
};

class VulkanColorAttachment : public VulkanAttachment {
public:
    VulkanColorAttachment() = default;
    VulkanColorAttachment(VkFormat format, VkExtent2D extent, uint32_t layer_count = 1,
        VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT, VkImageUsageFlags other_usages = 0) {
        create(format, extent, layer_count, sample_count, other_usages);
    }

    // non-const function
    void create(VkFormat format, VkExtent2D extent, uint32_t layer_count = 1,
        VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT, VkImageUsageFlags other_usages = 0) {
        VkImageCreateInfo image_create_info = {
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = layer_count,
            .samples = sample_count,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | other_usages,
        };
        image_memory.create(image_create_info,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | bool(other_usages & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) * VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
        image_view.create(image_memory.Image(),
            layer_count>1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
            format,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layer_count}
            );
    }

    // static function
    static bool format_availability(VkFormat format, bool support_blending = true) {
        return VulkanCore::get_singleton().get_vulkan_device().get_format_properties(format).optimalTilingFeatures &
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT << uint32_t(support_blending);
    }

};

class VulkanDepthStencilAttachment: public VulkanAttachment {
public:
    VulkanDepthStencilAttachment() = default;
    VulkanDepthStencilAttachment(VkFormat format, VkExtent2D extent, uint32_t layer_count = 1,
        VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT, VkImageUsageFlags other_usages = 0, bool stencil_only = false) {
        create(format, extent, layer_count, sample_count, other_usages, stencil_only);
    }

    // non-const function
    void create(VkFormat format, VkExtent2D extent, uint32_t layer_count = 1,
        VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT, VkImageUsageFlags other_usages = 0, bool stencil_only = false) {
        VkImageCreateInfo image_create_info = {
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = layer_count,
            .samples = sample_count,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | other_usages,
        };
        image_memory.create(image_create_info,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | bool(other_usages & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) * VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
        VkImageAspectFlags aspect_flags = (!stencil_only) * VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format > VK_FORMAT_S8_UINT) aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
        else if (format == VK_FORMAT_S8_UINT) aspect_flags = VK_IMAGE_ASPECT_STENCIL_BIT;
        image_view.create(image_memory.Image(),
            layer_count>1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
            format,
            {aspect_flags, 0, 1, 0, layer_count}
            );
    }

    static bool format_availability(VkFormat format) {
        return VulkanCore::get_singleton().get_vulkan_device().get_format_properties(format).optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
 };

