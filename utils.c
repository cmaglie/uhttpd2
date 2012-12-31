/*
 * uhttpd - Tiny single-threaded httpd
 *
 *   Copyright (C) 2010-2012 Jo-Philipp Wich <xm@subsignal.org>
 *   Copyright (C) 2012 Felix Fietkau <nbd@openwrt.org>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <ctype.h>
#include "uhttpd.h"

bool uh_use_chunked(struct client *cl)
{
	if (cl->request.version != UH_HTTP_VER_1_1)
		return false;

	if (cl->request.method == UH_HTTP_MSG_HEAD)
		return false;

	return true;
}

void uh_chunk_write(struct client *cl, const void *data, int len)
{
	bool chunked = uh_use_chunked(cl);

	uloop_timeout_set(&cl->timeout, conf.network_timeout * 1000);
	if (chunked)
		ustream_printf(cl->us, "%X\r\n", len);
	ustream_write(cl->us, data, len, true);
	if (chunked)
		ustream_printf(cl->us, "\r\n", len);
}

void uh_chunk_vprintf(struct client *cl, const char *format, va_list arg)
{
	char buf[256];
	va_list arg2;
	int len;

	uloop_timeout_set(&cl->timeout, conf.network_timeout * 1000);
	if (!uh_use_chunked(cl)) {
		ustream_vprintf(cl->us, format, arg);
		return;
	}

	va_copy(arg2, arg);
	len = vsnprintf(buf, sizeof(buf), format, arg2);
	va_end(arg2);

	ustream_printf(cl->us, "%X\r\n", len);
	if (len < sizeof(buf))
		ustream_write(cl->us, buf, len, true);
	else
		ustream_vprintf(cl->us, format, arg);
	ustream_printf(cl->us, "\r\n", len);
}

void uh_chunk_printf(struct client *cl, const char *format, ...)
{
	va_list arg;

	va_start(arg, format);
	uh_chunk_vprintf(cl, format, arg);
	va_end(arg);
}

void uh_chunk_eof(struct client *cl)
{
	if (!uh_use_chunked(cl))
		return;

	ustream_printf(cl->us, "0\r\n\r\n");
}

/* blen is the size of buf; slen is the length of src.  The input-string need
** not be, and the output string will not be, null-terminated.  Returns the
** length of the decoded string, -1 on buffer overflow, -2 on malformed string. */
int uh_urldecode(char *buf, int blen, const char *src, int slen)
{
	int i;
	int len = 0;

#define hex(x) \
	(((x) <= '9') ? ((x) - '0') : \
		(((x) <= 'F') ? ((x) - 'A' + 10) : \
			((x) - 'a' + 10)))

	for (i = 0; (i < slen) && (len < blen); i++)
	{
		if (src[i] != '%') {
			buf[len++] = src[i];
			continue;
		}

		if (i + 2 >= slen || !isxdigit(src[i + 1]) || !isxdigit(src[i + 2]))
			return -2;

		buf[len++] = (char)(16 * hex(src[i+1]) + hex(src[i+2]));
		i += 2;
	}
	buf[len] = 0;

	return (i == slen) ? len : -1;
}

/* blen is the size of buf; slen is the length of src.  The input-string need
** not be, and the output string will not be, null-terminated.  Returns the
** length of the encoded string, or -1 on error (buffer overflow) */
int uh_urlencode(char *buf, int blen, const char *src, int slen)
{
	int i;
	int len = 0;
	const char hex[] = "0123456789abcdef";

	for (i = 0; (i < slen) && (len < blen); i++)
	{
		if( isalnum(src[i]) || (src[i] == '-') || (src[i] == '_') ||
		    (src[i] == '.') || (src[i] == '~') )
		{
			buf[len++] = src[i];
		}
		else if ((len+3) <= blen)
		{
			buf[len++] = '%';
			buf[len++] = hex[(src[i] >> 4) & 15];
			buf[len++] = hex[ src[i]       & 15];
		}
		else
		{
			len = -1;
			break;
		}
	}

	return (i == slen) ? len : -1;
}

int uh_b64decode(char *buf, int blen, const unsigned char *src, int slen)
{
	int i = 0;
	int len = 0;

	unsigned int cin  = 0;
	unsigned int cout = 0;


	for (i = 0; (i <= slen) && (src[i] != 0); i++)
	{
		cin = src[i];

		if ((cin >= '0') && (cin <= '9'))
			cin = cin - '0' + 52;
		else if ((cin >= 'A') && (cin <= 'Z'))
			cin = cin - 'A';
		else if ((cin >= 'a') && (cin <= 'z'))
			cin = cin - 'a' + 26;
		else if (cin == '+')
			cin = 62;
		else if (cin == '/')
			cin = 63;
		else if (cin == '=')
			cin = 0;
		else
			continue;

		cout = (cout << 6) | cin;

		if ((i % 4) == 3)
		{
			if ((len + 3) < blen)
			{
				buf[len++] = (char)(cout >> 16);
				buf[len++] = (char)(cout >> 8);
				buf[len++] = (char)(cout);
			}
			else
			{
				break;
			}
		}
	}

	buf[len++] = 0;
	return len;
}