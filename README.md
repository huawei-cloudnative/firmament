Firmament is a cluster manager and scheduling platform developed CamSaS
(http://camsas.org) at the University of Cambridge Computer Laboratory.

It is currently in early alpha stage, with much of the high-level functionality
still missing, and interfaces frequently changing.


## System requirements

Firmament is currently known to work on Ubuntu 13.04 (raring) and 14.04
(trusty); with caveats (see below) on 13.10 (saucy); it does NOT work on
versions prior to 12.10 (quantal) as they cannot build libpion, which is now
included as a self-built dependency in order to ease transition to libpion v5
and for compatibility with Arch Linux.

Other configurations are untested - YMMV. Recent Debian versions typically work
with a bit of fiddling.

Reasons for known breakage:
 * Ubuntu 13.04 - segfault failures when using Boost 1.53 packages; use 1.49
                  (default).
 * Ubuntu 13.10 - clang/LLVM include paths need to be fixed.
                  /usr/{lib,include}/clang/3.2 should be symlinked to
                  /usr/lib/llvm-3.2/lib/clang/3.2.

## Getting started

After cloning the repository,

```
$ make all
```

fetches dependencies are necessary, and may ask you to install required
packages.

```
$ make test
```

runs unit tests.

Other targets can be listed by running

```
$ make help
```

Binaries are in the build/ subdirectory of the project root, and all accept the
`--helpshort` argument to show their command line options.

Start up by running a coordinator:

```
$ build/engine/coordinator --listen_uri tcp://<host>:<port> --task_lib_path=$(PWD)/build/engine/
```

Once the coordinator is up and running, you can access its HTTP interface at
http://<host>:8080/ (the port can be customized using `--http_ui_port`
argument).

To submit a toy job, first make the examples target and then use the script in
`scripts/job/job_submit.py`. Note that jobs are submitted to the web UI port,
and NOT the internal listen port!

```
$ make examples
$ cd scripts/job/
$ make
$ python job_submit.py <host> <webUI port (8080)> <binary>
```

Example for the last line:

```
$ python job_submit.py localhost 8080 /bin/sleep 60
```

(Note that you may need to run `make` in the script directory since the script
depends on some protocol buffer data structures that need to be compiled. If
you have run `make all`, all script dependencies should automatically have been
built, though.)

If this all works, you should see the job print "Hello world" in the
coordinator's console.