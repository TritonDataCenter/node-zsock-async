#
# Copyright (c) 2012 Joyent, Inc.  All rights reserved.
#

PREFIX_NODE :=	$(shell dirname $$(bash -c 'hash node; hash -t node'))/..
V8PLUS :=       $(shell $(PREFIX_NODE)/bin/node -e 'require("v8plus");')

include $(V8PLUS)/Makefile.v8plus.defs

CTFCONVERT	= true
CTFMERGE	= true
MODULE_DIR	= ../../lib

MODULE =	zsock_async_binding

SRCS =	\
		zsock_async.c

ERRNO_JSON =	errno.json

LIBS +=		-lxnet -lumem

#
# For the sendmsg/recvmsg and related macros, we need POSIX_C_SOURCE=200112L.
# To use strlcpy with that definition, we also need __EXTENSIONS__.
#
$(SRCS:%.c=%.o): CPPFLAGS +=    -D_POSIX_C_SOURCE=200112L 
$(SRCS:%.c=%.o): CPPFLAGS +=	-D__EXTENSIONS__

include $(V8PLUS)/Makefile.v8plus.targ
