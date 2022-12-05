#  The Power of `io_uring` - A journey

## Day `01` hello world", of sorts

For my little async I/O project using `io_uring`, I have a few not very audacious objects for today:

1. find out what I need to write a C program that uses `io_uring` features
2. ascertain that my dev environment satisfy all dependencies
3. install such dependencies
4. run a simple program that does nothing but the boiler-plate `io_uring` setup


Objective 1. and 2 are not too problematic if one is using a not-too-old linux distro. I'm writing this on Ubuntu 22.10 which right now uses kernel 5.19. So besides a modern linux kernel, we'll also need a C compiler. Not a problem there either.

There are 2 (that I know) ways to use the `io_uring` interface: 1 is through the low level API, dry but it works.
The second and by far the option for most people is using [liburing](https://github.com/axboe/liburing), which provides a much more user friendly layer of abstraction to the low level API, with very little overhead.

Luckily there is a `liburing-dev` package for Ubuntu that is sufficiently up to date to get started.

At the time this writing (circa Dec 2022) on Ubuntu 22.10 I ended up with:
* Linux kernel 5.19
* gcc 12.2
* `liburing` 2.2.2 (from the official repository)

All include files for `liburing` will be in the default include paths and to link the shared library into your program, just pass `-luring` to the compiler.

### Checking what is supported

One of the nice advantages of using `liburing` is that it can hide any of the complexity required to support different kernel versions. It even exposes a way to probe what is currently supported by the running kernel.

That was the first thing I did, here's a snippet ([check the full code here](https://github.com/bignacio/cpp-articles/blob/main/code/io_uring_probe.c))

```
 const char *operations[] = {
      "IORING_OP_NOP",
      "IORING_OP_READV",
      "IORING_OP_WRITEV",
      "IORING_OP_FSYNC",
  ...
  // more operations here

  struct io_uring_probe *probe = io_uring_get_probe();

  for (int op = 0; op <= probe->last_op; op++) {
    if (io_uring_opcode_supported(probe, op)) {
      printf("io_uring op %s supported\n", operations[op]);
    } else {
      printf("io_uring operation %s NOT supported\n", operations[op]);
    }
  }

  io_uring_free_probe(probe);
```


This will print everything the kernel supports and does not support.
Note that the `operations` array was based on the enum `io_uring_op` provided by `liburing`.

Perhaps not surprisingly all operations are supported, which is good to know even though I certainly do not intend to use them all.

Next: `io_uring` initialization, parameters and kernel features.