// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "commons.h"
#include "memleak.h"
#include "memleak.skel.h"
#include "trace_helpers.h"

#ifdef USE_BLAZESYM
#include "blazesym.h"
#endif

#include <sys/eventfd.h>
#include <sys/wait.h>
#include <sys/param.h>

#define DEFAULT_MIN_AGE_NS 500

static struct env
{
    int interval;
    int nr_intervals;
    pid_t pid;
    bool pid_from_child;
    bool trace_all;
    bool show_allocs;
    bool combined_only;
    int64_t min_age_ns;
    uint64_t sample_rate;
    int top_stacks;
    size_t min_size;
    size_t max_size;
    char object[32];

    bool wa_missing_free;
    bool percpu;
    int perf_max_stack_depth;
    int stack_map_max_entries;
    long page_size;
    bool kernel_trace;
    bool verbose;
    char command[32];
} env = {
    .interval = 5,
    .nr_intervals = -1,
    .pid = -1,
    .min_age_ns = DEFAULT_MIN_AGE_NS,
    .sample_rate = 1,
    .top_stacks = 10,
    .max_size = -1,
    .perf_max_stack_depth = 127,
    .stack_map_max_entries = 10240,
    .page_size = -1,
    .kernel_trace = true,
};

struct allocation_node
{
    uint64_t address;
    size_t size;
    struct allocation_node *next;
};

struct allocation
{
    uint64_t stack_id;
    size_t size;
    size_t count;
    struct allocation_node *allocations;
};

#define __ATTACH_UPROBE(skel, sym_name, prog_name, is_retprobe)  \
    do                                                           \
    {                                                            \
        LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts,                \
                    .func_name = #sym_name,                      \
                    .retprobe = is_retprobe);                    \
        skel->links.prog_name = bpf_program__attach_uprobe_opts( \
            skel->progs.prog_name,                               \
            env.pid,                                             \
            env.object,                                          \
            0,                                                   \
            &uprobe_opts);                                       \
    } while (false);

#define __CHECK_PROGRAM(skel, prog_name)                   \
    do                                                     \
    {                                                      \
        if (!skel->links.prog_name)                        \
        {                                                  \
            perror("No program attached for " #prog_name); \
            return -errno;                                 \
        }                                                  \
    } while (false);

#define __ATTACH_UPROBE_CHECKED(skel, sym_name, prog_name, is_retprobe) \
    do                                                                  \
    {                                                                   \
        __ATTACH_UPROBE(skel, sym_name, prog_name, is_retprobe);        \
        __CHECK_PROGRAM(skel, prog_name);                               \
    } while (false);

#define ATTACH_UPROBE(skel, sym_name, prog_name) __ATTACH_UPROBE(skel, sym_name, prog_name, false)
#define ATTACH_URETPROBE(skel, sym_name, prog_name) __ATTACH_UPROBE(skel, sym_name, prog_name, true)

#define ATTACH_UPROBE_CHECKED(skel, sym_name, prog_name) __ATTACH_UPROBE_CHECKED(skel, sym_name, prog_name, false)
#define ATTACH_URETPROBE_CHECKED(skel, sym_name, prog_name) __ATTACH_UPROBE_CHECKED(skel, sym_name, prog_name, true)

static volatile sig_atomic_t exiting;
static volatile bool child_exited = false;

static void sig_handler(int signo)
{
    if (signo == SIGCHLD)
        child_exited = 1;

    exiting = 1;
}

const char *argp_program_version = "memleak 0.1";
const char *argp_program_bug_address = "Jackie Liu <liuyun01@kylinos.cn>";
const char argp_program_doc[] =
    "Trace outstanding memory allocations\n"
    "\n"
    "USAGE: memleak [-h] [-c COMMAND] [-p PID] [-t] [-n] [-a] [-o AGE_MS] [-C] [-F] [-s SAMPLE_RATE] [-T TOP_STACKS] [-z MIN_SIZE] [-Z MAX_SIZE] [-O OBJECT] [-P] [INTERVAL] [INTERVALS]\n"
    "\n"
    "EXAMPLES:\n"
    "./memleak -p $(pidof allocs)\n"
    "        Trace allocations and display a summary of 'leaked' (outstanding)\n"
    "        allocations every 5 seconds\n"
    "./memleak -p $(pidof allocs) -t\n"
    "        Trace allocations and display each individual allocator function call\n"
    "./memleak -ap $(pidof allocs) 10\n"
    "        Trace allocations and display allocated addresses, sizes, and stacks\n"
    "        every 10 seconds for outstanding allocations\n"
    "./memleak -c './allocs'\n"
    "        Run the specified command and trace its allocations\n"
    "./memleak\n"
    "        Trace allocations in kernel mode and display a summary of outstanding\n"
    "        allocations every 5 seconds\n"
    "./memleak -o 60000\n"
    "        Trace allocations in kernel mode and display a summary of outstanding\n"
    "        allocations that are at least one minute (60 seconds) old\n"
    "./memleak -s 5\n"
    "        Trace roughly every 5th allocation, to reduce overhead\n"
    "";

#define OPT_PERF_MAX_STACK_DEPTH 1  /* --perf-max-stack-depth */
#define OPT_STACK_MAP_MAX_ENTRIES 2 /* --stack-map-max-entries */

static const struct argp_option opts[] = {
    {"pid", 'p', "PID", 0, "process ID to trace. If not specified, trace kernel allocs"},
    {"trace", 't', 0, 0, "print trace message for each alloc/free alloc"},
    {"show-allocs", 'a', 0, 0, "show allocation addresses and sizes as well as call stacks"},
    {"older", 'o', "AGE_MS", 0, "prune allocations younger than this age in milliseconds"},
    {"command", 'c', "COMMAND", 0, "execute and trace the specified command"},
    {"combined-only", 'C', 0, 0, "show combined allocation statistics only"},
    {"wa-missing-only", 'F', 0, 0, "workaround to alleviate misjudgments when free is missing"},
    {"sample-rate", 's', "SAMPLE_RATE", 0, "sample every N-th allocation to decrease to overhead"},
    {"top", 'T', "TOP_STACKS", 0, "display only this many top allocationg stacks (by size)"},
    {"min-size", 'z', "MIN_SIZE", 0, "capture only allocations larger than this size"},
    {"max-size", 'Z', "MAX_SIZE", 0, "capture only allocations smaller than this size"},
    {"obj", 'O', "OBJECT", 0, "attach to allocator functions in the specified object"},
    {"percpu", 'P', NULL, 0, "trace percpu allocations"},
    {"perf-max-stack-depth", OPT_PERF_MAX_STACK_DEPTH, "PERF_MAX_STACK_DEPTH",
     0, "The limit for both kernel and user stack traces (default 127)"},
    {"stack-map-max-entries", OPT_STACK_MAP_MAX_ENTRIES, "STACK_MAP_MAX_ENTRIES",
     0, "The number of unique stack traces that can be stored and displayed (default 10240)"},
    {NULL, 'h', NULL, OPTION_HIDDEN, "Show this full help"},
    {}};

static uint64_t *stack;
static struct allocation *allocs;
static const char default_object[] = "libc.so.6";