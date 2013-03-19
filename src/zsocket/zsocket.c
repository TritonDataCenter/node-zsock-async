/*
 * zsocket zonename relative-zone-path global-zone-path: create a Unix domain
 * socket bound to "relative-zone-path" inside zone "zonename", then connect to
 * the Unix domain socket at "global-zone-path" and send the bound socket.
 *
 * This is used to reliably, securely create a UDS bound inside a zone.
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libcontract.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ctfs.h>
#include <sys/contract/process.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <zone.h>


#define	EXIT_USAGE	2

static const char *zs_arg0;

static void zs_usage(const char *);
static int zs_zsocket(zoneid_t, const char *);
static int zs_readfd(int);
static int zs_writefd(int, int);
static int zs_uds_addr(struct sockaddr_un *, const char *);
static int zs_uds_bind(const char *);
static int zs_uds_connect(const char *);
static int zs_contract_init(void);
static int zs_contract_abandon_latest(void);
static void zs_contract_fini(int);

int
main(int argc, char *argv[])
{
	zoneid_t zoneid;
	int sendto, zsock;

	zs_arg0 = basename(argv[0]);
	if (argc != 4)
		zs_usage("missing arguments");

	zoneid = getzoneidbyname(argv[1]);
	if (zoneid < 0)
		err(EXIT_FAILURE, "failed to find zone \"%s\"", argv[1]);

	sendto = zs_uds_connect(argv[3]);
	if (sendto < 0)
		err(EXIT_FAILURE, "connecting to \"%s\"", argv[2]);

	zsock = zs_zsocket(zoneid, argv[2]);
	if (zsock < 0)
		err(EXIT_FAILURE, "creating zone socket");

	if (zs_writefd(sendto, zsock) != 0)
		err(EXIT_FAILURE, "sending zone socket");

	return (0);
}

static void
zs_usage(const char *msg)
{
	if (msg != NULL)
		warnx("%s", msg);

	(void) fprintf(stderr,
	    "usage: %s zonename zonepath globalpath\n", zs_arg0);
	exit(EXIT_USAGE);
}

static int
zs_uds_addr(struct sockaddr_un *addrp, const char *path)
{
	bzero(addrp, sizeof (struct sockaddr_un));
	addrp->sun_family = AF_UNIX;
	(void) strlcpy(addrp->sun_path, path, sizeof (addrp->sun_path));
	return (sizeof (addrp->sun_family) + strlen(addrp->sun_path));
}

static int
zs_uds_bind(const char *path)
{
	struct sockaddr_un addr;
	int sockfd, len;

	if ((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
		return (-1);

	len = zs_uds_addr(&addr, path);
	if (bind(sockfd, (struct sockaddr *)&addr, len) != 0) {
		(void) close(sockfd);
		return (-1);
	}

	(void) fcntl(sockfd, F_SETFL, O_NONBLOCK);

	return (sockfd);
}

static int
zs_uds_connect(const char *socketpath)
{
	struct sockaddr_un addr;
	int fd, len;

	if ((fd = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0)
		return (-1);

	len = zs_uds_addr(&addr, socketpath);
	if (connect(fd, (struct sockaddr *)&addr, len) < 0) {
		(void) close(fd);
		return (-1);
	}

	return (fd);
}

static int
zs_writefd(int fd, int fdtosend)
{
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof (int))];
	} ctl;
	struct cmsghdr *cmptr;

	assert(fd >= 0);
	assert(fdtosend >= 0);

	iov.iov_base = "";
	iov.iov_len = sizeof ("");
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = ctl.control;
	msg.msg_controllen = sizeof (ctl.control);

	cmptr = CMSG_FIRSTHDR(&msg);
	cmptr->cmsg_len = CMSG_LEN(sizeof (int));
	cmptr->cmsg_level = SOL_SOCKET;
	cmptr->cmsg_type = SCM_RIGHTS;
	*((int *)CMSG_DATA(cmptr)) = fdtosend;

	if (sendmsg(fd, &msg, 0) == -1)
		return (-1);

	return (0);
}

static int
zs_readfd(int fd)
{
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof (int))];
	} ctl;
	struct cmsghdr *cmptr;
	char c;
	int rv;

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

	return (*((int *)(CMSG_DATA(cmptr))));
}

static int
zs_zsocket(zoneid_t zoneid, const char *zonepath)
{
	int parentchild[2];
	int tmplfd, zsockfd, child_status;
	int rv = -1;
	pid_t childpid;

	assert(zoneid >= 0);

	/*
	 * Although we've connected to the UDS in the global zone to which we
	 * will send our bound file descriptor, once we enter the zone, we will
	 * no longer be able to send messages to that socket, since the path
	 * it's connected to doesn't exist inside the local zone.  Instead, we
	 * fork, zone_enter and bind in the child process, send the file
	 * descriptor back to the parent, and the parent (still in the GZ) sends
	 * that to the global zone named UDS.
	 *
	 * We need to fork anyway in case we're in an existing process contract.
	 * Contracts cannot span zones, so zone_enter() will fail if we're
	 * part of a contract on which we're not the sole member.
	 */
	if (socketpair(PF_UNIX, SOCK_DGRAM, 0, parentchild) != 0) {
		perror("socketpair");
		return (-1);
	}

	tmplfd = zs_contract_init();
	if (tmplfd < 0)
		goto done;

	childpid = fork();
	if (childpid == -1) {
		perror("fork");
		goto done;
	}

	if (childpid == 0) {
		zs_contract_fini(tmplfd);

		if (zone_enter(zoneid) != 0) {
			perror("zone_enter");
			exit(1);
		}

		if (unlink(zonepath) != 0 && errno != ENOENT) {
			perror("unlink");
			exit(1);
		}

		zsockfd = zs_uds_bind(zonepath);
		if (zsockfd < 0)
			exit(1);

		exit(zs_writefd(parentchild[1], zsockfd));
	}

	(void) zs_contract_abandon_latest();

	while (wait(&child_status) == -1 && errno == EINTR)
		;

	if (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0)
		rv = zs_readfd(parentchild[0]);

done:
	zs_contract_fini(tmplfd);
	(void) close(parentchild[0]);
	(void) close(parentchild[1]);
	return (rv);
}

static int
zs_contract_init(void)
{
	int tmplfd;

	tmplfd = open64(CTFS_ROOT "/process/template", O_RDWR);
	if (tmplfd < 0) {
		perror("open64 contract");
		return (-1);
	}

	if (ct_tmpl_set_critical(tmplfd, 0) != 0 ||
	    ct_tmpl_set_informative(tmplfd, 0) != 0 ||
	    ct_pr_tmpl_set_fatal(tmplfd, CT_PR_EV_HWERR) != 0 ||
	    ct_pr_tmpl_set_param(tmplfd, CT_PR_PGRPONLY) != 0 ||
	    ct_tmpl_activate(tmplfd)) {
		perror("contract setup");
		(void) close(tmplfd);
		return (-1);
	}

	return (tmplfd);
}

static void
zs_contract_fini(int tmplfd)
{
	if (tmplfd == -1)
		return;

	(void) ct_tmpl_clear(tmplfd);
	(void) close(tmplfd);
}

static int
zs_contract_abandon_latest(void)
{
	int fd, err;
	ctid_t ctid;
	ct_stathdl_t ctst;
	char path[PATH_MAX];

	fd = open64(CTFS_ROOT "/process/latest", O_RDONLY);
	if (fd < 0) {
		perror("open contract latest");
		return (-1);
	}

	if (ct_status_read(fd, CTD_COMMON, &ctst) != 0) {
		perror("ct_status_read");
		(void) close(fd);
		return (-1);
	}

	ctid = ct_status_get_id(ctst);
	ct_status_free(ctst);
	(void) close(fd);

	(void) snprintf(path, sizeof (path), CTFS_ROOT "/all/%ld/ctl", ctid);
	fd = open64(path, O_WRONLY);
	if (fd < 0) {
		perror("open contract ctl");
		return (-1);
	}

	err = ct_ctl_abandon(fd);
	if (err != 0)
		perror("contract abandon");

	(void) close(fd);
	return (err);
}
