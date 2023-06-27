// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "sigsnoop.h"
#include "compat.bpf.h"
#include "maps.bpf.h"

#define MAX_ENTRIES	10240

const volatile pid_t filtered_pid = 0;
const volatile int target_signal = 0;
const volatile bool failed_only = false;

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, struct event);
} values SEC(".maps");

static __always_inline int probe_entry(pid_t tpid, int sig)
{
	__u64 pid_tgid;
	__u32 pid, tgid;
	struct event event = {};

	if (tpid <= 0)
		return 0;

	if (target_signal && target_signal != sig)
		return 0;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid;
	tgid = pid_tgid >> 32;

	if (filtered_pid && tgid != filtered_pid)
		return 0;

	event.pid = tgid;
	event.tpid = tpid;
	event.sig = sig;
	bpf_get_current_comm(&event.comm, sizeof(event.comm));
	bpf_map_update_elem(&values, &pid, &event, BPF_ANY);
	return 0;
}

static __always_inline int probe_exit(void *ctx, int ret)
{
	__u32 pid = bpf_get_current_pid_tgid();
	struct event *eventp;
	struct event *buf;

	eventp = bpf_map_lookup_and_delete_elem(&values, &pid);
	if (!eventp)
		return 0;

	if (failed_only && ret >= 0)
		return 0;

	buf = reserve_buf(sizeof(struct event));
	if (!buf)
		return 0;

	__builtin_memcpy(buf, eventp, sizeof(struct event));
	buf->ret = ret;

	submit_buf(ctx, buf, sizeof(*buf));
	return 0;
}
