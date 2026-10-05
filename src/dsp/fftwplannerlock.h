#pragma once
#include <mutex>

// FFT execution on distinct plans is safe; plan creation/destruction is not.
inline std::mutex &wakkaFftwPlannerMutex() {
    static std::mutex mutex;
    return mutex;
}
