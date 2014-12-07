#include "private-libwebsockets.h"

static int
poll_service(struct libwebsocket_context *context, int timeout_ms)
{
	int n;
	int m;
	char buf;

	/* stay dead once we are dead */

	if (!context)
		return 1;

	context->service_tid = context->protocols[0].callback(context, NULL,
				     LWS_CALLBACK_GET_THREAD_ID, NULL, NULL, 0);

#ifdef LWS_OPENSSL_SUPPORT
	/* if we know we have non-network pending data, do not wait in poll */
	if (context->ssl_flag_buffered_reads)
		timeout_ms = 0;
#endif
	n = poll(context->fds, context->fds_count, timeout_ms);
	context->service_tid = 0;

#ifdef LWS_OPENSSL_SUPPORT
	if (!context->ssl_flag_buffered_reads && n == 0) {
#else
	if (n == 0) /* poll timeout */ {
#endif
		libwebsocket_service_fd(context, NULL);
		return 0;
	}

#ifdef LWS_OPENSSL_SUPPORT
	/* any more will have to set it fresh this time around */
	context->ssl_flag_buffered_reads = 0;
#endif

	if (n < 0) {
		if (LWS_ERRNO != LWS_EINTR)
			return -1;
		return 0;
	}

	/* any socket with events to service? */

	for (n = 0; n < context->fds_count; n++) {
#ifdef LWS_OPENSSL_SUPPORT
		struct libwebsocket *wsi;

		wsi = context->lws_lookup[context->fds[n].fd];
		if (wsi == NULL)
			continue;
		/*
		 * if he's not flowcontrolled, make sure we service ssl
		 * pending read data
		 */
		if (wsi->ssl && wsi->buffered_reads_pending) {
			lwsl_debug("wsi %p: forcing POLLIN\n", wsi);
			context->fds[n].revents |= context->fds[n].events & POLLIN;
			if (context->fds[n].revents & POLLIN)
				wsi->buffered_reads_pending = 0;
			else
				/* somebody left with pending SSL read data */
				context->ssl_flag_buffered_reads = 1;
		}
#endif
		if (!context->fds[n].revents)
			continue;

		if (context->fds[n].fd == context->dummy_pipe_fds[0]) {
			if (read(context->fds[n].fd, &buf, 1) != 1)
				lwsl_err("Cannot read from dummy pipe.");
			continue;
		}

		m = libwebsocket_service_fd(context, &context->fds[n]);
		if (m < 0)
			return -1;
		/* if something closed, retry this slot */
		if (m)
			n--;
	}

	return 0;
}


static void
poll_register(struct libwebsocket_context *context,
	      struct libwebsocket *wsi)
{
	context->fds[context->fds_count++].revents = 0;
}

struct lws_event_ops lws_poll_event_ops = {
	.service = poll_service,
	.socket_register = poll_register,
};
