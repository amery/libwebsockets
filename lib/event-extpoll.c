#include "private-libwebsockets.h"

/* communicate via protocol[0].callback()  */

static inline void
extpoll_callback(struct libwebsocket_context *context,
		 struct libwebsocket *wsi,
		 int code, int events)
{
	struct libwebsocket_pollargs pa = { wsi->sock, events, 0 };

	context->protocols[0].callback(context, wsi, code,
				       wsi->user_space,
				       (void *)&pa, 0);
}

#define callback(...)	extpoll_callback(context, wsi, __VA_ARGS__)

static void
extpoll_lock(struct libwebsocket_context *context,
	     struct libwebsocket *wsi)
{
	callback(LWS_CALLBACK_LOCK_POLL, 0);
}

static void
extpoll_unlock(struct libwebsocket_context *context,
	       struct libwebsocket *wsi)
{
	callback(LWS_CALLBACK_UNLOCK_POLL, 0);
}

static void
extpoll_register(struct libwebsocket_context *context,
		 struct libwebsocket *wsi)
{
	callback(LWS_CALLBACK_ADD_POLL_FD, LWS_POLLIN);
}

static void
extpoll_unregister(struct libwebsocket_context *context,
		   struct libwebsocket *wsi,
		   int m)
{
	if (wsi->sock)
		callback(LWS_CALLBACK_DEL_POLL_FD, 0);
}

static int
extpoll_change(struct libwebsocket_context *context,
	       struct libwebsocket *wsi,
	       struct libwebsocket_pollfd *pfd)
{
	callback(LWS_CALLBACK_CHANGE_MODE_POLL_FD, pfd->events);
	return 0;
}

extern int poll_service(struct libwebsocket_context *context, int timeout_ms);

struct lws_event_ops lws_extpoll_event_ops = {
	.lock = extpoll_lock,
	.unlock = extpoll_unlock,

	.socket_register = extpoll_register,
	.socket_unregister = extpoll_unregister,
	.socket_change = extpoll_change,

	.service = poll_service,
};
