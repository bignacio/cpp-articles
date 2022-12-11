# #  The Power of `io_uring` - A journey

## Day 02: Initialization, parameters and kernel features

For this round of unambitious goals, we'll setup a basic `io_uring` infrastructure that can be used later to actually perform asynchronous I/O operations.

`io_uring` gets its name from the fact it uses ring buffers for communication between the kernel and the user space application.
These ring buffers are initialized via a call to `io_uring_setup` which has the following signature:
```
int io_uring_setup(u32 entries, struct io_uring_params *p);
```
The parameter `entries` refers to the number of entries in the ring buffer in the submission and completion queues. This essentially determines the maximum number of I/O operations that can be performed concurrently.

By default the number of completion queue entries is twice the value of `entries` and the documentation suggests that it might be two small for network I/O, which multiple connections (sockets) performing concurrent operations.

The ideal value, as usual, will have to be tested and benchmarked for each case and ideally the application would implement a back pressure strategy when being overloaded with requests. To set a custom completion queue size, set the parameter field `cq_entries` and the add flag `IORING_SETUP_CQSIZE` to `flags`.

Once the ring buffers are initialized, the application can submit I/O operations to the kernel via the `io_uring_submit` call, which adds the operations to the submission queue.
The application can consume (reap) I/O completion events using various wait constructs offered by `liburing` and retrieve the results of the completed operations.

Note that `p` is modified after the call and it's various fields can be inspected in case you need to see what the kernel will be using for the ring setup.

### Some notes on tunning for performance

#### Setup parameters

Though I mentioned I won't be benchmarking any of the examples here, I'm interested in getting the most of `io_uring` and a few things caught my attention.

First, when setting up the ring it's possible to start a kernel thread to poll the submission queue and avoid making system calls for that. This is done by setting `IORING_SETUP_SQPOLL` in the `io_uring_params` `flags` field when calling `io_uring_setup`. Quoting the documentation

> By using the submission queue to fill in new submission queue entries and watching for completions on the completion queue, the application can submit and reap I/Os without doing a single system call.

This kernel thread will polling non stop if busy (see below for details) which means it could end up using a lot of CPU.
This might not be a problem in our case here since our application is expected to be process a large number of concurrent requests.

When using kernel level polling, I would strongly recommend setting `sq_thread_idle` and `sq_thread_cpu` in the setup parameters.

`sq_thread_idle` is set in milliseconds and determines how long the kernel thread (created due to `IORING_SETUP_SQPOLL`) will keep polling before going to sleep when there are no entries in the submission queue. This can help reducing the load on the system when there's no work to do.

The other recommendation is to set a core affinity for the kernel thread doing the polling. This is achieved by setting `sq_thread_cpu` to a [core ID](https://github.com/bignacio/dxpool#the-worker-pool) and setting the flag `IORING_SETUP_SQ_AFF` in the `io_uring_params` `flags` field.
This would help dedicating a core for async processing and potentially reducing cache misses, cross NUMA lookups and general switch overhead.