#include <iostream>
#include <vector>
#include <chrono>
#include "mmapped_vector.h"

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
    results.push_back({"mmapped_vector (MallocAllocator)", test_vector_performance<MmappedVector<size_t, MallocAllocator<size_t>>>()});
    results.push_back({"mmapped_vector (MmapAllocator)", test_vector_performance<MmappedVector<size_t, MmapAllocator<size_t>>>()});
    results.push_back({"mmapped_vector (FileAllocator)", test_vector_performance<MmappedVector<size_t, MmapFileAllocator<size_t>>>(test_file, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC)});
    remove(test_file.c_str());
    //results.push_back({"mmapped_vector (MallocAllocator, thread_safe)", test_vector_performance<MmappedVector<size_t, MallocAllocator<size_t>, true>>()});
    //results.push_back({"mmapped_vector (MmapAllocator, thread_safe)", test_vector_performance<MmappedVector<size_t, MmapAllocator<size_t>, true>>()});
    //results.push_back({"mmapped_vector (FileAllocator, thread_safe)", test_vector_performance<MmappedVector<size_t, MmapFileAllocator<size_t>, true>>(test_file, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC)});
    //remove(test_file.c_str());

    for (const auto& result : results) {
        std::cout << result.name << " push_back duration: " << result.duration << " seconds" << std::endl;
    }

    results.push_back({"item_count", static_cast<double>(TEST_SIZE)});

    std::cout << "[" << std::endl;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        std::cout << "  {" << std::endl;
        std::cout << "    \"name\": \"" << result.name << "\"," << std::endl;
        std::cout << "    \"duration\": " << result.duration << std::endl;
        std::cout << "  }";
        if (i < results.size() - 1) {
            std::cout << ",";
        }
        std::cout << std::endl;
    }
    std::cout << "]" << std::endl;

    return 0;
}
