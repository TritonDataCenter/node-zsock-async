#
# Copyright (c) 2013, Joyent, Inc. All rights reserved.
#
# Makefile: basic Makefile for template API service
#
# This Makefile is a template for new repos. It contains only repo-specific
# logic and uses included makefiles to supply common targets (javascriptlint,
# jsstyle, restdown, etc.), which are used by other repos as well. You may well
# need to rewrite most of this file, but you shouldn't need to touch the
# included makefiles.
#
# If you find yourself adding support for new targets that could be useful for
# other projects too, you should add these to the original versions of the
# included Makefiles (in eng.git) so that other teams can use them too.
#

#
# Tools
#
JSL		?= jsl
JSSTYLE		?= jsstyle
NPM		?= npm

#
# Files
#
JS_FILES	:= $(shell find examples lib -name '*.js')

CLEAN_FILES	+= \
		lib/zsock_async_binding.node	\
		src/binding/*.o			\
		src/binding/v8plus_errno.c 	\
		src/binding/v8plus_errno.h 	\
		src/binding/mapfile_node

JSL_CONF_NODE	 = tools/jsl.node.conf
JSL_FILES_NODE   = $(JS_FILES)
JSSTYLE_FILES	 = $(JS_FILES)

#
# Repo-specific targets
#
.PHONY: all
all: deps
	$(NPM) install

.PHONY: deps
deps:
	$(NPM) --no-rebuild install

.PHONY: binding
binding: src/zsocket/zsocket
	(cd src/binding && $(MAKE))

src/zsocket/zsocket: CPPFLAGS += -std=c99 -D_POSIX_C_SOURCE=200112L 
src/zsocket/zsocket: CPPFLAGS += -D__EXTENSIONS__
src/zsocket/zsocket: LDFLAGS += -lxnet -lcontract
src/zsocket/zsocket: src/zsocket/zsocket.c
CLEAN_FILES += src/zsocket/zsocket

include ./Makefile.targ
