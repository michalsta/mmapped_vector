#include <iostream>
#include <vector>
#include <chrono>
#include "mmapped_vector.h" // Adjust the path as necessary

#define TEST_SIZE 1000

using namespace mmapped_vector;

double test_std_vector_performance() {
    std::vector<int> vec;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < TEST_SIZE; ++i) {
        vec.push_back(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    return duration.count();
}

double test_mmapped_vector_performance() {
    MmappedVector<int, MallocAllocator<int, false> > vec; // Assuming mmapped_vector is similar to std::vector
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < TEST_SIZE; ++i) {
        vec.push_back(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    return duration.count();
}

double test_mmapped_vector_mmap_allocator_performance() {
    MmappedVector<int, MmapAllocator<int, false>> vec({MAP_ANONYMOUS | MAP_PRIVATE})); // Assuming mmapped_vector with mmap_allocator
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < TEST_SIZE; ++i) {
        vec.push_back(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    return duration.count();
}

int main() {
    double std_vector_duration = test_std_vector_performance();
    double mmapped_vector_duration = test_mmapped_vector_performance();
    double mmapped_vector_mmap_allocator_duration = test_mmapped_vector_mmap_allocator_performance();

    std::cout << "std::vector push_back duration: " << std_vector_duration << " seconds" << std::endl;
    std::cout << "mmapped_vector (MallocAllocator) push_back duration: " << mmapped_vector_duration << " seconds" << std::endl;
    std::cout << "mmapped_vector (MmapAllocator) push_back duration: " << mmapped_vector_mmap_allocator_duration << " seconds" << std::endl;

    if (mmapped_vector_duration != 0) {
        double speedup_factor_malloc = std_vector_duration / mmapped_vector_duration;
        std::cout << "Speedup factor (MallocAllocator): " << speedup_factor_malloc << std::endl;
    } else {
        std::cout << "mmapped_vector (MallocAllocator) duration is zero, cannot calculate speedup factor." << std::endl;
    }

    if (mmapped_vector_mmap_allocator_duration != 0) {
        double speedup_factor_mmap = std_vector_duration / mmapped_vector_mmap_allocator_duration;
        std::cout << "Speedup factor (MmapAllocator): " << speedup_factor_mmap << std::endl;
    } else {
        std::cout << "mmapped_vector (MmapAllocator) duration is zero, cannot calculate speedup factor." << std::endl;
    }

    return 0;
}