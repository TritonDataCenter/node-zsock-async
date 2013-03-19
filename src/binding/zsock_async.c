/*
 * zsock_async.c: v8plus provider to support zsock-async.
 *
 * This provider is used by lib/zsock_async.js.  See that file for a detailed
 * description of what this module does.
 *
 * This binding implements a "channel" object, constructed with a filesystem
 * path.  The constructor binds a new Unix Domain Socket to the given path, and
 * provides two methods: recvfd(), which reads a file descriptor sent over the
 * socket, and close(), which closes the underlying file descriptor.
 */

#include <alloca.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libnvpair.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ccompile.h>
#include <sys/debug.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>

#include "v8plus_glue.h"

static int uds_bind(const char *);
static int uds_recvfd(int);

typedef struct {
	int	zc_fd;
} za_channel_t;

static nvlist_t *
za_ctor(const nvlist_t *ap, void **cpp)
{
	char *path;
	za_channel_t *zp;

	if (v8plus_args(ap, V8PLUS_ARG_F_NOEXTRA,
	    V8PLUS_TYPE_STRING, &path, V8PLUS_TYPE_NONE) != 0)
		return (NULL);

	if ((zp = malloc(sizeof (*zp))) == NULL)
		return (v8plus_syserr(errno, NULL));

	zp->zc_fd = uds_bind(path);
	if (zp->zc_fd < 0) {
		free(zp);
		return (v8plus_syserr(errno, "failed to bind socket"));
	}

	*cpp = zp;
	return (v8plus_void());
}

static void
za_dtor(void *op)
{
	za_channel_t *zp = op;

	if (zp == NULL)
		return;

	if (zp->zc_fd != -1) {
		(void) close(zp->zc_fd);
		zp->zc_fd = -1;
	}

	free(zp);
}

static nvlist_t *
za_close(void *op, const nvlist_t *ap)
{
	za_channel_t *zp = op;

	if (v8plus_args(ap, V8PLUS_ARG_F_NOEXTRA, V8PLUS_TYPE_NONE) != 0)
		return (NULL);

	if (zp->zc_fd == -1)
		return (v8plus_error(V8PLUSERR_YOUSUCK,
		    "zsock recvfd binding has already been closed"));

	(void) close(zp->zc_fd);
	zp->zc_fd = -1;
	return (v8plus_void());
}

static nvlist_t *
za_recvfd(void *op, const nvlist_t *ap)
{
	za_channel_t *zp = op;
	int rv;

	if (v8plus_args(ap, V8PLUS_ARG_F_NOEXTRA, V8PLUS_TYPE_NONE) != 0)
		return (NULL);

	if (zp->zc_fd == -1)
		return (v8plus_error(V8PLUSERR_YOUSUCK,
		    "zsock recvfd binding has already been closed"));

	rv = uds_recvfd(zp->zc_fd);
	if (rv == -1)
		return (v8plus_syserr(errno, "failed to receive fd"));

	return (v8plus_obj(V8PLUS_TYPE_NUMBER, "res", (double)rv,
	    V8PLUS_TYPE_NONE));
}

static int
uds_bind(const char *path)
{
	struct sockaddr_un addr;
	int sockfd, len, errno_save, flags;

	if ((sockfd = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0)
		return (-1);

	bzero(&addr, sizeof (struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	(void) strlcpy(addr.sun_path, path, sizeof (addr.sun_path));
	len = sizeof (addr.sun_family) + strlen(addr.sun_path);
	if (bind(sockfd, (struct sockaddr *)&addr, len) != 0) {
		errno_save = errno;
		(void) close(sockfd);
		errno = errno_save;
		return (-1);
	}

	/*
	 * Unlike conventional Node.js add-ons, this one doesn't support polling
	 * on the file descriptor and emitting events when data becomes
	 * available.  Instead, we rely on the consumer to know (via out-of-band
	 * means) when data's available.  As a result, O_NONBLOCK should not be
	 * necessary at all, but we do it for safety: if the caller gets it
	 * wrong, they'll get back EAGAIN (or the like) rather than blocking the
	 * main thread.
	 */
	(void) fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if ((flags = fcntl(sockfd, F_GETFD)) != -1) {
		flags |= FD_CLOEXEC;
		(void) fcntl(sockfd, F_SETFD, flags);
	}

	return (sockfd);
}

static int
uds_recvfd(int fd)
{
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof (int))];
	} ctl;
	struct cmsghdr *cmptr;
	char c;
	int rv, flags;

	assert(fd >= 0);

	iov.iov_base = &c;
	iov.iov_len = sizeof (c);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = ctl.control;
	msg.msg_controllen = sizeof (ctl.control);

	rv = recvmsg(fd, &msg, 0);
	if (rv == -1)
		return (-1);

	if (rv == 0 ||
	    (cmptr = CMSG_FIRSTHDR(&msg)) == NULL ||
	    cmptr->cmsg_len != CMSG_LEN(sizeof (int)) ||
	    cmptr->cmsg_level != SOL_SOCKET ||
	    cmptr->cmsg_type != SCM_RIGHTS) {
		errno = EINVAL;
		return (-1);
	}

	rv = *((int *)(CMSG_DATA(cmptr)));

	if ((flags = fcntl(rv, F_GETFD)) != -1) {
		flags |= FD_CLOEXEC;
		(void) fcntl(rv, F_SETFD, flags);
	}

	return (rv);
}

/*
 * v8+ boilerplate
 */
const v8plus_c_ctor_f v8plus_ctor = za_ctor;
const v8plus_c_dtor_f v8plus_dtor = za_dtor;
const char *v8plus_js_factory_name = "_new";
const char *v8plus_js_class_name = "ZsockAsyncBinding";
const v8plus_method_descr_t v8plus_methods[] = {
	{
		md_name: "_close",
		md_c_func: za_close,
	},
	{
		md_name: "_recvfd",
		md_c_func: za_recvfd
	}
};
const uint_t v8plus_method_count =
    sizeof (v8plus_methods) / sizeof (v8plus_methods[0]);

const v8plus_static_descr_t v8plus_static_methods[] = {};
const uint_t v8plus_static_method_count =
    sizeof (v8plus_static_methods) / sizeof (v8plus_static_methods[0]);
