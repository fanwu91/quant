#include <iostream>
#include <thread>
#include <pthread.h>
#include <sched.h>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
// bind thread to specific CPU core
void bind_thread_to_core(std::thread& t, int cpu_core) {
    cpu_set_t cpuset;
void bind_thread_to_core(std::thread& t, int cpu_core) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpuset), &cpuset);
#else
    // Thread affinity not supported on macOS/other platforms
    (void)t;
    (void)cpu_core;
#endif
}

}

int main() {

    return 0;
}

