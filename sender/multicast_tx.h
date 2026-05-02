#ifndef MULTICAST_TX_H
#define MULTICAST_TX_H

#include <netinet/in.h>
#include <stdint.h>

/*
 * macOS/BSD multicast send defaults to errno 65 (EHOSTUNREACH) unless
 * setsockopt(IP_MULTICAST_IF) names a LAN IPv4 — the kernel will not infer an
 * egress interface the way Linux often does.
 *
 * IPv4 TTL for multicast hops is explicitly set to 1 (subnet scope).
 *
 * Optional env: MCAST_IF=<ipv4>, MCAST_RELAX_SRC=1 (allow non‑RFC1918 egress),
 * MULTICAST_DEBUG=1.
 */

/*
 * iface_ip_opt_arg: optional egress IPv4; NULL/empty ⇒ MCAST_IF then auto (prefers en0).
 * multicast_group_dotted: destination multicast group string.
 * mcast_port_host_order: UDP port as host-endian (e.g. 5433).
 *
 * Call immediately after socket(PF_INET, SOCK_DGRAM, 0) on that same fd — before bind/sendto.
 * Validates group is 224–239.x.x.x; validates egress not 0 / 127 / uninitialized.
 *
 * Returns 0 on success, -1 after logging to stderr (abort callers on -1).
 */
int multicast_tx_configure_ipv4(int sock, const char *iface_ip_opt_arg,
    const char *multicast_group_dotted, uint16_t mcast_port_host_order);

/* errno must reflect sendto failure; multicast_tx_warn_sendto logs group, iface IP, FD. */
void multicast_tx_warn_sendto(const char *ctx);

/* When non-zero: no stdout/stderr from this module (demo / polished UI). */
void multicast_tx_set_demo_quiet(int on);

#endif
