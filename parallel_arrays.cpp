#include <iostream>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <thread>
#include <cstddef>

typedef std::size_t size_t;


template<typename T>
class ParallelArray
{
    T* data;
    const size_t size;
    std::mutex m;
public:
    ParallelArray(size_t size) : size(size)
    {
        data = new T[size];
    }

    ~ParallelArray()
    {
        delete[] data;
    }

    T& operator[](size_t idx)
    {
        return data[idx];
    }

    size_t Size() const
    {
        return size;
    }
};

const size_t size = 1000000;
const size_t n_threads = std::thread::hardware_concurrency();

void print_sum(ParallelArray<int>& arr)
{
    return;
    int sum = 0;
    for(size_t i = 0; i < arr.Size(); i++)
    {
        sum += arr[i];
    }
    std::cout << "Sum: " << sum << std::endl;
}

// multithreaded performance tests
double test_atomic()
{
    ParallelArray<int> arr(size*n_threads);
    std::atomic<size_t> index = 0;
    static_assert(std::atomic<size_t>::is_always_lock_free);
    auto f = [&]()
    {
        int tostore = 0;
        for(size_t i = 0; i < size; i++)
        {
            size_t local_idx = index.fetch_add(1, std::memory_order_relaxed);
            arr[local_idx] = tostore;
            tostore++;
        }
    };
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    for (size_t i = 0; i < n_threads; i++)
    {
        threads.push_back(std::thread(f));
    }
    for (auto& t : threads)
        t.join();
    auto end = std::chrono::high_resolution_clock::now();
    print_sum(arr);
    return std::chrono::duration<double>(end - start).count();
}

double test_stripes()
{
    ParallelArray<int> arr(size*n_threads);
    auto f = [&](size_t stripe)
    {
        int tostore = 0;
        for(size_t i = 0; i < size; i++)
        {
            arr[stripe + n_threads * i] = tostore;
            tostore++;
        }
    };
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    for (size_t i = 0; i < n_threads; i++)
    {
        threads.push_back(std::thread(f, i));
    }
    for (auto& t : threads)
        t.join();
    auto end = std::chrono::high_resolution_clock::now();
    print_sum(arr);
    return std::chrono::duration<double>(end - start).count();
}

double test_chunked()
{
    ParallelArray<int> arr(size*n_threads);
    auto f = [&](size_t chunk)
    {
        int tostore = 0;
        for(size_t i = 0; i < size; i++)
        {
            arr[chunk*size + i] = tostore;
            tostore++;
        }
    };
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    for (size_t i = 0; i < n_threads; i++)
    {
        threads.push_back(std::thread(f, i));
    }
    for (auto& t : threads)
        t.join();
    auto end = std::chrono::high_resolution_clock::now();
    print_sum(arr);
    return std::chrono::duration<double>(end - start).count();
}


double test_mutexed()
{
    ParallelArray<int> arr(size*n_threads);
    std::mutex m;
    size_t index = 0;
    auto f = [&]()
    {
        int tostore = 0;
        for(size_t i = 0; i < size; i++)
        {
            std::lock_guard<std::mutex> lock(m);
            arr[index] = tostore;
            index++;
            tostore++;
        }
    };
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    for (size_t i = 0; i < n_threads; i++)
    {
        threads.push_back(std::thread(f));
    }
    for (auto& t : threads)
        t.join();
    auto end = std::chrono::high_resolution_clock::now();
    print_sum(arr);
    return std::chrono::duration<double>(end - start).count();
}




int main()
{
    std::cout << "Atomic: " << test_atomic() << "s\n";
    std::cout << "Stripes: " << test_stripes() << "s\n";
    std::cout << "Chunked: " << test_chunked() << "s\n";
    std::cout << "Mutexed: " << test_mutexed() << "s\n";
    return 0;
}