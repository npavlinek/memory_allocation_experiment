# Memory Allocation Experiment

## About

The experiment was to see whether it is worth it to implement and use a custom memory allocator for performance and code simplicity.

There are three different implementations:

- STL version: using `std::string` and `std::vector`, a fairly standard implementation
- Non-STL version: using only `malloc`, a custom string builder and storing file names as a linked list
- Custom allocator version: the same as the non-STL version, but using a custom linear allocator

## Results

Running this program inside the Mozilla source code repository (changeset `66e3220110ba0dd99ba7d45684ac4731886a59a9`):

```
STL version took 4.89 s and found 1062892 items
No STL version took 3.88 s and found 1062892 items
Custom allocator version took 3.87 s and found 1062892 items
```
