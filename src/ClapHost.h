#pragma once

#include <clap/clap.h>

// Minimal CLAP host for offline rendering.
// All callbacks that are irrelevant to offline batch processing are stubs.
class ClapHost
{
public:
    ClapHost();
    const clap_host_t* get() const { return &host_; }

private:
    clap_host_t host_;

    static const void* CLAP_ABI getExtension   (const clap_host_t*, const char* id);
    static void        CLAP_ABI requestRestart  (const clap_host_t*) {}
    static void        CLAP_ABI requestProcess  (const clap_host_t*) {}
    static void        CLAP_ABI requestCallback (const clap_host_t*) {}

    static const clap_host_log_t          s_log;
    static const clap_host_thread_check_t s_threadCheck;
    static const clap_host_state_t        s_state;
    static const clap_host_params_t       s_params;
};
