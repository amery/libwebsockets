#include "private-libwebsockets.h"

int
poll_service(struct libwebsocket_context *context, int timeout_ms)
{
	int n;
	int i;
	DWORD ev;
	WSANETWORKEVENTS networkevents;
	struct libwebsocket_pollfd *pfd;

	/* stay dead once we are dead */

	if (context == NULL)
		return 1;

	context->service_tid = context->protocols[0].callback(context, NULL,
				     LWS_CALLBACK_GET_THREAD_ID, NULL, NULL, 0);

	for (i = 0; i < context->fds_count; ++i) {
		pfd = &context->fds[i];
		if (pfd->fd == context->listen_service_fd)
			continue;

		if (pfd->events & LWS_POLLOUT) {
			if (context->lws_lookup[pfd->fd]->sock_send_blocking)
				continue;
			pfd->revents = LWS_POLLOUT;
			n = libwebsocket_service_fd(context, pfd);
			if (n < 0)
				return n;
		}
	}

	ev = WSAWaitForMultipleEvents(context->fds_count + 1,
				     context->e.poll.events, FALSE, timeout_ms, FALSE);
	context->service_tid = 0;

	if (ev == WSA_WAIT_TIMEOUT) {
		libwebsocket_service_fd(context, NULL);
		return 0;
	}

	if (ev == WSA_WAIT_EVENT_0) {
		WSAResetEvent(context->e.poll.events[0]);
		return 0;
	}

	if (ev < WSA_WAIT_EVENT_0 || ev > WSA_WAIT_EVENT_0 + context->fds_count)
		return -1;

	pfd = &context->fds[ev - WSA_WAIT_EVENT_0 - 1];

	if (WSAEnumNetworkEvents(pfd->fd,
			context->e.poll.events[ev - WSA_WAIT_EVENT_0],
					      &networkevents) == SOCKET_ERROR) {
		lwsl_err("WSAEnumNetworkEvents() failed with error %d\n",
								     LWS_ERRNO);
		return -1;
	}

	pfd->revents = networkevents.lNetworkEvents;

	if (pfd->revents & LWS_POLLOUT)
		context->lws_lookup[pfd->fd]->sock_send_blocking = FALSE;

	return libwebsocket_service_fd(context, pfd);
}

static void
poll_register(struct libwebsocket_context *context,
	      struct libwebsocket *wsi)
{
	context->fds[context->fds_count++].revents = 0;
	context->e.poll.events[context->fds_count] = WSACreateEvent();
	WSAEventSelect(wsi->sock, context->e.poll.events[context->fds_count], LWS_POLLIN);
}

static void
poll_unregister(struct libwebsocket_context *context,
		struct libwebsocket *wsi,
		int m)
{
	WSACloseEvent(context->e.poll.events[m + 1]);
	context->e.poll.events[m + 1] = context->e.poll.events[context->fds_count + 1];
}

static int
poll_change(struct libwebsocket_context *context,
	    struct libwebsocket *wsi,
	    struct libwebsocket_pollfd *pfd)
{
	long networkevents = LWS_POLLOUT | LWS_POLLHUP;

	if ((pfd->events & LWS_POLLIN))
		networkevents |= LWS_POLLIN;

	if (WSAEventSelect(wsi->sock,
			   context->e.poll.events[wsi->position_in_fds_table + 1],
			   networkevents) != SOCKET_ERROR)
		return 0;

	lwsl_err("WSAEventSelect() failed with error %d\n", LWS_ERRNO);

	return 1;
}

struct lws_event_ops lws_poll_event_ops = {
	.service = poll_service,
	.socket_register = poll_register,
	.socket_unregister = poll_unregister,
	.socket_change = poll_change,
};
