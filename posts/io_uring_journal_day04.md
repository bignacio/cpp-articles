# The Power of `io_uring` - A journey

## Day 04: An event loop for `io_uring`

The event loop is required for us to obtain the results of anything that was submitted to the kernel (via `io_uring_submit`) and what we need very similar to what would be implemented using `select` or `poll`/`epoll` but with some important differences.

Unlike mechanisms using non-blocking sockets, submitting an operation doesn't tell the user when to perform that operation but it actually executes it. In other words, submitting a write us the equivalent of performing an async write.

Since we're using `io_uring_prep_multishot_accept_direct`, we can call it once before the event loop starts and just process reads, writes etc in the loop itself.

The code for that would look like this

```
struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);

// socketDesc is the listening socket
io_uring_prep_multishot_accept_direct(sQueue, socketDesc, NULL, NULL, 0);

io_uring_submit(ring);

while(true){
    struct io_uring_cqe *cQueue = NULL;
    int waitErr = io_uring_wait_cqe(ring, &cQueue);

    if (waitErr != 0) {
        perror(strerror(-waitErr));
        exit(1);
    }

    if (cQueue->res < 0) {
        perror(strerror(-cQueue->res));
        exit(1);
    }
}
```

With that we can accept multiple connections and the accepted socket (the new connection) will be returned in `cQueue->res`.

### Performing other operations

Now we have