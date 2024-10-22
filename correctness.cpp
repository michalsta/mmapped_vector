#include "mmapped_vector.h"

#include <iostream>
#include <vector>
#include <cassert>


// Write correctness tests for MmappedVector, just correctness, single-threaded, no performance tests

template <typename T>
mmapped_vector::MmappedVector<T, mmapped_vector::MallocAllocator<T>> malloc_vector() {
    return mmapped_vector::MmappedVector<int, mmapped_vector::MallocAllocator<int>>();
}

template <typename T>
mmapped_vector::MmappedVector<T, mmapped_vector::MmapAllocator<T>> mmap_vector() {
    return mmapped_vector::MmappedVector<int, mmapped_vector::MmapAllocator<int>>();
}

template <typename T>
mmapped_vector::MmappedVector<T, mmapped_vector::MmapFileAllocator<T>> mmap_file_vector() {
    return mmapped_vector::MmappedVector<int, mmapped_vector::MmapFileAllocator<int>>("test.dat", MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC, 0);
}


template <typename VectorType>
VectorType empty() {
    static int test_file_no = 0;
    if constexpr (std::is_same<VectorType, std::vector<typename VectorType::value_type>>::value) {
        return std::vector<typename VectorType::value_type>();
    } else if constexpr (std::is_same<VectorType, mmapped_vector::MmappedVector<typename VectorType::value_type, mmapped_vector::MallocAllocator<typename VectorType::value_type>>>::value) {
        return mmapped_vector::MmappedVector<typename VectorType::value_type, mmapped_vector::MallocAllocator<typename VectorType::value_type>>();
    } else if constexpr (std::is_same<VectorType, mmapped_vector::MmappedVector<typename VectorType::value_type, mmapped_vector::MmapAllocator<typename VectorType::value_type>>>::value) {
        return mmapped_vector::MmappedVector<typename VectorType::value_type, mmapped_vector::MmapAllocator<typename VectorType::value_type>>();
    } else if constexpr (std::is_same<VectorType, mmapped_vector::MmappedVector<typename VectorType::value_type, mmapped_vector::MmapFileAllocator<typename VectorType::value_type>>>::value) {
        std::string file_name = "test" + std::to_string(test_file_no++) + ".dat";
        return mmapped_vector::MmappedVector<typename VectorType::value_type, mmapped_vector::MmapFileAllocator<typename VectorType::value_type>>(file_name, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    } else {
        throw std::runtime_error("empty() not implemented for this type");
    }
}


template <typename VectorType>
void run_tests()
{
    VectorType vec = empty<VectorType>();

    // Test empty vector
    assert(vec.size() == 0);
    //assert(vec.capacity() == 0);
    assert(vec.empty());

    // Test push_back
    vec.push_back(1);
    assert(vec.size() == 1);
    assert(vec.capacity() >= 1);
    assert(!vec.empty());
    assert(vec[0] == 1);

    // Test emplace_back
    vec.emplace_back(2);
    assert(vec.size() == 2);
    assert(vec.capacity() >= 2);
    assert(!vec.empty());
    assert(vec[0] == 1);
    assert(vec[1] == 2);

    // Test pop_back
    vec.pop_back();
    assert(vec.size() == 1);
    assert(vec.capacity() >= 1);
    assert(!vec.empty());
    assert(vec[0] == 1);

    // Test at
    assert(vec.at(0) == 1);
    try {
        std::ignore = vec.at(1);
        assert(false);
    } catch (std::out_of_range& e) {
        assert(true);
    }

    // Test front
    assert(vec.front() == 1);

    // Test back
    assert(vec.back() == 1);

    // Test clear
    vec.clear();
    assert(vec.size() == 0);
    assert(vec.capacity() >= 1);
    assert(vec.empty());

    // Skip testing copy constructor
    // Test move constructor
    vec.push_back(1);
    vec.push_back(2);
    VectorType vec2(std::move(vec));
    assert(vec.size() == 0);
    assert(vec.capacity() >= 0);
    assert(vec.empty());
    assert(vec2.size() == 2);
    assert(vec2.capacity() >= 2);
    assert(!vec2.empty());
    assert(vec2[0] == 1);
    assert(vec2[1] == 2);

    // Skip testing copy assignment operator
    // Test move assignment operator
    vec = std::move(vec2);
    assert(vec2.size() == 0);
    assert(vec2.capacity() >= 0);
    assert(vec2.empty());
    assert(vec.size() == 2);
    assert(vec.capacity() >= 2);
    assert(!vec.empty());
    assert(vec[0] == 1);
    assert(vec[1] == 2);

    // Test resize
    vec.resize(1);
    assert(vec.size() == 1);
    assert(vec.capacity() >= 1);
    assert(!vec.empty());
    assert(vec[0] == 1);

    // Test destructor
    VectorType* vec_ptr = new VectorType(empty<VectorType>());
    vec_ptr->push_back(1);
    delete vec_ptr;

    // Test operator[]
    assert(vec[0] == 1);

    // Test operator==
    VectorType vec3 = empty<VectorType>();
    vec.push_back(2);
    vec3.push_back(1);
    vec3.push_back(2);
    assert(vec == vec3);
    vec3.push_back(3);
    assert(!(vec == vec3));

    // Test operator!=
    assert(vec != vec3);
    vec.push_back(3);
    assert(!(vec != vec3));

    // Test cend
    assert(vec.cend() == vec.begin() + vec.size());


}


int main()
{
    std::cerr << "Running tests for std::vector" << std::endl;
    run_tests<std::vector<int>>();
    std::cerr << "done" << std::endl;
    std::cerr << "Running tests for MmappedVector (MallocAllocator)" << std::endl;
    run_tests<mmapped_vector::MmappedVector<int, mmapped_vector::MallocAllocator<int>>>();
    std::cerr << "done" << std::endl;
    std::cerr << "Running tests for MmappedVector (MmapAllocator)" << std::endl;
    run_tests<mmapped_vector::MmappedVector<int, mmapped_vector::MmapAllocator<int>>>();
    std::cerr << "done" << std::endl;
    std::cerr << "Running tests for MmappedVector (MmapFileAllocator)" << std::endl;
    run_tests<mmapped_vector::MmappedVector<int, mmapped_vector::MmapFileAllocator<int>>>();
    std::cerr << "done" << std::endl;

    return 0;
}