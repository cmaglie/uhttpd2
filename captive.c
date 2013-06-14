
#include <libubox/blobmsg.h>
#include "uhttpd.h"

static char *captive_host = NULL;
static char *captive_url = NULL;

void uh_captive_set_host(const char *host, const char *url) {
	captive_host = strdup(host);
	captive_url = strdup(url);
}

bool uh_captive_check_host(const char *host) {
	/* Captive host support configured? */
	if (captive_host==NULL || captive_url==NULL)
		return false;

	/* Is request sent to the captive host? */
	if (host!=NULL && !strcmp(host, captive_host))
		/* yes: proceed normally */
		return false;

	/* no: must redirect to the correct URL */
	return true;
}

bool uh_captive_redirect(struct client *cl) {
	uh_http_header(cl, 302, "Found");
	ustream_printf(cl->us, "Content-Length: 0\r\n");
	ustream_printf(cl->us, "Location: %s\r\n\r\n", captive_url);
	uh_request_done(cl);
	return true;
}

