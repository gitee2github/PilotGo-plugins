// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "commons.h"
#include "wakeuptime.h"
#include "wakeuptime.skel.h"
#include "trace_helpers.h"

const char *argp_program_version = "wakeuptime 0.1";
const char *argp_program_bug_address = "Jackie Liu <liuyun01@kylinos.cn>";
const char argp_program_doc[] =
"Summarize sleep to wakeup time by waker kernel stack.\n"
"\n"
"USAGE: wakeuptime [-h] [-p PID | -u] [-v] [-m MIN-BLOCK-TIME] "
"[-M MAX-BLOCK-TIME] ]--perf-max-stack-depth] [--stack-storage-size] [duration]\n"
"EXAMPLES:\n"
"       wakeuptime              # trace blocked time with waker stacks\n"
"       wakeuptime 5            # trace for 5 seconds only\n"
"       wakeuptime -u           # don't include kernel threads (user only)\n"
"       wakeuptime -p 185       # trace for PID 185 only\n";

#define OPT_PERF_MAX_STACK_DEPTH	1	/* --perf-max-stack-depth */
#define OPT_STACK_STORAGE_SIZE		2	/* --stack-storage-size */

static const struct argp_option opts[] = {
	{ "pid", 'p', "PID", 0, "Trace this PID only" },
	{ "verbose", 'v', NULL, 0, "Show raw address" },
	{ "user-threads-only", 'u', NULL, 0, "User threads only (no kernel threads)" },
	{ "perf-max-stack-depth", OPT_PERF_MAX_STACK_DEPTH, "PERF_MAX_STACK_DEPTH",
		0, "The limit for both kernel and user stack traces (default 127)" },
	{ "stack-storage-size", OPT_STACK_STORAGE_SIZE, "STACK_STORAGE_SIZE",
		0, "The number of unique stack traces that can be stored and displayed (default 1024)" },
	{ "min-block-time", 'm', "MIN-BLOCK-TIME", 0,
		"The amount of time in microseconds over which we store traces (default 1)" },
	{ "max-block-time", 'M', "MAX-BLOCK-TIME", 0,
		"The amount of time in microseconds under which we store traces (default U64_MAX)" },
	{ NULL, 'h', NULL, OPTION_HIDDEN, "Show the full help" },
	{}
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	static int pos_args;

	switch (key) {
	case 'h':
		argp_state_help(state, stderr, ARGP_HELP_STD_HELP);
		break;
	case 'v':
		env.verbose = true;
		break;
	case 'u':
		env.user_threads_only = true;
		break;
	case 'p':
		env.pid = argp_parse_pid(key, arg, state);
		break;
         
	}
	
	return 0;
}

int main(int argc, char *argv[])
{
	static const struct argp argp = {
		.options = opts,
		.parser = parse_arg,
		.doc = argp_program_doc,
	};

	int err;

	err = argp_parse(&argp, argc, argv, 0, NULL, NULL);
	if (err)
		return err;

	return err != 0;
}
