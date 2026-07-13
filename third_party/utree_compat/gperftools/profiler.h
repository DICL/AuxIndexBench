// No-op shim for environments without gperftools.
#pragma once
static inline int  ProfilerStart(const char*) { return 0; }
static inline void ProfilerStop(void) {}
