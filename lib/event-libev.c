/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010-2014 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include "private-libwebsockets.h"

/*
 * error: dereferencing type-punned pointer will break strict-aliasing rules
 */
#ifdef ev_io_init
#undef ev_io_init
static inline void ev_io_init(struct ev_io *w,
			      void (*cb)(struct ev_loop *, struct ev_io *, int),
			      int fd, int events)
{
	ev_init(w, cb);
	ev_io_set(w, fd, events);
}
#endif

#ifdef ev_signal_init
#undef ev_signal_init
static inline void ev_signal_init(struct ev_signal *w,
			      void (*cb)(struct ev_loop *, struct ev_signal *, int),
			      int signum)
{
	ev_init(w, cb);
	ev_signal_set(w, signum);
}
#endif

#ifdef ev_timer_init
#undef ev_timer_init
static inline void ev_timer_init(struct ev_timer *w,
				 void (*cb)(struct ev_loop *, struct ev_timer *, int),
				 ev_tstamp after, ev_tstamp repeat)
{
	ev_init(w, cb);
	ev_timer_set(w, after, repeat);
}
#endif

/* ev_timer_set() also triggers dereferencing warnings, but a reset is more useful */
static inline void ev_timer_reset(struct ev_loop *loop,
				  struct ev_timer *w,
				  ev_tstamp after, ev_tstamp repeat)
{
	ev_timer_stop(loop, w);
	w->at = after;
	w->repeat = repeat;
	ev_timer_start(loop, w);
}

/*
 * libev callbacks
 */
static inline void
libev_wsi_cb(struct ev_loop *loop, struct libwebsocket *wsi, int fd, int revents)
{
	struct libwebsocket_pollfd eventfd;
	struct libwebsocket_context *context = wsi->e.ev.context;

	if (revents & EV_ERROR)
		return;

	eventfd.fd = fd;
	eventfd.revents = EV_NONE;
	if (revents & EV_READ)
		eventfd.revents |= LWS_POLLIN;
	if (revents & EV_WRITE)
		eventfd.revents |= LWS_POLLOUT;

	libwebsocket_service_fd(context, &eventfd);
}

static void
libev_read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	struct _lws_libev_event_related *evi = container_of(watcher, struct _lws_libev_event_related, w_read);
	struct libwebsocket *wsi = container_of(evi, struct libwebsocket, e.ev);

	libev_wsi_cb(loop, wsi, watcher->fd, revents);
}

static void
libev_write_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	struct _lws_libev_event_related *evi = container_of(watcher, struct _lws_libev_event_related, w_write);
	struct libwebsocket *wsi = container_of(evi, struct libwebsocket, e.ev);

	libev_wsi_cb(loop, wsi, watcher->fd, revents);
}

/* just a dummy placeholder to "break" ev_run(..., EVRUN_ONCE) */
static void
libev_timeout_cb(struct ev_loop *loop, struct ev_timer *w, int revents)
{
	lwsl_debug("%s(%p, %p, %u): libev timeout!\n",
		   __func__, loop, w, revents);
}

static int
libev_init(struct lws_context_creation_info *info,
	   struct libwebsocket_context *context)
{
	struct _lws_libev_event_info *evi = &info->e.ev;
	struct _lws_libev_event_context *evc = &context->e.ev;
	int backend;
	const char * backend_name;

	if (!evi->loop)
		evi->loop = ev_default_loop(0);

	evc->loop = evi->loop;
	backend = ev_backend(evc->loop);

	switch (backend) {
	case EVBACKEND_SELECT:
		backend_name = "select";
		break;
	case EVBACKEND_POLL:
		backend_name = "poll";
		break;
	case EVBACKEND_EPOLL:
		backend_name = "epoll";
		break;
	case EVBACKEND_KQUEUE:
		backend_name = "kqueue";
		break;
	case EVBACKEND_DEVPOLL:
		backend_name = "/dev/poll";
		break;
	case EVBACKEND_PORT:
		backend_name = "Solaris 10 \"port\"";
		break;
	default:
		backend_name = "Unknown libev backend";
		break;
	};

	lwsl_notice(" libev backend: %s\n", backend_name);

	/* update the internal timestamp so ev_now() works since before the first ev_run */
	ev_now_update(evi->loop);

	/* timeout watcher */
	ev_timer_init(&evc->w_timeout, libev_timeout_cb, 0., 0.);

	return 1;
}

static int
libev_service(struct libwebsocket_context *context, int timeout_ms)
{
	int flags = EVRUN_ONCE;
	struct _lws_libev_event_context *evc = &context->e.ev;

	/* timeout */
	if (timeout_ms > 0) {
		ev_timer_reset(evc->loop, &evc->w_timeout,
			       ((double)timeout_ms) / 1000.0,
			       0.);
	} else {
		flags |= EVRUN_NOWAIT;
	}

	/* run once */
	ev_run(evc->loop, flags);

	return 0;
}

static void
libev_register(struct libwebsocket_context *context,
	       struct libwebsocket *wsi)
{
	struct _lws_libev_event_context *evc = &context->e.ev;
	struct _lws_libev_event_related *evs = &wsi->e.ev;
	int fd = wsi->sock;

	evs->context = context;
	ev_io_init(&evs->w_read, libev_read_cb, fd, EV_READ);
	ev_io_init(&evs->w_write, libev_write_cb, fd, EV_WRITE);

	ev_io_start(evc->loop, &evs->w_read);

	context->fds[context->fds_count++].revents = 0;
}

static void
libev_unregister(struct libwebsocket_context *context,
		 struct libwebsocket *wsi,
		 int m)
{
	struct _lws_libev_event_context *evc = &context->e.ev;
	struct _lws_libev_event_related *evs = &wsi->e.ev;

	ev_io_stop(evc->loop, &evs->w_read);
	ev_io_stop(evc->loop, &evs->w_write);
}

static int
libev_change(struct libwebsocket_context *context,
	     struct libwebsocket *wsi,
	     struct libwebsocket_pollfd *pfd)
{
	struct _lws_libev_event_context *evc = &context->e.ev;
	struct _lws_libev_event_related *evs = &wsi->e.ev;
	/* read */
	if (pfd->events & LWS_POLLIN) {
		if (!evs->w_read.active)
			ev_io_start(evc->loop, &evs->w_read);
	} else if (evs->w_read.active) {
		ev_io_stop(evc->loop, &evs->w_read);
	}

	/* write */
	if (pfd->events & LWS_POLLOUT) {
		if (!evs->w_write.active)
			ev_io_start(evc->loop, &evs->w_write);
	} else if (evs->w_write.active) {
		ev_io_stop(evc->loop, &evs->w_write);
	}

	return 0;
}

struct lws_event_ops lws_libev_event_ops = {
	.init = libev_init,

	.service = libev_service,
	.socket_register = libev_register,
	.socket_unregister = libev_unregister,
	.socket_change = libev_change,
};
