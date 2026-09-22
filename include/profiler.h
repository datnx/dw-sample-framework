#pragma once

#include <ogl.h>
#include <memory>
#include <string>

#if defined(DWSF_VULKAN)
#    include "vk.h"
#endif

#define DW_SCOPED_SAMPLE(name, ...) dw::profiler::ScopedProfile __FILE__##__LINE__(name, ##__VA_ARGS__)

namespace dw
{
namespace profiler
{
constexpr int BUFFER_COUNT = 4;

#if defined(DWSF_VULKAN)
struct ScopedProfile
{
    ScopedProfile(std::string name, vk::CommandBuffer::Ptr cmd_buf);
    ~ScopedProfile();
    vk::CommandBuffer::Ptr m_cmd_buf;
    std::string m_name;
};

extern void initialize(vk::Backend::Ptr backend);
extern void begin_sample(std::string name, vk::CommandBuffer::Ptr cmd_buf);
extern void end_sample(std::string name, vk::CommandBuffer::Ptr cmd_buf);
#else
enum SampleType
{
    CPU,
    GPU,
    CPU_GPU
};

struct ScopedProfile
{
    ScopedProfile(std::string name, SampleType type = CPU_GPU);
    ~ScopedProfile();
    SampleType  m_type;
    std::string m_name;
};

extern void initialize();
extern void begin_sample(std::string name, SampleType type);
extern void end_sample(std::string name, SampleType type);
#endif

extern void shutdown();
extern void begin_frame();
extern void end_frame();

#if defined(DWSF_IMGUI)
extern void ui();
#endif

extern std::vector<std::string> get_names();
extern std::vector<float>       get_frame_time();

}; // namespace profiler
} // namespace dw