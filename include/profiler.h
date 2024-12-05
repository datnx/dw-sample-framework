#pragma once

#include <ogl.h>
#include <memory>
#include <string>

#if defined(DWSF_VULKAN)
#    include "vk.h"
#endif

#if defined(DWSF_VULKAN)
#    define DW_SCOPED_SAMPLE(name, type, cmd_buf) dw::profiler::ScopedProfile __FILE__##__LINE__(name, type, cmd_buf)
#else
#    define DW_SCOPED_SAMPLE(name, type) dw::profiler::ScopedProfile __FILE__##__LINE__(name, type)
#endif

namespace dw
{
namespace profiler
{
enum SampleType
{
    CPU,
    GPU,
    CPU_GPU
};

struct ScopedProfile
{
    ScopedProfile(std::string name, SampleType type = CPU_GPU
#if defined(DWSF_VULKAN)
                  ,
                  vk::CommandBuffer::Ptr cmd_buf
#endif
    );
    ~ScopedProfile();

#if defined(DWSF_VULKAN)
    vk::CommandBuffer::Ptr m_cmd_buf;
#endif
    std::string m_name;
    SampleType  m_type;
};

extern void initialize(
#if defined(DWSF_VULKAN)
    vk::Backend::Ptr backend
#endif
);
extern void shutdown();
extern void begin_sample(std::string name, SampleType type
#if defined(DWSF_VULKAN)
                         ,
                         vk::CommandBuffer::Ptr cmd_buf
#endif
);
extern void end_sample(std::string name, SampleType type
#if defined(DWSF_VULKAN)
                       ,
                       vk::CommandBuffer::Ptr cmd_buf
#endif
);
extern void begin_frame();
extern void end_frame();

#if defined(DWSF_IMGUI)
extern void ui();
#endif

extern std::vector<std::string> get_names();

}; // namespace profiler
} // namespace dw