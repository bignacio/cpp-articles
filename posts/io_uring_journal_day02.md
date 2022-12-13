# The Power of `io_uring` - A journey

## Day 02: Initialization, parameters and kernel features

For this round of unambitious goals, we'll setup a basic `io_uring` infrastructure that can be used later to actually perform asynchronous I/O operations.

`io_uring` gets its name from the fact it uses ring buffers for communication between the kernel and the user space application.
These ring buffers can be initialized in many ways but perhaps the most convenient is `io_uring_queue_init_params` which has the following signature:
```
int io_uring_queue_init_params(unsigned entries, struct io_uring *ring, struct io_uring_params *p);
```
The parameter `entries` refers to the number of entries in the ring buffer in the submission and completion queues. This essentially determines the maximum number of I/O operations that can be performed concurrently.

By default the number of completion queue entries is twice the value of `entries` and the documentation suggests that it might be two small for network I/O, which multiple connections (sockets) performing concurrent operations.

The ideal value, as usual, will have to be tested and benchmarked for each case and ideally the application would implement a back pressure strategy when being overloaded with requests. To set a custom completion queue size, set the parameter field `cq_entries` and the add flag `IORING_SETUP_CQSIZE` to `flags`.

The second parameter `ring` is a pointer to an allocated `struct io_uring ring` variable.

The parameters passed in `p` can be used to fine tune the ring's behaviour. See below for some examples and consult `liburing`'s documentation for more.

Once the ring buffers are initialized, the application can submit I/O operations to the kernel via the `io_uring_submit` call, which adds the operations to the submission queue.
The application can consume (reap) I/O completion events using various wait constructs offered by `liburing` and retrieve the results of the completed operations.

Note that `p` is modified after the call and it's various fields can be inspected in case you need to see what the kernel will be using for the ring setup.

### Some notes on tunning for performance

#### Setup parameters

Though I mentioned I won't be benchmarking any of the examples here, I'm interested in getting the most of `io_uring` and a few things caught my attention.

First, when setting up the ring it's possible to start a kernel thread to poll the submission queue and avoid making system calls for that. This is done by setting `IORING_SETUP_SQPOLL` in the `io_uring_params` `flags` field when calling `io_uring_queue_init_params`. Quoting the documentation

> By using the submission queue to fill in new submission queue entries and watching for completions on the completion queue, the application can submit and reap I/Os without doing a single system call.

This kernel thread will polling non stop if busy (see below for details) which means it could end up using a lot of CPU.
This might not be a problem in our case here since our application is expected to be process a large number of concurrent requests.

When using kernel level polling, I would strongly recommend setting `sq_thread_idle` and `sq_thread_cpu` in the setup parameters.

`sq_thread_idle` is set in milliseconds and determines how long the kernel thread (created due to `IORING_SETUP_SQPOLL`) will keep polling before going to sleep when there are no entries in the submission queue. This can help reducing the load on the system when there's no work to do.

The other recommendation is to set a core affinity for the kernel thread doing the polling. This is achieved by setting `sq_thread_cpu` to a [core ID](https://github.com/bignacio/dxpool#the-worker-pool) and setting the flag `IORING_SETUP_SQ_AFF` in the `io_uring_params` `flags` field.
This would help dedicating a core for async processing and potentially reducing cache misses, cross NUMA lookups and general switch overhead.

### File descriptors and allocation

Working with `io_uring` is all about file descriptors, the ring itself has one and everything else we use for I/O is via file descriptors.

The kernel maintains a file descriptor table internally for application and doing that has a cost, which gets higher and if the application is multi-threaded.

Fortunately for us, `liburing` provides a nice way to register file tables the ring will be using, as well as the ring file descriptor itself.

To register the ring file descriptor, just call `io_uring_register_ring_fd` passing the ring initialized in the previous step.

To register a file descriptor table there are a few different options, one would be via `io_uring_register_files`, if you want to be specific about which file descriptors to use. The other is `io_uring_register_files_sparse` where an empty file descriptor table and uses descriptors as unused spots are required.

The number of descriptors required depends on the application. I am expecting to register a few hundred file descriptors, perhaps thousands, for application that hold a large number of connections at the same time.

To use the registered descriptors when performing I/O operations, we need to use a different set of functions usually denoted by the infix `_direct`. We'll see that in details in the next chapters.

Next: [Accepting connections in the `io_uring` universe.](io_uring_journal_day03.md)