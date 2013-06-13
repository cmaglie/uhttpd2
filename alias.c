
#include <libubox/blobmsg.h>
#include "uhttpd.h"

static LIST_HEAD(aliases);

struct url_alias {
	struct list_head list;
	const char *from;
	int from_l;
	const char *to;
	int to_l;
};

void uh_alias_add(const char *from, const char *to) {
	struct url_alias *alias = malloc(sizeof(struct url_alias));
	alias->from = strdup(from);
	alias->from_l = strlen(from);
	alias->to = strdup(to);
	alias->to_l = strlen(to);

	list_add_tail(&alias->list, &aliases);
}

bool uh_alias_transform(const char *url, char *dest, int dest_l) {
	struct url_alias *alias;

	list_for_each_entry(alias, &aliases, list) {
		if (strncmp(url, alias->from, alias->from_l) == 0) {
			snprintf(dest, dest_l, alias->to, url + alias->from_l);
			dest[dest_l-1] = 0;
			return true;
		}
	}

	// The URL doesn't match any alias, copy as is
	strncpy(dest, url, dest_l);
	dest[dest_l-1] = 0;
	return false;
}

