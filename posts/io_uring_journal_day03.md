# The Power of `io_uring` - A journey

## Day 03: Accepting connections in the `io_uring` universe

Now that we have our `io_uring` ring setup, we can start doing something with it and I chose to continue from the stand point of a server listening for requests on a socket.

Any operation we perform is submitted to the ring's queue to be executed asynchronously, as one would assume. The minimum steps required to submit any operation are:

1. obtain a submission queue entry (`sqe`)
1. prepare the entry with the intended operation
1. submit the operation to the kernel (actually the ring and any prepared operation)

Note it's possible to prepare various operations and submit them all with a single submit call.

After submitting an operation, we can obtain (reap) the result by calling `io_uring_wait_cqe`.

### A server side socket

I'll assume the reader is familiar with the process of [having a socket listening on a port](https://github.com/bignacio/cpp-articles/blob/main/code/socket_listen.c). We'll then start from the point where the socket is ready to accept requests, and for that, we'll use `liburing`'s `io_uring_prep_multishot_accept_direct` call.

There are a few ways to submit accept requests with `liburing` but we'll use this for a few reasons.

First it's multi-shot, which means we don't need to submit a new accept operation everything a connection is accepted.

The other reason is the use of *direct* file descriptors (remember the [file descriptor table registration we did last time](io_uring_journal_day02.md#file-descriptors-and-allocation)?).


The parameters passed to `io_uring_prep_multishot_accept_direct` are the ring, a submission queue and the rest are the same as you'd pass to [accept4](https://linux.die.net/man/2/accept4). One notable fact is the output `socketaddr` value does not make much sense in a multi-shot setup since it could be overwritten by other accept operations before being read.

Putting everything together, this his how the code might look like.

```

int serverSocket;
// here would be the code to create, bind and put the socket in listen mode

struct io_uring *ring = malloc(sizeof(struct io_uring));
memset(ring, 0, sizeof(struct io_uring));

struct io_uring_params uringParams;
memset(&uringParams, 0, sizeof(struct io_uring_params));


uringParams.sq_thread_idle = 2; // millis before putting the worker thread to sleep

int errorCode = io_uring_queue_init_params(8, ring, &uringParams);

if (errorCode != 0) {
    perror(strerror(-errorCode));
    exit(1);
}

errorCode = io_uring_register_ring_fd(ring);
if (errorCode != 1) {
    perror(strerror(-errorCode));
    exit(1);
}

errorCode = io_uring_register_files_sparse(ring, params.numFilesInTable);
if (errorCode != 0) {
    perror(strerror(-errorCode));
    exit(1);
}

// the ring is setup, let's start accepting connections

struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);
io_uring_prep_multishot_accept_direct(sQueue, socketDesc, NULL, NULL, 0);


if(io_uring_submit(ring) != 1){
    exit(1);
}

// here our application would be accepting connections
```
Not the best code but you get the idea. Just don't copy/paste + leave it untouched.

Note that the error code returned is negative so we need to negate it again to find out what it was.

Now we need to process all the accept operations we executed and for that, we need an event loop.

Next: [An event loop for `io_uring`.](io_uring_journal_day04.md)