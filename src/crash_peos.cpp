// Rift Ascend's crash handler on PeOS (src/core/crash.h): the PeOS port,
// docs/rift-ascend-port-plan.md, phase D. GPL-3.0, as Rift Ascend.
//
// crash.cpp is Linux's: POSIX signals with siginfo, the registers from
// ucontext, an alternate stack, backtrace. PeOS has none of that for
// programs: a fault ends the program and the kernel writes it in its log
// ("USER FAULT", where and which address). What is kept here: the program's
// last steps (crash::step), written to the log with an exception that
// reaches the top (reportException, std::terminate), and in a text report
// in the directory given to install, as crash.cpp does.
#include "core/crash.h"

#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <typeinfo>

#include <cxxabi.h>

namespace crash {
namespace {

constexpr int kStepLines = 16;
constexpr size_t kStepLength = 240;

std::mutex g_lock;
char g_steps[kStepLines][kStepLength];
int g_next_step = 0;
unsigned long g_step_count = 0;
bool g_installed = false;
std::string g_report_path;

std::string readableName(const char* mangled) {
    int status = 0;
    char* name = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    std::string out = status == 0 && name ? name : mangled;
    std::free(name);
    return out;
}

// The steps, oldest first, one per line (g_lock held).
std::string stepsText() {
    std::string out;
    const int count = g_step_count < kStepLines ? int(g_step_count) : kStepLines;
    for (int i = 0; i < count; ++i) {
        const int index = (g_next_step - count + i + kStepLines) % kStepLines;
        out += "  ";
        out += g_steps[index];
        out += '\n';
    }
    return out;
}

void writeReport(const std::string& message) {
    std::string steps;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        steps = stepsText();
    }
    LOG_ERROR(logging::Category::Core, "{}; last steps:\n{}", message, steps);
    logging::flush();
    if (g_report_path.empty()) return;
    if (std::FILE* f = std::fopen(g_report_path.c_str(), "w")) {
        std::fprintf(f, "%s\n\nLast steps:\n%s\n(PeOS: no stack trace; a fault is in the kernel's log as USER FAULT)\n",
                     message.c_str(), steps.c_str());
        std::fclose(f);
    }
}

void onTerminate() {
    std::string message = "std::terminate";
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            message += ": " + readableName(typeid(e).name()) + ": " + e.what();
        } catch (...) {
            message += ": not a std::exception";
        }
    }
    writeReport(message);
    std::abort();
}

}  // namespace

void install(const char* process_tag, const char* directory) {
    if (g_installed) return;
    g_installed = true;
    // A relative folder ("logs", as the client asks) would be inside the
    // app's bundle, which is read-only on PeOS: the app's own folder in
    // /starlight/config instead, beside its log (2026-11-14: no crash file).
    std::string dir = directory && *directory == '/' ? directory : "/starlight/config/rift";
    g_report_path = dir + "/crash-" + (process_tag && *process_tag ? process_tag : "app") + ".txt";
    std::set_terminate(onTerminate);
    LOG_INFO(logging::Category::Core, "crash handler: {} (PeOS: steps and exceptions; faults in the kernel's log)",
             g_report_path);
}

void testCrash() {
    LOG_WARN(logging::Category::Core, "crash handler: forced test dump");
    writeReport("forced test dump");
    std::abort();
}

void step(std::string_view text) {
    std::lock_guard<std::mutex> guard(g_lock);
    char* line = g_steps[g_next_step];
    size_t length = text.size() < kStepLength - 1 ? text.size() : kStepLength - 1;
    std::memcpy(line, text.data(), length);
    line[length] = '\0';
    for (size_t i = 0; i < length; ++i)
        if (line[i] == '\r' || line[i] == '\n') line[i] = ' ';
    g_next_step = (g_next_step + 1) % kStepLines;
    ++g_step_count;
}

void reportException(const char* where, const std::exception* exception) {
    std::string message = std::string("C++ exception caught in ") + (where && *where ? where : "?") + ": ";
    if (exception)
        message += readableName(typeid(*exception).name()) + ": " + exception->what();
    else
        message += "(not a std::exception)";
    writeReport(message);
}

}  // namespace crash
