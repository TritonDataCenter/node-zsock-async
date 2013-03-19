/*
 * examples/basic.js: example usage of zsock-async
 */

var mod_http = require('http');
var mod_bunyan = require('bunyan');
var mod_spawnasync = require('spawn-async');
var mod_zsockasync = require('..');

var zonepath = '/var/run/zsockasync-example';
var log = new mod_bunyan({ 'name': 'Example' });
var worker, server;

function main()
{
	if (process.argv.length <= 2) {
		console.error('zonename must be specified');
		process.exit(2);
	}

	worker = mod_spawnasync.createWorker({ 'log': log });

	mod_zsockasync.createZoneSocket({
	    'zone': process.argv[2],
	    'path': zonepath,
	    'spawner': worker
	}, function (err, fd) {
		if (err) {
			console.error('failed: %s', err.message);
			return;
		}

		server = mod_http.createServer(onRequest);
		mod_zsockasync.listenFD(server, fd, function () {
			console.log('server listening at %s in zone %s',
			    zonepath, process.argv[2]);
			console.log('try this:');
			console.log('printf "GET / HTTP/1.0\\n\\n" | ' +
			    'zlogin %s nc -U %s', process.argv[2],
			    zonepath);
		});
	});
}

function onRequest(request, response)
{
	console.log('got request');

	response.writeHead(200);
	response.end('hello world\n');
}

main();
