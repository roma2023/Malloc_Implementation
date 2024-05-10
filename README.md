# Malloc Lab

This project is part of the Systems course and focuses on building a dynamic memory allocator. It involves implementing a custom `malloc`, `free`, and `realloc` library that provides efficient and flexible memory management.

## Objectives

1. **Memory Allocation**: Design and implement an allocator that efficiently handles memory requests using `malloc`.
2. **Memory Deallocation**: Create a system that properly frees memory to reduce fragmentation using `free`.
3. **Reallocation**: Implement the `realloc` function to resize existing memory blocks while maintaining data integrity.
4. **Performance Optimization**: Achieve a balance between speed and memory usage, reducing fragmentation as much as possible.
5. **Code Correctness**: Ensure that all memory management functions work correctly and prevent memory leaks.

## Features

- **Explicit Free List**: The allocator maintains an explicit free list to manage free memory blocks efficiently.
- **Boundary Tag Coalescing**: Adjacent free blocks are merged (coalesced) using boundary tags to minimize fragmentation.
- **Segregated Free Lists**: Free blocks are categorized into different size classes for quicker allocation and deallocation.
- **First-Fit Search**: Allocation uses a first-fit strategy to find suitable blocks from the free list.
- **Payload Alignment**: All allocated memory is aligned to the required boundary to meet system requirements.

## Challenges

- **Fragmentation**: Balancing memory fragmentation while maximizing allocation speed is a complex challenge.
- **Coalescing**: Merging adjacent free blocks while ensuring the overall integrity of the free list.
- **Realloc Efficiency**: Adjusting memory blocks dynamically while preserving existing data can be challenging.

## Evaluation and Testing

- **Performance Testing**: The allocator has been tested with various benchmarks to evaluate speed and fragmentation.
- **Correctness Testing**: Memory functions are tested for correctness, including edge cases, to ensure reliability.

## Future Improvements

- **Thread Safety**: Implementing thread safety would be a valuable improvement for concurrent environments.
- **Adaptive Strategies**: An adaptive allocation strategy could further improve memory usage and speed.

## Conclusion

This project has provided a deeper understanding of dynamic memory management and its complexities. By implementing this custom memory allocator, critical skills in low-level memory management, optimization, and debugging have been developed.
