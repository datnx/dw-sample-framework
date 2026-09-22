#include <profiler.h>
#include <imgui.h>
#include <macros.h>
#include <timer.h>
#include <stack>
#include <vector>
#include <logger.h>
#if defined(DWSF_VULKAN)
#    include <extensions_vk.h>
#endif

#define MAX_SAMPLES 256

namespace dw
{
namespace profiler
{
// -----------------------------------------------------------------------------------------------------------------------------------

struct Profiler
{
    struct Sample
    {
        std::string name;
#if defined(DWSF_VULKAN)
        uint32_t query_index;
#else
        SampleType  type;
        std::unique_ptr<gl::Query> query;
        Sample(SampleType T) : type(T)
        {
            if (T == CPU)
                query = nullptr;
            else
                query = std::make_unique<gl::Query>();
        }
#endif
        bool    start = true;
        double  cpu_time;
        Sample* end_sample;
    };

    struct Buffer
    {
        std::vector<std::unique_ptr<Sample>> samples;
        int32_t                              index = 0;
#if defined(DWSF_VULKAN)
        vk::QueryPool::Ptr query_pool;
        uint32_t           query_index = 0;
#endif

        Buffer()
        {
            samples.resize(MAX_SAMPLES);

            for (uint32_t i = 0; i < MAX_SAMPLES; i++)
                samples[i] = nullptr;
        }
    };

    // -----------------------------------------------------------------------------------------------------------------------------------

    Profiler(
#if defined(DWSF_VULKAN)
        vk::Backend::Ptr backend
#endif
    )
    {
#ifdef WIN32
        QueryPerformanceFrequency(&m_frequency);
#endif

#if defined(DWSF_VULKAN)
        for (int i = 0; i < BUFFER_COUNT; i++)
            m_sample_buffers[i].query_pool = vk::QueryPool::create(backend, VK_QUERY_TYPE_TIMESTAMP, MAX_SAMPLES);
#endif
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    ~Profiler()
    {
#if defined(DWSF_VULKAN)
        for (int i = 0; i < BUFFER_COUNT; i++)
            m_sample_buffers[i].query_pool.reset();
#endif
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    double get_current_cpu_time()
    {
#ifdef WIN32
        LARGE_INTEGER cpu_time;
        QueryPerformanceCounter(&cpu_time);
        return cpu_time.QuadPart * (1000000.0 / m_frequency.QuadPart);
#else
        timeval cpu_time;
        gettimeofday(&cpu_time, nullptr);
        return (cpu_time.tv_sec * 1000000.0) + cpu_time.tv_usec;
#endif
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

#if defined(DWSF_VULKAN)
    void begin_sample(std::string name, vk::CommandBuffer::Ptr cmd_buf)
    {
        if (m_should_reset)
        {
            m_sample_buffers[m_write_buffer_idx].query_index = 0;
            vkCmdResetQueryPool(cmd_buf->handle(), m_sample_buffers[m_write_buffer_idx].query_pool->handle(), 0, MAX_SAMPLES);
            m_should_reset = false;
        }

        int32_t idx = m_sample_buffers[m_write_buffer_idx].index++;

        if (!m_sample_buffers[m_write_buffer_idx].samples[idx])
            m_sample_buffers[m_write_buffer_idx].samples[idx] = std::make_unique<Sample>();

        auto& sample = m_sample_buffers[m_write_buffer_idx].samples[idx];

        sample->name = name;

        sample->query_index = m_sample_buffers[m_write_buffer_idx].query_index++;
        vkCmdWriteTimestamp(cmd_buf->handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_sample_buffers[m_write_buffer_idx].query_pool->handle(), sample->query_index);

        VkDebugUtilsLabelEXT debug_label;

        debug_label.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        debug_label.pNext      = nullptr;
        debug_label.pLabelName = name.c_str();
        debug_label.color[0]   = 0.0f;
        debug_label.color[1]   = 1.0f;
        debug_label.color[2]   = 0.0f;
        debug_label.color[3]   = 1.0f;

        vkCmdBeginDebugUtilsLabelEXT(cmd_buf->handle(), &debug_label);

        sample->end_sample = nullptr;
        sample->start      = true;
        sample->cpu_time = get_current_cpu_time();

        m_sample_stack.push(sample.get());
    }

    void end_sample(std::string name, vk::CommandBuffer::Ptr cmd_buf)
    {
        int32_t idx = m_sample_buffers[m_write_buffer_idx].index++;

        if (!m_sample_buffers[m_write_buffer_idx].samples[idx])
            m_sample_buffers[m_write_buffer_idx].samples[idx] = std::make_unique<Sample>();

        auto& sample = m_sample_buffers[m_write_buffer_idx].samples[idx];

        sample->name  = name;
        sample->start = false;

        sample->query_index = m_sample_buffers[m_write_buffer_idx].query_index++;
        vkCmdWriteTimestamp(cmd_buf->handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_sample_buffers[m_write_buffer_idx].query_pool->handle(), sample->query_index);

        vkCmdEndDebugUtilsLabelEXT(cmd_buf->handle());

        sample->end_sample = nullptr;
        sample->cpu_time = get_current_cpu_time();

        Sample* start = m_sample_stack.top();

        start->end_sample = sample.get();

        m_sample_stack.pop();
    }
#else
    void begin_sample(std::string name, SampleType type)
    {
        int32_t idx = m_sample_buffers[m_write_buffer_idx].index++;

        if (!m_sample_buffers[m_write_buffer_idx].samples[idx])
            m_sample_buffers[m_write_buffer_idx].samples[idx] = std::make_unique<Sample>(type);

        auto& sample = m_sample_buffers[m_write_buffer_idx].samples[idx];

        sample->name = name;
        sample->type = type;

        if (type != CPU) sample->query->query_counter(GL_TIMESTAMP);

        sample->end_sample = nullptr;
        sample->start      = true;
        if (type != GPU) sample->cpu_time = get_current_cpu_time();

        m_sample_stack.push(sample.get());
    }

    void end_sample(std::string name, SampleType type)
    {
        int32_t idx = m_sample_buffers[m_write_buffer_idx].index++;

        if (!m_sample_buffers[m_write_buffer_idx].samples[idx])
            m_sample_buffers[m_write_buffer_idx].samples[idx] = std::make_unique<Sample>(type);

        auto& sample = m_sample_buffers[m_write_buffer_idx].samples[idx];

        sample->name  = name;
        sample->type  = type;
        sample->start = false;

        if (sample->type != CPU) sample->query->query_counter(GL_TIMESTAMP);

        sample->end_sample = nullptr;
        if (sample->type != GPU) sample->cpu_time = get_current_cpu_time();

        Sample* start = m_sample_stack.top();

        start->end_sample = sample.get();

        m_sample_stack.pop();
    }
#endif
    // -----------------------------------------------------------------------------------------------------------------------------------

    void begin_frame()
    {
#if defined(DWSF_VULKAN)
        m_should_reset = true;
#endif
        m_read_buffer_idx++;
        m_write_buffer_idx++;

        if (m_read_buffer_idx == BUFFER_COUNT)
            m_read_buffer_idx = 0;

        if (m_write_buffer_idx == BUFFER_COUNT)
            m_write_buffer_idx = 0;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void end_frame()
    {
        if (m_read_buffer_idx >= 0)
            m_sample_buffers[m_read_buffer_idx].index = 0;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

#if defined(DWSF_IMGUI)
    void ui()
    {
        m_current_frame_time.clear();

        if (m_read_buffer_idx >= 0)
        {
            for (int32_t i = 0; i < m_sample_buffers[m_read_buffer_idx].index; i++)
            {
                auto& sample = m_sample_buffers[m_read_buffer_idx].samples[i];

                if (sample->start)
                {
                    if (!m_should_pop_stack.empty())
                    {
                        if (!m_should_pop_stack.top())
                        {
                            m_should_pop_stack.push(false);
                            continue;
                        }
                    }

                    std::string id = std::to_string(i);

                    float gpu_time, cpu_time;

#if defined(DWSF_VULKAN)
                    cpu_time = (sample->end_sample->cpu_time - sample->cpu_time) * 0.001f;
                    m_current_frame_time.emplace_back(cpu_time);

                    uint64_t start_time = 0;
                    uint64_t end_time   = 0;
                    
                    m_sample_buffers[m_read_buffer_idx].query_pool->results(sample->query_index, 1, sizeof(uint64_t), &start_time, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
                    m_sample_buffers[m_read_buffer_idx].query_pool->results(sample->end_sample->query_index, 1, sizeof(uint64_t), &end_time, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

                    uint64_t gpu_time_diff = end_time - start_time;
                             gpu_time      = float(gpu_time_diff / 1000000.0);
                    m_current_frame_time.emplace_back(gpu_time);

                    if (ImGui::TreeNode(id.c_str(), "%s | %f ms (CPU) | %f ms (GPU)", sample->name.c_str(), cpu_time, gpu_time))
                        m_should_pop_stack.push(true);
                    else
                        m_should_pop_stack.push(false);
#else
                    if (sample->type != GPU)
                    {
                        cpu_time = (sample->end_sample->cpu_time - sample->cpu_time) * 0.001f;
                        m_current_frame_time.emplace_back(cpu_time);
                    }

                    if (sample->type != CPU)
                    {
                        uint64_t start_time = 0;
                        uint64_t end_time   = 0;

                        if (!sample->query->result_available()) DW_LOG_WARNING("stall");
                        sample->query->result_64(&start_time);
                        if (!sample->end_sample->query->result_available()) DW_LOG_WARNING("stall");
                        sample->end_sample->query->result_64(&end_time);

                        uint64_t gpu_time_diff = end_time - start_time;
                                 gpu_time      = float(gpu_time_diff / 1000000.0);
                        m_current_frame_time.emplace_back(gpu_time);
                    }

                    bool time_display;
                    if (sample->type == CPU_GPU)
                        time_display = ImGui::TreeNode(id.c_str(), "%s | %f ms (CPU) | %f ms (GPU)", sample->name.c_str(), cpu_time, gpu_time);
                    else if (sample->type == CPU)
                        time_display = ImGui::TreeNode(id.c_str(), "%s | %f ms (CPU)", sample->name.c_str(), cpu_time);
                    else if (sample->type == GPU)
                        time_display = ImGui::TreeNode(id.c_str(), "%s | %f ms (GPU)", sample->name.c_str(), gpu_time);
                    if (time_display) m_should_pop_stack.push(true);
                    else m_should_pop_stack.push(false);
#endif
                }
                else
                {
                    if (!m_should_pop_stack.empty())
                    {
                        bool should_pop = m_should_pop_stack.top();
                        m_should_pop_stack.pop();

                        if (should_pop)
                            ImGui::TreePop();
                    }
                }
            }
        }
    }
#endif

    // -----------------------------------------------------------------------------------------------------------------------------------

    std::vector<std::string> get_names()
    {
        std::vector<std::string> names = {};

        if (m_read_buffer_idx >= 0)
        {
            for (int32_t i = 0; i < m_sample_buffers[m_read_buffer_idx].index; i++)
            {
                auto& sample = m_sample_buffers[m_read_buffer_idx].samples[i];
                if (sample->start)
                {
#if defined(DWSF_VULKAN)
                    names.push_back(sample->name);
#else
                    std::string name = sample->name;
                    if (sample->type == SampleType::CPU_GPU)
                    {
                        names.push_back(name + "_cpu");
                        names.push_back(name + "_gpu");
                    }
                    else names.push_back(name);
#endif
                }
            }
        }

        return names;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    int32_t             m_read_buffer_idx  = -BUFFER_COUNT;
    int32_t             m_write_buffer_idx = -1;
    Buffer              m_sample_buffers[BUFFER_COUNT];
    std::stack<Sample*> m_sample_stack;
    std::stack<bool>    m_should_pop_stack;
    std::vector<float>  m_current_frame_time;

#if defined(DWSF_VULKAN)
    bool m_should_reset = true;
#endif

#ifdef WIN32
    LARGE_INTEGER m_frequency;
#endif
};

Profiler* g_profiler = nullptr;

// -----------------------------------------------------------------------------------------------------------------------------------

#if defined(DWSF_VULKAN)
ScopedProfile::ScopedProfile(std::string name, vk::CommandBuffer::Ptr cmd_buf) : m_name(name)
{
    begin_sample(m_name, cmd_buf);
    m_cmd_buf = cmd_buf;
}

ScopedProfile::~ScopedProfile()
{
    end_sample(m_name, m_cmd_buf);
}

void initialize(vk::Backend::Ptr backend)
{
    g_profiler = new Profiler(backend);
}

void begin_sample(std::string name, vk::CommandBuffer::Ptr cmd_buf)
{
    g_profiler->begin_sample(name, cmd_buf);
}

void end_sample(std::string name, vk::CommandBuffer::Ptr cmd_buf)
{
    g_profiler->end_sample(name, cmd_buf);
}
#else
ScopedProfile::ScopedProfile(std::string name, SampleType type) : m_name(name)
{
    begin_sample(m_name, type);
}

ScopedProfile::~ScopedProfile()
{
    end_sample(m_name, m_type);
}

void initialize()
{
    g_profiler = new Profiler();
}

void begin_sample(std::string name, SampleType type)
{
    g_profiler->begin_sample(name, type);
}

void end_sample(std::string name, SampleType type)
{
    g_profiler->end_sample(name, type);
}
#endif

// -----------------------------------------------------------------------------------------------------------------------------------

void shutdown() { DW_SAFE_DELETE(g_profiler); }

// -----------------------------------------------------------------------------------------------------------------------------------

void begin_frame() { g_profiler->begin_frame(); }

// -----------------------------------------------------------------------------------------------------------------------------------

void end_frame() { g_profiler->end_frame(); }

// -----------------------------------------------------------------------------------------------------------------------------------

#if defined(DWSF_IMGUI)
void ui()
{
    g_profiler->ui();
}
#endif

// -----------------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> get_names()
{
    return g_profiler->get_names();
}

// -----------------------------------------------------------------------------------------------------------------------------------

std::vector<float> get_frame_time()
{
    return g_profiler->m_current_frame_time;
}

// -----------------------------------------------------------------------------------------------------------------------------------
} // namespace profiler
} // namespace dw