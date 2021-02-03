// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "message.h"


static ev_uint32_t msg_length(struct evbuffer *buf)
{
	size_t buflen = evbuffer_get_length(buf);
	ev_uint32_t msglen;

	if (buflen < 4)
		return 0;

	evbuffer_copyout(buf, &msglen, 4);
	msglen = ntohl(msglen);
	if (buflen < msglen + 4)
		return 0;

	return msglen;
}

/**
 * rteipc_msg_drain - remove a message from an evbuffer and copy it to @msg_out
 * @buf: evbuffer from which data removed
 * @size_out: message data length
 * @msg_out: buffer allocated and filled with data, must be free()ed by caller
 */
int rteipc_msg_drain(struct evbuffer *buf, size_t *size_out, char **msg_out)
{
	ev_uint32_t len = msg_length(buf);
	char *msg;

	if (!len)
		return 0;

	msg = malloc(len);
	if (!msg)
		return -1;

	evbuffer_drain(buf, 4);
	evbuffer_remove(buf, msg, len);
	*msg_out = msg;
	*size_out = len;

	return 1;
}

int rteipc_msg_write(evutil_socket_t fd, const void *data, size_t len)
{
	size_t offset = 0;
	const ev_uint8_t *pos;
	ssize_t written;

	pos = (const ev_uint8_t *)data;
	while (offset < len) {
		do {
			written = write(fd, pos + offset, len - offset);
		} while (written < 0 && ((errno == EINTR || errno == EAGAIN)));

		if (written >= 0) {
			offset += written;
		} else {
			fprintf(stderr, "Failed to write data(%d)\n", errno);
			close(fd);
			return -1;
		}
	}
	return 0;
}

int rteipc_evbuffer(struct bufferevent *bev, struct evbuffer *buf)
{
	size_t len = evbuffer_get_length(buf);
	size_t nl = htonl(len);

	evbuffer_prepend(buf, &nl, 4);
	return bufferevent_write_buffer(bev, buf);
}

int rteipc_buffer(struct bufferevent *bev, const void *data, size_t len)
{
	struct evbuffer *buf = evbuffer_new();
	int ret;

	evbuffer_add(buf, data, len);
	ret = rteipc_evbuffer(bev, buf);
	evbuffer_free(buf);
	return ret;
}
