/*
 * Darwin/BSD: multicast UDP send fails with errno 65 (EHOSTUNREACH) unless this process
 * sets IP_MULTICAST_IF to a concrete local IPv4 before sendto(); macOS does not pick
 * a LAN egress implicitly. Applied on the same SOCK_DGRAM socket used for sendto().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "multicast_tx.h"

#define ENV_MCAST_IF      "MCAST_IF"
#define ENV_MCAST_RELAX   "MCAST_RELAX_SRC"
#define ENV_MCAST_DBG     "MULTICAST_DEBUG"

static int multicast_tx_demo_quiet;

void
multicast_tx_set_demo_quiet(int on)
{
	multicast_tx_demo_quiet = on ? 1 : 0;
}

#define MC_LOG(...)	do {						\
		if (!multicast_tx_demo_quiet)			\
			fprintf(stderr, __VA_ARGS__);		\
	} while (0)
#define MC_OUT(...)	do {						\
		if (!multicast_tx_demo_quiet)			\
			printf(__VA_ARGS__);			\
	} while (0)
#define MC_PERR(M)	do {						\
		if (!multicast_tx_demo_quiet)			\
			perror(M);				\
	} while (0)

/* Last successful configure (for sendto diagnostics). Not thread‑safe — fine for CLI tools). */
static int g_cfg_ok;
static int g_sock_fd = -1;
static char g_egress_ip[INET_ADDRSTRLEN];
static char g_group_ip[INET_ADDRSTRLEN];
static uint16_t g_udp_port_host;

static int
debug_mc(void)
{
	const char *d = getenv(ENV_MCAST_DBG);

	if (multicast_tx_demo_quiet)
		return 0;
	return d != NULL && d[0] != '\0';
}

static uint32_t
nh32(in_addr_t a)
{
	return (uint32_t)ntohl((uint32_t)a);
}

/* True if 224.0.0.0 – 239.255.255.255 */
static int
ipv4_is_multicast_grp(uint32_t h)
{
	uint32_t oct0;

	oct0 = h >> 24;
	return oct0 >= 224U && oct0 <= 239U;
}

/*
 * Typical LAN/private or link‑local egress (RFC1918 + 169.254/16).
 */
static int
ipv4_site_scope_ok(uint32_t h)
{

	if ((h >> 24) == 10U)
		return 1;
	/* 192.168/16 */
	if ((h >> 16) == 0xC0A8U)
		return 1;
	/* 172.16–31./12 */
	if (h >= 0xAC100000UL && h <= 0xAC1FFFFFUL)
		return 1;
	/* 169.254/16 link‑local */
	if ((h >> 16) == 0xA9FEU)
		return 1;
	return 0;
}

static void
remember_ctx(int sockfd, const char *iface_ip, const char *grp_ip, uint16_t port_h)
{

	g_sock_fd = sockfd;
	strncpy(g_egress_ip, iface_ip, sizeof(g_egress_ip) - 1);
	g_egress_ip[sizeof(g_egress_ip) - 1] = '\0';
	strncpy(g_group_ip, grp_ip, sizeof(g_group_ip) - 1);
	g_group_ip[sizeof(g_group_ip) - 1] = '\0';
	g_udp_port_host = port_h;
	g_cfg_ok = 1;
}

static void
forget_ctx(void)
{

	g_cfg_ok = 0;
	g_sock_fd = -1;
	g_egress_ip[0] = '\0';
	g_group_ip[0] = '\0';
	g_udp_port_host = 0;
}

static int
pick_sock_inet(int sock)
{
	struct sockaddr_storage ss;
	socklen_t sl;

	sl = sizeof(ss);
	memset(&ss, 0, sizeof(ss));
	if (getsockname(sock, (struct sockaddr *)(void *)&ss, &sl) != 0) {
		MC_PERR("Multicast: getsockname(verify PF_INET UDP)");
		return -1;
	}
	if (ss.ss_family != AF_INET) {
		MC_LOG(
		    "Multicast: socket must be AF_INET for IPv4 multicast (got ss_family=%d).\n",
		    (int)ss.ss_family);
		return -1;
	}
	return 0;
}

/* Find first ifaddrs entry matching IPv4 addr */
static int
find_ifaddrs_for_ipv4(const struct ifaddrs *list, struct in_addr addr,
    unsigned int *flags_out, const char **name_out)
{
	const struct ifaddrs *ia;

	for (ia = list; ia != NULL; ia = ia->ifa_next) {
		const struct sockaddr_in *sin;

		if (ia->ifa_addr == NULL ||
		    ia->ifa_addr->sa_family != AF_INET)
			continue;
		sin = (const struct sockaddr_in *)(const void *)
		    ia->ifa_addr;
		if (memcmp(&sin->sin_addr, &addr, sizeof(addr)) != 0)
			continue;
		if (flags_out != NULL)
			*flags_out = (unsigned int)ia->ifa_flags;
		if (name_out != NULL)
			*name_out = ia->ifa_name;
		return 0;
	}
	return -1;
}

static int
iface_ok_candidate(unsigned int fl)
{

	return (fl & IFF_UP) != 0 && (fl & IFF_MULTICAST) != 0 &&
	    (fl & IFF_LOOPBACK) == 0;
}

static int
egress_numeric_ok(uint32_t h)
{

	if (h == 0) {
		MC_LOG(
		    "Multicast: egress IP 0.0.0.0 is invalid — pick a LAN address.\n");
		return -1;
	}
	if ((h >> 24) == 127U) {
		MC_LOG(
		    "Multicast: egress 127.x.x.x (loopback) rejected for LAN multicast "
		    "(set MCAST_IF to Wi‑Fi/Ethernet IPv4).\n");
		return -1;
	}
	if (ipv4_is_multicast_grp(h)) {
		MC_LOG(
		    "Multicast: egress must not be a multicast address.\n");
		return -1;
	}
	if (!ipv4_site_scope_ok(h)) {
		const char *rel = getenv(ENV_MCAST_RELAX);

		if (rel == NULL || strcmp(rel, "1") != 0)
			return -2; /* skip candidate or bail for explicit CLI */
	}
	return 0;
}

static int
try_iface_list(const struct ifaddrs *head, struct in_addr *out_mif,
    const char **out_name, unsigned int *out_sf, const char *match_name)
{
	const struct ifaddrs *ia;

	for (ia = head; ia != NULL; ia = ia->ifa_next) {
		uint32_t a_host;
		const struct sockaddr_in *sin;
		int ev;

		if (ia->ifa_addr == NULL ||
		    ia->ifa_addr->sa_family != AF_INET)
			continue;
		if (match_name != NULL &&
		    strcmp(ia->ifa_name, match_name) != 0)
			continue;
		if (!iface_ok_candidate((unsigned int)ia->ifa_flags))
			continue;

		sin = (const struct sockaddr_in *)(const void *)
		    ia->ifa_addr;
		a_host = ntohl((uint32_t)sin->sin_addr.s_addr);

		if (a_host == 0 || (a_host >> 24) == 127)
			continue;

		ev = egress_numeric_ok(a_host);
		if (ev == -2)
			continue; /* e.g. public IP without MCAST_RELAX_SRC=1 */
		if (ev != 0)
			continue;

		memcpy(out_mif, &sin->sin_addr, sizeof(*out_mif));
		*out_name = ia->ifa_name;
		*out_sf = (unsigned int)ia->ifa_flags;
		return 0;
	}
	return -1;
}

static int
auto_select_iface(struct ifaddrs *ifaddrs, struct in_addr *mif,
    const char **name, unsigned int *sf)
{
	static const char *prefer[] = { "en0", "en1", NULL };
	size_t pi;

	for (pi = 0; prefer[pi] != NULL; pi++) {
		if (try_iface_list(ifaddrs, mif, name, sf, prefer[pi]) == 0)
			return 0;
	}
	return try_iface_list(ifaddrs, mif, name, sf, NULL);
}

int
multicast_tx_configure_ipv4(int sock, const char *iface_ip_opt_arg,
    const char *multicast_group_dotted, uint16_t mcast_port_host_order)
{
	const unsigned char ttl = 1;
	const char *chosen_src = "auto-select";
	const char *ip_opt;
	struct in_addr mif_addr;
	struct in_addr grp;
	struct ifaddrs *ifaddrs;
	unsigned int sel_sf = 0;
	const char *sel_name = "?";
	char ebuf[INET_ADDRSTRLEN];
	char gbuf[INET_ADDRSTRLEN];
	uint32_t gh;
	int rg;

	memset(&mif_addr, 0, sizeof(mif_addr));
	forget_ctx();

	if (sock < 0 || multicast_group_dotted == NULL ||
	    multicast_group_dotted[0] == '\0') {
		MC_LOG(
		    "Multicast: invalid sock or multicast group string.\n");
		return -1;
	}
	if (pick_sock_inet(sock) != 0)
		return -1;

	if (inet_pton(AF_INET, multicast_group_dotted, &grp) != 1) {
		MC_LOG(
		    "Multicast: bad multicast_address \"%s\".\n",
		    multicast_group_dotted);
		return -1;
	}
	if (!inet_ntop(AF_INET, &grp, gbuf, sizeof(gbuf))) {
		MC_LOG("Multicast: group inet_ntop failed.\n");
		return -1;
	}

	gh = nh32(grp.s_addr);
	if (!ipv4_is_multicast_grp(gh)) {
		MC_LOG(
		    "Multicast: destination \"%s\" is not IPv4 multicast "
		    "(expected 224.0.0.0–239.255.255.255).\n",
		    gbuf);
		return -1;
	}

	ip_opt = iface_ip_opt_arg;
	if (ip_opt != NULL && ip_opt[0] != '\0')
		chosen_src = "argv";
	else {
		ip_opt = getenv(ENV_MCAST_IF);
		if (ip_opt != NULL && ip_opt[0] != '\0')
			chosen_src = ENV_MCAST_IF;
	}

	if (getifaddrs(&ifaddrs) != 0) {
		MC_PERR("Multicast: getifaddrs");
		return -1;
	}

	if (ip_opt != NULL && ip_opt[0] != '\0') {
		unsigned int uf = 0;
		const char *ifn = NULL;
		uint32_t eh;

		rg = inet_pton(AF_INET, ip_opt, &mif_addr);
		if (rg != 1) {
			MC_LOG(
			    "Multicast: invalid IPv4 \"%s\" (%s).\n",
			    ip_opt,
			    iface_ip_opt_arg != NULL &&
			    iface_ip_opt_arg[0] != '\0' ? "CLI" :
			    ENV_MCAST_IF);
			freeifaddrs(ifaddrs);
			return -1;
		}
		eh = nh32(mif_addr.s_addr);
		rg = egress_numeric_ok(eh);
		if (rg != 0) {
			if (rg == -2) {
				char ib[INET_ADDRSTRLEN];

				if (!inet_ntop(AF_INET, &mif_addr, ib, sizeof(ib)))
					snprintf(ib, sizeof(ib), "?");
				MC_LOG(
				    "Multicast: rejecting egress %s (non‑RFC1918 "
				    "without %s=1).\n",
				    ib, ENV_MCAST_RELAX);
			}
			freeifaddrs(ifaddrs);
			return -1;
		}

		if (!inet_ntop(AF_INET, &mif_addr, ebuf, sizeof(ebuf)))
			snprintf(ebuf, sizeof(ebuf), "?");

		if (find_ifaddrs_for_ipv4(ifaddrs, mif_addr, &uf, &ifn) != 0) {
			MC_LOG(
			    "Multicast: no interface owns IPv4 %s.\n", ebuf);
			freeifaddrs(ifaddrs);
			return -1;
		}
		if ((uf & IFF_UP) == 0) {
			MC_LOG(
			    "Multicast: interface \"%s\" (IP %s) is not IFF_UP.\n",
			    ifn != NULL ? ifn : "?", ebuf);
			freeifaddrs(ifaddrs);
			return -1;
		}
		if ((uf & IFF_MULTICAST) == 0) {
			MC_LOG(
			    "Multicast: interface \"%s\" lacks IFF_MULTICAST.\n",
			    ifn != NULL ? ifn : "?");
			freeifaddrs(ifaddrs);
			return -1;
		}
		sel_sf = uf;
		sel_name = ifn;
	} else {
		if (auto_select_iface(ifaddrs, &mif_addr, &sel_name, &sel_sf) != 0) {
			MC_LOG(
			    "Multicast: no eligible interface (IFF_UP+IFF_MULTICAST, "
			    "non‑loopback). Set %s=<lan_ip>.\n",
			    ENV_MCAST_IF);
			freeifaddrs(ifaddrs);
			return -1;
		}
		if (!inet_ntop(AF_INET, &mif_addr, ebuf, sizeof(ebuf)))
			snprintf(ebuf, sizeof(ebuf), "?");
	}

	freeifaddrs(ifaddrs);

	MC_OUT("Using multicast interface: %s\n", ebuf);
	MC_OUT("Sending to multicast group: %s:%u\n",
	    gbuf, (unsigned)mcast_port_host_order);
	MC_OUT("Socket FD: %d\n", sock);
	if (debug_mc())
		MC_OUT("Multicast: egress chosen via %s, OS ifname=%s\n",
		    chosen_src, sel_name != NULL ? sel_name : "?");

	if (debug_mc()) {
		MC_LOG(
		    "[MULTICAST_DEBUG] if=%s if_flags=0x%x ttl=%u mif_raw=0x%08x "
		    "group_raw=0x%08x\n",
		    sel_name != NULL ? sel_name : "?",
		    sel_sf, (unsigned int)ttl,
		    (unsigned)ntohl((uint32_t)mif_addr.s_addr),
		    (unsigned)ntohl((uint32_t)grp.s_addr));
	}

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &mif_addr,
	    sizeof(mif_addr)) != 0) {
		MC_LOG(
		    "Multicast: setsockopt(IP_MULTICAST_IF) errno=%d (%s).\nAborting "
		    "(egress \"%s\", fd %d).\n",
		    errno, strerror(errno), ebuf, sock);
		if (errno == EHOSTUNREACH || errno == ENETUNREACH)
			MC_LOG(
			    "Multicast: EHOSTUNREACH (macOS often errno 65): invalid "
			    "multicast egress or routing — verify LAN IP \"%s\" on fd "
			    "%d.\n",
			    ebuf, sock);
		return -1;
	}

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) !=
	    0) {
		MC_LOG(
		    "Multicast: setsockopt(IP_MULTICAST_TTL) errno=%d (%s).\n",
		    errno, strerror(errno));
		return -1;
	}

	MC_OUT("Multicast: applied IP_MULTICAST_IF and IP_MULTICAST_TTL=%u OK.\n",
	    (unsigned int)ttl);

	remember_ctx(sock, ebuf, gbuf, mcast_port_host_order);
	return 0;
}

void
multicast_tx_warn_sendto(const char *ctx)
{
	int err;

	if (multicast_tx_demo_quiet)
		return;

	err = errno;
	if (ctx != NULL && ctx[0] != '\0')
		MC_LOG("%s: errno=%d (%s)\n", ctx, err, strerror(err));
	else
		MC_LOG("%s\n", strerror(err));

	MC_LOG(
	    "Multicast: send failed: errno=%d (%s).\n",
	    err, strerror(err));

	if (g_cfg_ok != 0) {
		MC_LOG(
		    "  Sending to multicast group: [%s:%u]\n",
		    g_group_ip, (unsigned)g_udp_port_host);
		MC_LOG(
		    "  Using multicast interface: %s\n", g_egress_ip);
		MC_LOG("  Socket FD: %d\n", g_sock_fd);
	} else {
		MC_LOG(
		    "  (No stored multicast_tx context — call "
		    "multicast_tx_configure_ipv4 on this fd before sendto.)\n");
	}

	if (err == EHOSTUNREACH || err == ENETUNREACH
#ifdef __APPLE__
	    || err == 65
#endif
	    )
		MC_LOG(
		    "  Likely cause: EHOSTUNREACH / ENETUNREACH (Darwin: errno 65 = "
		    "EHOSTUNREACH = \"No route to host\").\n"
		    "  Fix egress (IP multicast interface option): export %s=<LAN IPv4> "
		    "or pass it after multicast_address.\n",
		    ENV_MCAST_IF);
}
