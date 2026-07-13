// perfctr.hpp - Thin wrapper over perf_event_open. Degrades gracefully
// when perf_event_paranoid disallows access.

#pragma once
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <vector>

namespace aib {

struct PerfCounter { int fd = -1; uint64_t id = 0; uint64_t value = 0; };

class PerfGroup {
public:
    PerfGroup() = default;

    int add(uint32_t type, uint64_t config, const char* /*label*/) {
        perf_event_attr attr{};
        attr.type           = type;
        attr.size           = sizeof(attr);
        attr.config         = config;
        attr.disabled       = (group_leader_ == -1) ? 1 : 0;
        attr.exclude_kernel = 1;
        attr.exclude_hv     = 1;
        attr.read_format    = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

        int leader = group_leader_;
        int fd = (int)syscall(__NR_perf_event_open, &attr, 0, -1, leader, 0);
        if (fd < 0) {
            available_ = false;
            counters_.push_back({-1, 0, 0});
            return (int)counters_.size() - 1;
        }
        if (group_leader_ == -1) group_leader_ = fd;
        uint64_t id = 0;
        ioctl(fd, PERF_EVENT_IOC_ID, &id);
        counters_.push_back({fd, id, 0});
        return (int)counters_.size() - 1;
    }

    void start() {
        if (group_leader_ < 0) return;
        ioctl(group_leader_, PERF_EVENT_IOC_RESET,  PERF_IOC_FLAG_GROUP);
        ioctl(group_leader_, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }

    void stop() {
        if (group_leader_ < 0) return;
        ioctl(group_leader_, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
        size_t buf_words = 1 + 2 * counters_.size();
        std::vector<uint64_t> buf(buf_words, 0);
        ssize_t r = read(group_leader_, buf.data(), buf.size() * sizeof(uint64_t));
        if (r < 0) return;
        uint64_t nr = buf[0];
        for (uint64_t i = 0; i < nr; ++i) {
            uint64_t value = buf[1 + 2*i];
            uint64_t id    = buf[1 + 2*i + 1];
            for (auto& c : counters_) if (c.id == id) c.value = value;
        }
    }

    uint64_t read_idx(int idx) const {
        if (idx < 0 || idx >= (int)counters_.size()) return 0;
        return counters_[idx].value;
    }
    bool available() const { return available_ && group_leader_ >= 0; }

    ~PerfGroup() { for (auto& c : counters_) if (c.fd >= 0) close(c.fd); }

private:
    int group_leader_ = -1;
    bool available_   = true;
    std::vector<PerfCounter> counters_;
};

} // namespace aib
