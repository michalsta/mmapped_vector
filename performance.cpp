#include <iostream>
#include <vector>
#include <chrono>
#include "mmapped_vector.h" // Adjust the path as necessary

size_t TEST_SIZE = 1000000000;

using namespace mmapped_vector;
template <typename VectorType, typename... Args>
double test_vector_performance(Args&&... args) {
    VectorType vec(std::forward<Args>(args)...);
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < TEST_SIZE; ++i) {
        vec.push_back(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    return duration.count();
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        TEST_SIZE = std::stoull(argv[1]);
    }
    std::string test_file = "/home/mist/test.dat";
    if(argc > 2) {
        test_file = argv[2];
    }


    double std_vector_duration = test_vector_performance<std::vector<int>>();
    double mmapped_vector_duration = test_vector_performance<MmappedVector<int, MallocAllocator<int, false>>>();
    double mmapped_vector_mmap_allocator_duration = test_vector_performance<MmappedVector<int, MmapAllocator<int, false>>>();
    double mmapped_vector_file_allocator_duration = test_vector_performance<MmappedVector<int, MmapFileAllocator<int, false>>>(test_file, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC);

    std::cout << "std::vector push_back duration: " << std_vector_duration << " seconds" << std::endl;
    std::cout << "mmapped_vector (MallocAllocator) push_back duration: " << mmapped_vector_duration << " seconds" << std::endl;
    std::cout << "mmapped_vector (MmapAllocator) push_back duration: " << mmapped_vector_mmap_allocator_duration << " seconds" << std::endl;
    std::cout << "mmapped_vector (FileAllocator) push_back duration: " << mmapped_vector_file_allocator_duration << " seconds" << std::endl;

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

    if (mmapped_vector_file_allocator_duration != 0) {
        double speedup_factor_file = std_vector_duration / mmapped_vector_file_allocator_duration;
        std::cout << "Speedup factor (FileAllocator): " << speedup_factor_file << std::endl;
    } else {
        std::cout << "mmapped_vector (FileAllocator) duration is zero, cannot calculate speedup factor." << std::endl;
    }

    remove(test_file.c_str());

    return 0;
}
