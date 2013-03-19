/*
 * zsock-async.js: open Unix Doman Sockets inside illumos zones
 */

var mod_assert = require('assert');
var mod_fs = require('fs');
var mod_path = require('path');

var binding = require('./zsock_async_binding');

/* public interface */
exports.createZoneSocket = createZoneSocket;
exports.listenFD = listenFD;

/*
 * Creates a Unix Domain Socket in the specified zone.  Note: it is invalid to
 * have multiple operations pending for the same zone, regardless of the path.
 * The resulting behavior is undefined.  (If this limitation becomes important,
 * it's relatively easy to remove, but makes clean up of temporary files a bit
 * more complex.)
 *
 * Named arguments include:
 *
 *    zone (string)		zonename of the zone in which to create the UDS
 *    (required)
 *
 *    path (string)		path (inside the zone) to create the UDS
 *    (required)
 *
 *    spawner			spawn-async worker process (see
 *    (required)		node-spawn-async)
 *
 * Callback is invoked as "callback(err, fd)", where "fd" is an integer.  To use
 * this, see listenFD.
 */
function createZoneSocket(args, callback)
{
	mod_assert.equal(typeof (args), 'object',
	    'named arguments are required');
	mod_assert.equal(typeof (args['zone']), 'string',
	    'zonename (string) is required');
	mod_assert.equal(typeof (args['path']), 'string',
	    'path (string) is required');
	mod_assert.equal(typeof (args['spawner']), 'object',
	    'spawner (object) is required');
	mod_assert.equal(typeof (callback), 'function',
	    'callback (function) is required');

	var cmd = mod_path.join(__dirname, '../src/zsocket/zsocket');
	var gzpath = mod_path.join('/tmp', args['zone'] + '-zsocket-create');
	var argv = [ cmd, args['zone'], args['path'], gzpath ];

	/*
	 * This process is a little circuitous.  In order to securely bind a
	 * socket inside a zone, we must create the socket and bind it from a
	 * process inside the zone itself.  Otherwise, it would be possible for
	 * the zone to use symlinks to trick us into thinking we're talking to
	 * the zone when we're actually talking to a component in another zone
	 * (or even the global zone itself).  (Binding the socket from a process
	 * inside the zone makes this impossible, since that process cannot see
	 * anything outside the zone.)
	 *
	 * In order to do that, we need to fork() and zone_enter() in the child
	 * process.  The related node-zsock add-on implements this using a
	 * straight fork/zone_enter from the main thread.  Unfortunately, even
	 * performing that operation occasionally can have significant impacts
	 * on the event loop.  node-spawn-async exists to offload that to a
	 * child process in order to keep the main event loop responsive.  In
	 * order to use that, we have to phrase what we want in terms of a
	 * program we can "exec".
	 *
	 * That introduces a new problem: how can we receive a file descriptor
	 * opened by a child of our child process, without special support from
	 * the immediate child process (the spawn-async worker)?  To do this, we
	 * create a Unix Domain Socket for communciating with our grandchild,
	 * and we spawn "zsocket" (built with this add-on) as our grandchild.
	 * That program takes as an argument the path to our Unix Domain Socket,
	 * does the fork()/zone_enter() dance described above, and then sends
	 * the bound zsocket over the named Unix Domain Socket we created at the
	 * start.
	 *
	 * Before we do anything, we remove the temporary UDS, in case it's
	 * still there from a previous operation.
	 */
	mod_fs.unlink(gzpath, function (err0) {
		if (err0 && err0['code'] != 'ENOENT') {
			callback(err0);
			return;
		}

		/*
		 * Now create a new "channel": this binds a new Unix Domain
		 * Socket to the given path.  Once we have the channel, we must
		 * call channel._close() to close the underlying fd, regardless
		 * of whether the "zsocket" command succeeds or not.
		 */
		var channel = new (binding._new)(gzpath);

		/*
		 * Spawn "zsocket" to create the zone socket and send it over
		 * the channel.
		 */
		args['spawner'].aspawn(argv, function (err1, stdout, stderr) {
			var fd;

			if (err1) {
				err1 = new Error('failed to create zsocket: ' +
				    err1.message + ' (stderr: ' +
				    JSON.stringify(stderr) + ')');
			} else {
				fd = channel._recvfd();
				mod_assert.equal(typeof (fd), 'number');
			}

			/*
			 * Try to clean up the socket we used.  Ignore failures
			 * to remove the file, but wait until the unlink
			 * completes before returning.
			 */
			channel._close();
			mod_fs.unlink(gzpath,
			    function () { callback(err1, fd); });
		    });
	});
}

/*
 * Use the given net.Server object to begin listening for connections on the
 * given file descriptor, a Unix Domain Socket returned by createZoneSocket.
 * This uses Node-private interfaces, and is thus subject to breakage, but is
 * known to work on Node versions 0.6 and 0.8.
 */
function listenFD(socket, fd, callback)
{
	var Pipe = process.binding('pipe_wrap').Pipe;
	var p = new Pipe(true);
	p.open(fd);
	p.readable = p.writable = true;
	socket._handle = p;
	socket.listen(callback);
}
