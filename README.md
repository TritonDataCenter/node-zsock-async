# node-zsock-async: open Unix Doman Sockets inside illumos zones

This is an asynchronous version of the node-zsock add-on.  As for why async
matters here, see the node-spawn-async module.

This add-on allows programs running in the global zone of illumos- and
Solaris-based systems (such as SmartOS or OmniOS) to securely create servers
listening on a Unix Domain Socket bound inside a local zone.  The idea is that
an agent running in the global zone securely binds a UDS to a well-known path
inside the local zone and starts an HTTP server.  Components running inside the
zone can make HTTP requests over the socket, and the global zone agent always
knows which zone is talking to it.

For an example of using this add-on, see examples/basic.js.
