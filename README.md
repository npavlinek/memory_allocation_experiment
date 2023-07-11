# Memory Allocation Experiment

## About

The experiment was to see whether it is worth it to implement and use a custom memory allocator for performance and code simplicity.

There are three different implementations:

- STL version: using `std::string` and `std::vector`, a fairly standard implementation
- Non-STL version: using only `malloc`, a custom string builder and storing file names as a linked list
- Custom allocator version: the same as the non-STL version, but using a custom linear allocator

The implementations recursively iterate over all files and directories inside of the current directory, saving all file names into memory.

## Results

Running this program inside the Mozilla source code repository (changeset `66e3220110ba0dd99ba7d45684ac4731886a59a9`):

```
STL version took 5.09 s and found 1062892 items
Non-STL version took 4.13 s and found 1062892 items
Custom allocator version took 4.09 s and found 1062892 items
```

In this case the custom allocator version is slightly faster, sometimes it is even faster and other times it is slower than the non-STL version. I still see this as a win though. Even if the custom allocator breaks even with `malloc`, it is still a win because of its' simplicity; the custom allocator is 19 lines of code (depending on how you count code), there is no way `malloc` is anywhere close to that.

## License

Zero Clause BSD License, a public domain equivalent license. See the `LICENSE` file for more details.
