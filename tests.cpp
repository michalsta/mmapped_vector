#include "mmapped_vector.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <stdexcept>

// Enum to specify the vector type
enum class VectorType {
    STD_VECTOR,
    MMAPPED_ANON,
    MMAPPED_FILE
};

// Vector wrapper to handle different initializations
template<typename T, VectorType type>
class VectorWrapper;

// Specialization for std::vector
template<typename T>
class VectorWrapper<T, VectorType::STD_VECTOR> {
public:
    using VectorT = std::vector<T>;
    VectorT vec;
};

// Specialization for MmappedVector with anonymous memory
template<typename T>
class VectorWrapper<T, VectorType::MMAPPED_ANON> {
public:
    using VectorT = MmappedVector<T>;
    VectorT vec;
};

// Specialization for MmappedVector with file-backed storage
template<typename T>
class VectorWrapper<T, VectorType::MMAPPED_FILE> {
public:
    using VectorT = MmappedVector<T>;
    VectorT vec;
    VectorWrapper() : vec("test_file.bin") {}
};

// Test function template
template<typename T, VectorType type>
void run_vector_tests() {
    std::cout << "Running tests for "
              << (type == VectorType::STD_VECTOR ? "std::vector" :
                  type == VectorType::MMAPPED_ANON ? "MmappedVector (anonymous)" :
                  "MmappedVector (file-backed)") << std::endl;

    VectorWrapper<T, type> wrapper;
    auto& vec = wrapper.vec;

    // Test push_back and size
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    assert(vec.size() == 3);
    std::cout << "push_back and size: OK" << std::endl;

    // Test operator[]
    assert(vec[0] == 1);
    assert(vec[1] == 2);
    assert(vec[2] == 3);
    std::cout << "operator[]: OK" << std::endl;

    // Test front and back
    assert(vec.front() == 1);
    assert(vec.back() == 3);
    std::cout << "front and back: OK" << std::endl;

    // Test clear
    vec.clear();
    assert(vec.size() == 0);
    assert(vec.empty());
    std::cout << "clear: OK" << std::endl;

    // Test resize
    vec.clear();
    vec.resize(5);  // Resize to 5 elements
    assert(vec.size() == 5);
    std::cout << "resize: OK" << std::endl;

    // Test reserve
    size_t old_capacity = vec.capacity();
    vec.reserve(100);
    assert(vec.capacity() >= 100);
    assert(vec.capacity() > old_capacity);
    assert(vec.size() == 5);  // Size should not change after reserve
    std::cout << "reserve: OK" << std::endl;

    // Test push_back after reserve
    vec.clear();
    for (int i = 0; i < 150; ++i) {
        vec.push_back(i);
    }
    assert(vec.size() == 150);
    assert(vec.capacity() >= 150);
    std::cout << "push_back after reserve: OK" << std::endl;

    // Test data access
    T* data_ptr = vec.data();
    assert(data_ptr[0] == 0);
    assert(data_ptr[149] == 149);
    std::cout << "data access: OK" << std::endl;

    std::cout << "All tests passed!" << std::endl << std::endl;
}

// Test file persistence for MmappedVector with file-backed storage
void test_file_persistence() {
    std::cout << "Testing file persistence for MmappedVector (file-backed)" << std::endl;
    {
        VectorWrapper<int, VectorType::MMAPPED_FILE> wrapper;
        auto& vec = wrapper.vec;
        vec.clear();
        vec.push_back(10);
        vec.push_back(20);
        vec.push_back(30);
    } // vec goes out of scope, should sync to file

    // Re-open the file
    VectorWrapper<int, VectorType::MMAPPED_FILE> wrapper;
    auto& vec = wrapper.vec;
    assert(vec.size() == 3);
    assert(vec[0] == 10);
    assert(vec[1] == 20);
    assert(vec[2] == 30);
    std::cout << "File persistence: OK" << std::endl << std::endl;
}

int main() {
    try {
        run_vector_tests<int, VectorType::STD_VECTOR>();
        run_vector_tests<int, VectorType::MMAPPED_ANON>();
        run_vector_tests<int, VectorType::MMAPPED_FILE>();
        test_file_persistence();
        std::cout << "All tests completed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}




