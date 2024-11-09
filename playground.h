#include <iostream>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include "allocators.h"

template <typename VectorType>
void test_vector_correctness(VectorType& vec);


template <typename T>
class ThreadSafeVector {
    std::atomic<T*> ptr;
    std::atomic<size_t> element_count;
    std::atomic<size_t> capacity;
    std::atomic<size_t> pushes_done;
    std::mutex m;

public:
    ThreadSafeVector() : ptr(static_cast<T*>(malloc(16*sizeof(T)))) {
        capacity.store(16);
        element_count.store(0);
        pushes_done.store(0);
    };
    ~ThreadSafeVector() {
        free(ptr);
    }

    void push_back(const T& value) {
        size_t place_idx = element_count.fetch_add(1);
        size_t local_capacity = capacity.load();
        if (place_idx >= local_capacity) {
            increase_capacity(place_idx, 1);
        }
        ptr[place_idx] = value;
        pushes_done++;
    }

    // Push n elements from iterator
    /*
    template <typename Iterator>
    void push_back(Iterator begin, Iterator end) {
        size_t n = std::distance(begin, end);
        size_t place_idx = element_count.fetch_add(n);
        size_t local_capacity = capacity.load();
        if (place_idx + n >= local_capacity) {
            increase_capacity(place_idx + n, n);
        }
        while(begin != end) {
            ptr[place_idx++] = *begin++;
        }
        pushes_done += n;
    }*/

    void increase_capacity(size_t needed_idx, size_t reserved_pushes) {
        pushes_done += reserved_pushes;
        std::lock_guard<std::mutex> lock(m);
        size_t local_capacity = capacity.load();
        if (needed_idx < local_capacity)
        {
            pushes_done -= reserved_pushes;
            return;
        }
        while(pushes_done.load() < element_count.load()) {
            //std::cerr << "local_capacity: " << local_capacity << " pushes_done: " << pushes_done.load() << "diff: " << local_capacity - pushes_done.load() << std::endl;
            std::this_thread::yield();
        }
        size_t new_capacity = std::max(2*local_capacity, needed_idx);
        ptr.store(static_cast<T*>(realloc(ptr, new_capacity*sizeof(T))));
        capacity.store(new_capacity);
        pushes_done -= reserved_pushes;
    }

    T& operator[](size_t idx) {
        return ptr[idx];
    }

    friend void test_vector_correctness<>(ThreadSafeVector<T>& vec);
};



template <typename T>
class MutexedVector {
    std::vector<T> vec;
    std::mutex m;

public:
    void push_back(const T& value) {
        std::lock_guard<std::mutex> lock(m);
        vec.push_back(value);
    }

    template <typename Iterator>
    void push_back(Iterator begin, Iterator end) {
        std::lock_guard<std::mutex> lock(m);
        vec.insert(vec.end(), begin, end);
    }

    T& operator[](size_t idx) {
        std::lock_guard<std::mutex> lock(m);
        return vec[idx];
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(m);
        return vec.size();
    }

    size_t capacity() {
        std::lock_guard<std::mutex> lock(m);
        return vec.capacity();
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(m);
        return vec.empty();
    }

    T& front() {
        std::lock_guard<std::mutex> lock(m);
        return vec.front();
    }
/*
    const T& front() {
        std::lock_guard<std::mutex> lock(m);
        return vec.front();
    }
*/
    void pop_back() {
        std::lock_guard<std::mutex> lock(m);
        vec.pop_back();
    }
};


template <typename T>
class ThreadSafeMmapVector {
    mmapped_vector::MmapAllocator<T> allocator;
    std::atomic<size_t> element_count;
    const size_t max_size = 4'398'046'511'104;
public:
    ThreadSafeMmapVector() : allocator() {
        element_count.store(0);
        allocator.resize(max_size);
    }

    void push_back(const T& value) {
        size_t place_idx = element_count.fetch_add(1, std::memory_order_relaxed);
        allocator.get_ptr()[place_idx] = value;
    }

    T& operator[](size_t idx) {
        return allocator.get_ptr()[idx];
    }

    friend void test_vector_correctness<>(ThreadSafeMmapVector<T>& vec);
};

// write correctness tests for ThreadSafeVector, mixing push_back() and push_back(iterator, iterator), multithreaded
// and checking the results


#include <chrono>
#include <string>

class Timer {
public:
    // Constructor starts the timer
    Timer(const std::string& name = "")
        : name(name), start(std::chrono::high_resolution_clock::now()) {}

    // Destructor stops the timer and logs the elapsed time
    ~Timer() {
        std::cout << "Elapsed time" << (name.empty() ? "" : " for " + name)
                  << ": " << getElapsedTime() / 1000000.0 << " seconds\n";
    }

    // Method to get the current elapsed time in microseconds
    long long getElapsedTime() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    }

private:
    std::string name;
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
};