/* Outgoing TCP connections by host name, for URLs that Home Assistant or Music Assistant hand us. */
#ifndef NET_H
#define NET_H

/* Connected socket or -1.  Every address the name resolves to is tried in turn, so an unreachable one (a global IPv6
 * address behind the egress lock, say) does not hide a working one.  Names ending in ".local" are looked up by mDNS: the
 * system resolver does not do that.  timeout_s applies to each connect() and stays set as send and receive timeout. */
int net_connect(const char *host, const char *port, int timeout_s);

/* socket() as the real group where it differs from the effective one (net.c); for sockets others open for us (libcurl) */
int net_socket(int family, int type, int protocol);

/* IPv4 address of a ".local" name by one-shot mDNS query (RFC 6762 5.1), in network byte order; 0 if nobody answered. */
unsigned mdns_resolve4(const char *name);

/* 1 while an interface other than loopback has an IPv4 address, or an IPv6 one beyond the link, and a carrier (the address alone says nothing: dhcpcd
 * keeps it with the network gone); getifaddrs failing counts as online.  HASSMIC_FAKE_ONLINE (tests): a file, online
 * while it exists.  improv.c's look at the network, and plugin_fetch.c's before it asks the plugin index */
int net_online(void);

/* The IPv6 address of a ".local" name by a one-shot mDNS query (RFC 6762 5.1): a ULA if it has one, else global, else
 * link-local with its interface; 0, or -1 if nobody answered. */
struct sockaddr_in6;
int mdns_resolve6(const char *name, struct sockaddr_in6 *out);
/* The IPv4 address of one (an A question, RFC 6762 5.1), in network byte order; 0 if nobody answered.  Callers ask
 * mdns_resolve6 first: IPv6 is preferred */
unsigned mdns_resolve4(const char *name);
#endif
