// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "commons.h"
#include "blk_types.h"
#include "biosnoop.h"
#include "biosnoop.skel.h"
#include "trace_helpers.h"

static volatile sig_atomic_t exiting;

static struct env {
	char *disk;
	int duration;
	bool timestamp;
	bool queued;
	bool verbose;
	char *cgroupspath;
	bool cg;
} env;

static volatile __u64 start_ts;

const char *argp_program_version = "biosnoop 0.1";
const char *argp_program_bug_address = "Jackie Liu <liuyun01@kylinos.cn>";
const char argp_program_doc[] =
"Trace block I/O.\n"
"\n"
"USAGE: biosnoop [--help] [-d DISK] [-c CG] [-Q]\n"
"\n"
"EXAMPLES:\n"
"    biosnoop              # trace all block I/O\n"
"    biosnoop -Q           # include OS queued time in I/O time\n"
"    biosnoop 10           # trace for 10 seconds only\n"
"    biosnoop -d sdc       # trace sdc only\n"
"    biosnoop -c CG        # Trace process under cgroupsPath CG\n";

static const struct argp_option opts[] = {
	{ "queued", 'Q', NULL, 0, "Include OS queued time in I/O time" },
	{ "disk", 'd', "DISK", 0, "Trace this disk only" },
	{ "verbose", 'v', NULL, 0, "Verbose debug output" },
	{ "cgroup", 'c', "/sys/fs/cgroup/unified/CG", 0, "Trace process in cgroup path" },
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
	case 'Q':
		env.queued = true;
		break;
	case 'c':
		env.cg = true;
		env.cgroupspath = arg;
		break;
	case 'd':
		env.disk = arg;
		if (strlen(arg) + 1 > DISK_NAME_LEN) {
			warning("Invalid disk name %s: too long\n", arg);
			argp_usage(state);
		}
		break;
	case ARGP_KEY_ARG:
		errno = 0;
		if (pos_args == 0) {
			env.duration = strtoll(arg, NULL, 10);
			if (errno || env.duration <= 0) {
				warning("Invalid delay (in us): %s\n", arg);
				argp_usage(state);
			}
		} else {
			warning("Unrecognized positional argument: %s\n", arg);
			argp_usage(state);
		}
		pos_args++;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
			   va_list args)
{
	if (level == LIBBPF_DEBUG && !env.verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sig_handler(int sig)
{
	exiting = 1;
}

static void blk_fill_rwbs(char *rwbs, unsigned int op)
{
	int i = 0;

	if (op & REQ_PREFLUSH)
		rwbs[i++] = 'F';

	switch (op & REQ_OP_MASK) {
	case REQ_OP_WRITE:
	case REQ_OP_WRITE_SAME:
		rwbs[i++] = 'W';
		break;
	case REQ_OP_DISCARD:
		rwbs[i++] = 'D';
		break;
	case REQ_OP_SECURE_ERASE:
		rwbs[i++] = 'D';
		rwbs[i++] = 'E';
		break;
	case REQ_OP_FLUSH:
		rwbs[i++] = 'F';
		break;
	case REQ_OP_READ:
		rwbs[i++] = 'R';
		break;
	default:
		rwbs[i++] = 'N';
		break;
	}

	if (op & REQ_FUA)
		rwbs[i++] = 'F';
	if (op & REQ_RAHEAD)
		rwbs[i++] = 'A';
	if (op & REQ_SYNC)
		rwbs[i++] = 'S';
	if (op & REQ_META)
		rwbs[i++] = 'M';

	rwbs[i] = '\0';
}

static struct partitions *partitions;

void handle_event(void *ctx, int cpu, void *data, __u32 data_sz)
{
	const struct partition *partition;
	const struct event *e = data;
	char rwbs[RWBS_LEN];

	if (!start_ts)
		start_ts = e->ts;

	blk_fill_rwbs(rwbs, e->cmd_flags);
	partition = partitions__get_by_dev(partitions, e->dev);
	printf("%-11.6f %-14.14s %-7d %-7s %-4s %-10lld %-7d ",
	       (e->ts - start_ts) / 1000000000.0,
	       e->comm, e->pid, partition ? partition->name : "Unknown", rwbs,
	       e->sector, e->len);
	if (env.queued)
		printf("%7.3f ", e->qdelta != -1 ?
			e->qdelta / 1000000.0 : -1);
	printf("%7.3f\n", e->delta / 1000000.0);
}

void handle_lost_events(void *ctx, int cpu, __u64 lost_cnt)
{
	warning("lost %llu events on CPU #%d!\n", lost_cnt, cpu);
}