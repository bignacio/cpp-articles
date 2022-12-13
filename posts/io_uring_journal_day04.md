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

    io_uring_cqe_seen(ring, cQueue);
}
```
Note that there are 2 possible failures, one on the wait operation itself and another, in the `res` field, that was the operation previously submitted. In this case, the accept operations.

With that we can accept multiple connections and the accepted socket (the new connection) will be returned in `cQueue->res`.

After the event is processed, `io_uring_cqe_seen` must be called to mark the entry as completed and allow it to be reused.

### Performing other operations

Now that we have the ability to accept socket connections, let's try reading and writing to it and will be doing that by submitting read and write operations to the ring.

Which means we need to know which operation was returned by `io_uring_wait_cqe` since it could be any.
Luckily (actually, by design) the submit queue entry has a `user_data` field that we can use to set any content intended to be passed to a completion queue entry, also in the `user_data` field.

Though `user_data` is an integer field, it can also be, coerced, into taking a pointers and `liburing` offers 2 functions to help with that: `io_uring_sqe_set_data` and `io_uring_cqe_get_data`.

Let's modify the code above to make use of `user_data` to identify completed operations.

```
typedef enum{
  EventTypeAccept,
} EventType;

typedef struct {
  EventType eventType;
} EventData;

struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);
EventData acceptEvent = malloc(sizeof(EventData));
acceptEvent.eventType = EventTypeAccept;

// socketDesc is the listening socket
io_uring_prep_multishot_accept_direct(sQueue, socketDesc, NULL, NULL, 0);

// user_data must be set after prep commands because they will clear various
// fields, including user_data
io_uring_sqe_set_data(sQueue, &acceptEvent);

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

    EventData* cEvent = (EventData*)io_uring_cqe_get_data(cQueue);

    int clientSocket = cQueue->res;

    switch (cEvent->eventType){
    case EventTypeAccept:
        // do something with clientSocket
        break;
    }

    io_uring_cqe_seen(ring, cQueue);
}
```

Calls to `io_uring_sqe_set_data` must be set after prep commands because they will clear various fields in the submission queue entry, including `user_data`.
