/*
 * Copyright (C) 1999-2001, 2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * INTERNET SOFTWARE CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: net.c,v 1.3.2.2.4.1 2003/08/22 06:24:24 marka Exp $ */

#include <config.h>

#include <errno.h>
#include <unistd.h>

#include <isc/log.h>
#include <isc/msgs.h>
#include <isc/net.h>
#include <isc/once.h>
#include <isc/string.h>
#include <isc/util.h>

#if defined(ISC_PLATFORM_HAVEIPV6) && defined(ISC_PLATFORM_NEEDIN6ADDRANY)
const struct in6_addr isc_net_in6addrany = IN6ADDR_ANY_INIT;
#endif

static isc_once_t 	once = ISC_ONCE_INIT;
static isc_once_t 	once_ipv6only = ISC_ONCE_INIT;
static isc_result_t	ipv4_result = ISC_R_NOTFOUND;
static isc_result_t	ipv6_result = ISC_R_NOTFOUND;
static isc_result_t	ipv6only_result = ISC_R_NOTFOUND;

static isc_result_t
try_proto(int domain) {
	SOCKET s;
	isc_result_t result = ISC_R_SUCCESS;

	s = socket(domain, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET) {
		switch (WSAGetLastError()) {
		case WSAEAFNOSUPPORT:
		case WSAEPROTONOSUPPORT:
		case WSAEINVAL:
			return (ISC_R_NOTFOUND);
		default:
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "socket() %s: %s",
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strerror(errno));
			return (ISC_R_UNEXPECTED);
		}
	}

#ifdef ISC_PLATFORM_HAVEIPV6
#ifdef WANT_IPV6
#ifdef ISC_PLATFORM_HAVEIN6PKTINFO
	if (domain == PF_INET6) {
		struct sockaddr_in6 sin6;
		unsigned int len;

		/*
		 * Check to see if IPv6 is broken, as is common on Linux.
		 */
		len = sizeof(sin6);
		if (getsockname(s, (struct sockaddr *)&sin6, (void *)&len) < 0)
		{
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				      "retrieving the address of an IPv6 "
				      "socket from the kernel failed.");
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				      "IPv6 support is disabled.");
			result = ISC_R_NOTFOUND;
		} else {
			if (len == sizeof(struct sockaddr_in6))
				result = ISC_R_SUCCESS;
			else {
				isc_log_write(isc_lctx,
					      ISC_LOGCATEGORY_GENERAL,
					      ISC_LOGMODULE_SOCKET,
					      ISC_LOG_ERROR,
					      "IPv6 structures in kernel and "
					      "user space do not match.");
				isc_log_write(isc_lctx,
					      ISC_LOGCATEGORY_GENERAL,
					      ISC_LOGMODULE_SOCKET,
					      ISC_LOG_ERROR,
					      "IPv6 support is disabled.");
				result = ISC_R_NOTFOUND;
			}
		}
	}
#endif
#endif
#endif

	closesocket(s);

	return (result);
}

static void
initialize_action(void) {
	ipv4_result = try_proto(PF_INET);
#ifdef ISC_PLATFORM_HAVEIPV6
#ifdef WANT_IPV6
#ifdef ISC_PLATFORM_HAVEIN6PKTINFO
	ipv6_result = try_proto(PF_INET6);
#endif
#endif
#endif
}

static void
initialize(void) {
	RUNTIME_CHECK(isc_once_do(&once, initialize_action) == ISC_R_SUCCESS);
}

isc_result_t
isc_net_probeipv4(void) {
	initialize();
	return (ipv4_result);
}

#ifdef ISC_PLATFORM_HAVEIPV6
#ifdef WANT_IPV6
isc_result_t
isc_net_probeipv6(void) {
	initialize();
	return (ipv6_result);
}

static void
try_ipv6only(void) {
#ifdef IPV6_V6ONLY
	int s, on;
	char strbuf[ISC_STRERRORSIZE];
#endif
	isc_result_t result;

	result = isc_net_probeipv6();
	if (result != ISC_R_SUCCESS) {
		ipv6only_result = result;
		return;
	}

#ifndef IPV6_V6ONLY
	ipv6only_result = ISC_R_NOTFOUND;
	return;
#else
	/* check for TCP sockets */
	s = socket(PF_INET6, SOCK_STREAM, 0);
	if (s == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "socket() %s: %s",
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED,
						"failed"),
				 strbuf);
		ipv6only_result = ISC_R_UNEXPECTED;
		return;
	}

	on = 1;
	if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0) {
		ipv6only_result = ISC_R_NOTFOUND;
		goto close;
	}

	close(s);

	/* check for UDP sockets */
	s = socket(PF_INET6, SOCK_DGRAM, 0);
	if (s == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "socket() %s: %s",
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED,
						"failed"),
				 strbuf);
		ipv6only_result = ISC_R_UNEXPECTED;
		return;
	}

	on = 1;
	if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0) {
		ipv6only_result = ISC_R_NOTFOUND;
		goto close;
	}

	close(s);

	ipv6only_result = ISC_R_SUCCESS;

close:
	close(s);
	return;
#endif
}

static void
initialize_ipv6only(void) {
	RUNTIME_CHECK(isc_once_do(&once_ipv6only,
				  try_ipv6only) == ISC_R_SUCCESS);
}
#endif
#endif

isc_result_t
isc_net_probe_ipv6only(void) {
#ifdef ISC_PLATFORM_HAVEIPV6
#ifdef WANT_IPV6
	initialize_ipv6only();
#else
	ipv6only_result = ISC_R_NOTFOUND;
#endif
#endif
	return (ipv6only_result);
}
