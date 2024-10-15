#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>

#include "mmapped_vector.h"
#include "playground.h"


// Write performance tests for MmappedVector with MallocAllocator and MmapAllocator, multithreaded
// compared to std::vector guarded by std::mutex

#define NO_THREADS 1
#define TEST_SIZE 10000000

using namespace mmapped_vector;
template <typename VectorType>
void test_vector_correctness(VectorType& vec) {
    //spawn threads
    const size_t thread_count = 4;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < thread_count; ++i) {
        threads.push_back(std::thread([&vec]() {
            for (size_t i = 0; i < 1000000; ++i) {
                vec.push_back(i);
            }
        }));
    }
    /*for (size_t i = 0; i < thread_count; ++i) {
        threads.push_back(std::thread([&vec]() {
            std::vector<int> v;
            for (size_t i = 0; i < 10; ++i) {
                v.push_back(i);
            }
            for(size_t i = 0; i < 1000000/10; ++i) {
                vec.push_back(v.begin(), v.end());
                vec.push_back(11);
            }
        }));
    }*/
    for (auto& thread : threads) {
        thread.join();
    }
    size_t sum = 0;
    for (size_t i = 0; i < 4000000; ++i) {
        sum += vec[i];
    }
    std::cout << "Sum: " << sum << std::endl;
}

template <typename VectorType>
double test_vector_performance(VectorType& vec) {
    //spawn threads
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NO_THREADS; ++i) {
        threads.push_back(std::thread([&vec]() {
            for (size_t i = 0; i < TEST_SIZE / NO_THREADS; ++i) {
                vec.push_back(i);
            }
        }));
    }
    for (auto& thread : threads) {
        thread.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    return duration.count();
}

int main() {
    std::string test_file = "/home/mist/test.dat";
    struct TestResult {
        std::string name;
        double duration;
    };
    std::vector<TestResult> results;
/*
    MmappedVector<size_t, MallocAllocator<size_t, true>, true> vec1;
    results.push_back({"mmapped_vector (MallocAllocator, thread_safe)", test_vector_performance(vec1)});

    MmappedVector<size_t, MmapAllocator<size_t, true>, true> vec2;
    results.push_back({"mmapped_vector (MmapAllocator, thread_safe)", test_vector_performance(vec2)});

    MmappedVector<size_t, MmapFileAllocator<size_t, true>, true> vec3(test_file, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC);
    results.push_back({"mmapped_vector (FileAllocator, thread_safe)", test_vector_performance(vec3)});
    remove(test_file.c_str());
*/
    ThreadSafeVector<size_t> vec4;
    results.push_back({"ThreadSafeVector", test_vector_performance(vec4)});
    std::vector<size_t> vec5;
    results.push_back({"std::vector", test_vector_performance(vec5)});
    ThreadSafeVector<size_t> vec6;
    MutexedVector<size_t> vec7;
    test_vector_correctness(vec7);

    for (const auto& result : results) {
        std::cout << result.name << " push_back duration: " << result.duration << " seconds" << std::endl;
    }

    return 0;
}