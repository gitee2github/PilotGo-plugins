// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_endian.h>
#include "bindsnoop.h"

#define MAX_ENTRIES	10240
#define MAX_PORTS	1024

const volatile bool filter_memcg = false;
const volatile pid_t target_pid = 0;
const volatile bool ignore_errors = true;
const volatile bool filter_by_port = false;

static int probe_entry(struct pt_regs *ctx, struct socket *socket)
{
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	pid_t tgid = pid_tgid >> 32;
	pid_t pid = (pid_t)pid_tgid;

	if (target_pid && target_pid != tgid)
		return 0;

	bpf_map_update_elem(&sockets, &pid, &socket, BPF_ANY);
	return 0;
}

static int probe_exit(struct pt_regs *ctx, short ver)
{
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	pid_t tgid = pid_tgid >> 32;
	pid_t pid = (pid_t)pid_tgid;
	struct socket **socketp, *socket;
	struct inet_sock *inet_sock;
	struct sock *sock;
	union bind_options opts;
	struct bind_event event = {};
	__u16 sport = 0, *port;
	int ret;

	socketp = bpf_map_lookup_elem(&sockets, &pid);
	if (!socketp)
		return 0;

	ret = PT_REGS_RC(ctx);
	if (ignore_errors && ret != 0)
		goto cleanup;

	socket = *socketp;
	sock = BPF_CORE_READ(socket, sk);
	inet_sock = (struct inet_sock *)sock;

	sport = bpf_ntohs(BPF_CORE_READ(inet_sock, inet_sport));
	port = bpf_map_lookup_elem(&ports, &sport);
	if (filter_by_port && !port)
		goto cleanup;

	opts.fields.freebind = BPF_CORE_READ_BITFIELD_PROBED(inet_sock, freebind);
	opts.fields.transparent = BPF_CORE_READ_BITFIELD_PROBED(inet_sock, transparent);
	opts.fields.bind_address_no_port = BPF_CORE_READ_BITFIELD_PROBED(inet_sock, bind_address_no_port);
	opts.fields.reuseaddress = BPF_CORE_READ_BITFIELD_PROBED(sock, __sk_common.skc_reuse);
	opts.fields.reuseport = BPF_CORE_READ_BITFIELD_PROBED(sock, __sk_common.skc_reuseport);
	event.opts = opts.data;
	event.ts_us = bpf_ktime_get_ns() / 1000;
	event.pid = tgid;
	event.port = sport;
	event.bound_dev_if = BPF_CORE_READ(sock, __sk_common.skc_bound_dev_if);
	event.ret = ret;
	event.proto = BPF_CORE_READ_BITFIELD_PROBED(sock, sk_protocol);
	bpf_get_current_comm(&event.task, sizeof(event.task));
	event.ver = ver;
	if (ver == 4)
		BPF_CORE_READ_INTO(&event.addr, inet_sock, inet_saddr);
	else
		BPF_CORE_READ_INTO(&event.addr, sock, __sk_common.skc_v6_rcv_saddr.in6_u.u6_addr32);

	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &event, sizeof(event));

cleanup:
	bpf_map_delete_elem(&sockets, &pid);
	return 0;
}