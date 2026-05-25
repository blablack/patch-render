#include "ClapHost.h"

#include <clap/ext/log.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/thread-check.h>
#include <cstdio>
#include <cstring>

//==============================================================================
// Log
//==============================================================================
static void CLAP_ABI hostLog(const clap_host_t*, clap_log_severity sev, const char* msg)
{
    if (sev >= CLAP_LOG_WARNING)
        fprintf(stderr, "[clap] %s\n", msg);
}

const clap_host_log_t ClapHost::s_log = { hostLog };

//==============================================================================
// Thread-check
//==============================================================================
// Single-threaded headless renderer: all calls come from the same thread,
// which serves as both main and audio thread.
static bool CLAP_ABI isMainThread(const clap_host_t*) { return true; }
static bool CLAP_ABI isAudioThread(const clap_host_t*) { return true; }

const clap_host_thread_check_t ClapHost::s_threadCheck = { isMainThread, isAudioThread };

//==============================================================================
// State
//==============================================================================
static void CLAP_ABI markStateDirty(const clap_host_t*) {}

const clap_host_state_t ClapHost::s_state = { markStateDirty };

//==============================================================================
// Params
//==============================================================================
static void CLAP_ABI paramsRescan(const clap_host_t*, clap_param_rescan_flags) {}
static void CLAP_ABI paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
static void CLAP_ABI paramsRequestFlush(const clap_host_t*) {}

const clap_host_params_t ClapHost::s_params = { paramsRescan, paramsClear, paramsRequestFlush };

//==============================================================================
// Extension dispatch
//==============================================================================
const void* CLAP_ABI ClapHost::getExtension(const clap_host_t*, const char* id)
{
    if (strcmp(id, CLAP_EXT_LOG)          == 0) return &s_log;
    if (strcmp(id, CLAP_EXT_THREAD_CHECK) == 0) return &s_threadCheck;
    if (strcmp(id, CLAP_EXT_STATE)        == 0) return &s_state;
    if (strcmp(id, CLAP_EXT_PARAMS)       == 0) return &s_params;
    return nullptr;
}

//==============================================================================
// Constructor
//==============================================================================
ClapHost::ClapHost()
{
    host_.clap_version     = CLAP_VERSION_INIT;
    host_.host_data        = this;
    host_.name             = "patch-render";
    host_.vendor           = "patch-press";
    host_.url              = "";
    host_.version          = "1.0.0";
    host_.get_extension    = getExtension;
    host_.request_restart  = requestRestart;
    host_.request_process  = requestProcess;
    host_.request_callback = requestCallback;
}
