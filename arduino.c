/*
 * uhttpd - Tiny single-threaded httpd
 *          Arduino Yun (Bridge) dispatcher
 *
 *   Copyright (C) 2013 Cristian Maglie <c.maglie@bug.st>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <libubox/blobmsg.h>
#include <netdb.h>
#include <sys/socket.h>
#include <errno.h>

#include "uhttpd.h"

static char *url_prefix = NULL;
static char *bridge_ip;
static int bridge_port;
static int bridge_timeout = 10;

void uh_arduino_set_options(char *_url_prefix, char *_bridge_ip, int _bridge_port) {
	url_prefix = _url_prefix;
	bridge_ip = _bridge_ip;
	bridge_port = _bridge_port;
}

void uh_arduino_set_timeout(int timeout) {
	bridge_timeout = timeout;
}

static int send_with_timeout(int sockfd, void *data, int len) {
	while (len>0) {
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(sockfd, &wfds);

		struct timeval tv;
		tv.tv_sec = bridge_timeout;
		tv.tv_usec = 0;

		if (select(sockfd+1, NULL, &wfds, NULL, &tv) <= 0)
			return -1;
		ssize_t l = send(sockfd, data, len, 0);
		if (l<0)
			return l;
		len -= l;
		data += l;
	}
	return 0;
}

static ssize_t recv_with_timeout(int sockfd, void *data, int len) {
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(sockfd, &rfds);

	struct timeval tv;
	tv.tv_sec = bridge_timeout;
	tv.tv_usec = 0;

	if (select(sockfd+1, &rfds, NULL, NULL, &tv) <= 0)
		return -1;
	return recv(sockfd, data, len, 0);
}

static void arduino_main(struct client *cl, struct path_info *pi, char *url)
{
	/* Retrieve bridge address */
	struct hostent *server = gethostbyname(bridge_ip);
	if (server == NULL)
		goto error;
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(bridge_port);
	memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
	
	/* Open socket and try to connect */
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		goto error;
	if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
		goto error_connect;

	/* Send requested URL (without the prefix) */
	url += strlen(url_prefix);
	if (send_with_timeout(sockfd, url, strlen(url)) < 0)
		goto error_chatting;
	if (send_with_timeout(sockfd, "\r\n", 2) < 0)
		goto error_chatting;

	/* Wait response */
	bool header_sent = false;
	int readed = 0;
	uint8_t buff[1024];
	for (;;) {
		ssize_t l = recv_with_timeout(sockfd, buff+readed, 1024-readed);
		if (l <= 0)
			break;
		/* If header was sent, just proxy received data... */
		if (header_sent) {
			fwrite(buff, 1, l, stdout);
			continue;
		}

		/* ...else accumulate bytes */
		readed += l;
		if (readed <= 6) 
			continue;

		/* When we have enough bytes check for an header */
		if (memcmp("Status", buff, 6)) {
			/* No header found, forge one, and send it before data */
			printf("Status: 200\r\n\r\n");
		} 
		fwrite(buff, 1, readed, stdout);
		readed = 0;
		header_sent = true;
	}
	if (!header_sent) {
		/* We fall here if the response is shortes than 6 bytes */
		printf("Status: 200\r\n\r\n");
		fwrite(buff, 1, readed, stdout);
	}
	fflush(stdout);
	close(sockfd);
	return;

error_chatting:
error_connect:
	close(sockfd);
error:
	printf("Status: 500\r\n");
	printf("\r\n");
	printf("Couldn't connect to bridge:\r\n");
	printf("%s\r\n", strerror(errno));
	fflush(stdout);
}

enum arduino_hdr {
	HDR_AUTHORIZATION,
	__HDR_MAX
};

static void arduino_handle_request(struct client *cl, char *url, struct path_info *pi)
{
	static const struct blobmsg_policy hdr_policy[__HDR_MAX] = {
		[HDR_AUTHORIZATION] = { "authorization", BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[__HDR_MAX];
	blobmsg_parse(hdr_policy, __HDR_MAX, tb, blob_data(cl->hdr.head), blob_len(cl->hdr.head));
	struct path_info p;
	p.auth = NULL;
	p.name = url;
	if (tb[HDR_AUTHORIZATION])
		p.auth = blobmsg_data(tb[HDR_AUTHORIZATION]);

	if (!uh_auth_check(cl, &p))
		/* Authorization required! */
		return;

	if (uh_create_process(cl, pi, url, arduino_main)) 
		return;

	uh_client_error(cl, 500, "Internal Server Error",
			"Failed to process request: %s", strerror(errno));
}

static bool check_arduino_url(const char *url)
{
	if (url_prefix == NULL)
		return false;
	return uh_path_match(url_prefix, url);
}

struct dispatch_handler arduino_dispatch = {
	.script = true,
	.check_url = check_arduino_url,
	.handle_request = arduino_handle_request,
};

