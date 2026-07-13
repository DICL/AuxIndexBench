// No-op shim for environments without gperftools (see profiler.h).
#pragma once
class MallocExtension {
public:
    static MallocExtension* instance() { static MallocExtension e; return &e; }
    void ReleaseFreeMemory() {}
    bool GetNumericProperty(const char*, size_t*) { return false; }
};
