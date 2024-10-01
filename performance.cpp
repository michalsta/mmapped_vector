#include <iostream>
#include <vector>
#include <chrono>
#include "mmapped_vector.h" // Adjust the path as necessary

size_t TEST_SIZE = 10000000;

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
    struct TestResult {
        std::string name;
        double duration;
    };
    std::vector<TestResult> results;

    results.push_back({"std::vector", test_vector_performance<std::vector<size_t>>()});
    results.push_back({"mmapped_vector (MallocAllocator)", test_vector_performance<MmappedVector<size_t, MallocAllocator<size_t, false>>>()});
    results.push_back({"mmapped_vector (MmapAllocator)", test_vector_performance<MmappedVector<size_t, MmapAllocator<size_t, false>>>()});
    results.push_back({"mmapped_vector (FileAllocator)", test_vector_performance<MmappedVector<size_t, MmapFileAllocator<size_t, false>>>(test_file, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC)});
    remove(test_file.c_str());
    results.push_back({"mmapped_vector (MallocAllocator, thread_safe)", test_vector_performance<MmappedVector<size_t, MallocAllocator<size_t, true>, true>>()});
    results.push_back({"mmapped_vector (MmapAllocator, thread_safe)", test_vector_performance<MmappedVector<size_t, MmapAllocator<size_t, true>, true>>()});
    results.push_back({"mmapped_vector (FileAllocator, thread_safe)", test_vector_performance<MmappedVector<size_t, MmapFileAllocator<size_t, true>, true>>(test_file, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC)});
    remove(test_file.c_str());

    for (const auto& result : results) {
        std::cout << result.name << " push_back duration: " << result.duration << " seconds" << std::endl;
    }

    double std_vector_duration = results[0].duration;

    for (const auto& result : results) {
        if (result.name != "std::vector") {
            if (result.duration != 0) {
                double speedup_factor = std_vector_duration / result.duration;
                std::cout << "Speedup factor (" << result.name << "): " << speedup_factor << std::endl;
            } else {
                std::cout << result.name << " duration is zero, cannot calculate speedup factor." << std::endl;
            }
        }
    }


    return 0;
}
