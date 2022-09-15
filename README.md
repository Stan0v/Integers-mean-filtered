# Integers-mean-filtered


Here we have 2 multithread approaches:
1. Static threads which count equals with count of system's hardware threads
2. Thread pool with the same threads count

Results:
Array size: 10000000 Iteration count: 1000

SINGLE THREAD execution
Total time, ns: 92390334200 (92.4s) Average time, ns: 92390334 (92.4 ms)

STATIC THREADS execution
Total time, ns: 43352088800 (43.3s) Average time, ns: 43352088 (43.3 ms)

THREAD POOL execution
Total time, ns: 30598625000 (30.6s) Average time, ns: 30598625 (30.6 ms)

COUNT ASSESSMENT
Array size: 10000000 Iteration count: 10
Numbers count: 15232 Time difference: 5470 ns

At first sight, static threads seems to be enough, but after some thinking I came to understanding that some threads would stand idle,
due to corresponding adjacent medians. Nevertheless current implementation in static threads do the task in place while thread pool using additional memory 
to copy sorted elements.

Thread pool implementation based on information from Anthony Williams "C++ Concurrency in Action" book davanced with lock free work stealing queue. 

As about algorithmic complexities 2 approaches could be used:
1. Median search using "n-th element" algorithms like "quickselect" and so on
Current implementation based on miniselect library https://danlark.org/2020/11/11/miniselect-practical-and-generic-selection-algorithms/
and currently using Floyd-Rivest algorithm with O(n) complexity in average and after that filtering process with the same O(n) complexity.

2. Overall sorting O(nlogn) complexity in average and then binary search of element in sorted array with O(logn) complexity
I planned to make a research based on the next state-of-art fresh implementation of quicksort from Google:
https://opensource.googleblog.com/2022/06/Vectorized%20and%20performance%20portable%20Quicksort.html
But unfortunately I met some difficulties and had no time to overcome them. 

Despite of bigger O complexity of 2. option compared with 1. option. Final result could be opposite.
As many factors influence final performance, including data parallelization aspect - SIMD, cache optimizations - e.g.
binary search vs shifts within cache line to search element, thread affinity, thread pool tasks split and so on and so forth
So for the current solution there are still many places to research and optimize.


As about count assesment when single threaded approach performs faster, genetic algorithm could be used to find an extremum. But I implemented it in simple 'binary' powers of 2
