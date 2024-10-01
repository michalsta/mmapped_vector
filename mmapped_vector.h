#include <sys/mman.h>
#include <string>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring> // for std::memcpy
#include <mutex>
#include <atomic>

#include "allocators.h"

namespace mmapped_vector {

template <typename T, typename AllocatorType, bool thread_safe = false>
class MmappedVector {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable for safe memory movement");
    // TODO static_assert(std::is_base_of<Allocator, AllocatorType>::value, "AllocatorType must be derived from Allocator");

private:
    AllocatorType allocator;
    std::conditional_t<thread_safe, std::atomic<size_t>, size_t> element_count;

public:
    // Default constructor: Creates an empty vector with initial capacity
    MmappedVector(AllocatorType allocator = MallocAllocator<T, thread_safe>());

    // Destructor: Cleans up resources
    ~MmappedVector();

    // Deleted copy constructor and assignment operator
    MmappedVector(const MmappedVector& other) = delete;
    MmappedVector& operator=(const MmappedVector& other) = delete;

    // Move constructor
    MmappedVector(MmappedVector&& other) noexcept = delete;

    // Move assignment operator
    MmappedVector& operator=(MmappedVector&& other) noexcept = delete;

    // Adds an element to the end of the vector
    void push_back(const T& value);

    // Constructs an element in-place at the end of the vector
    template<typename... Args>
    void emplace_back(Args&&... args);

    // Removes the last element from the vector
    void pop_back();

    // Access element at specified index (no bounds checking)
    T& operator[](size_t index);
    const T& operator[](size_t index) const;

    // Returns the number of elements in the vector
    size_t size() const;

    // Returns the current capacity of the vector
    size_t capacity() const;

    // Checks if the vector is empty
    bool empty() const;

    // Returns a reference to the first element
    T& front();
    const T& front() const;

    // Returns a reference to the last element
    T& back();
    const T& back() const;

    // Removes all elements from the vector
    void clear();

    // Changes the number of elements stored
    void resize(size_t new_size);

    // Requests that the vector capacity be at least enough to contain n elements
    void reserve(size_t new_capacity);

    // Reduces memory usage by freeing unused memory
    void shrink_to_fit();

    // Returns pointer to the underlying array
    T* data();
    const T* data() const;

    // Iterator support
    T* begin();
    T* end();
    const T* begin() const;
    const T* end() const;
    const T* cbegin() const;
    const T* cend() const;

    // Access element at specified index (with bounds checking)
    T& at(size_t pos);
    const T& at(size_t pos) const;

    // Synchronizes the mapped memory with the underlying storage device
    void sync();

    // Comparison operators
    bool operator==(const MmappedVector& other) const;
    bool operator!=(const MmappedVector& other) const;

private:
};

// Method implementations

template <typename T, typename AllocatorType, bool thread_safe>
MmappedVector<T, AllocatorType, thread_safe>::MmappedVector(AllocatorType _allocator)
    : allocator(_allocator), element_count(allocator.get_size()) {};

template <typename T, typename AllocatorType, bool thread_safe>
MmappedVector<T, AllocatorType, thread_safe>::~MmappedVector() {};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T& MmappedVector<T, AllocatorType, thread_safe>::operator[](size_t index) const {
    return allocator.T[index];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T& MmappedVector<T, AllocatorType, thread_safe>::operator[](size_t index) {
    return allocator.T[index];
};


// TODO fix this
template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::push_back(const T& value) {
    allocator.resize(element_count + 1);
    allocator[element_count++] = value;
};

// TODO: shrink if needed
template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::pop_back() {
    element_count--;
};


template <typename T, typename AllocatorType, bool thread_safe> inline
size_t MmappedVector<T, AllocatorType, thread_safe>::size() const {
    return element_count;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
size_t MmappedVector<T, AllocatorType, thread_safe>::capacity() const {
    return allocator.get_capacity();
};

template <typename T, typename AllocatorType, bool thread_safe> inline
bool MmappedVector<T, AllocatorType, thread_safe>::empty() const {
    return element_count == 0;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T& MmappedVector<T, AllocatorType, thread_safe>::front() {
    return allocator[0];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T& MmappedVector<T, AllocatorType, thread_safe>::front() const {
    return allocator[0];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T& MmappedVector<T, AllocatorType, thread_safe>::back() {
    return allocator[element_count - 1];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T& MmappedVector<T, AllocatorType, thread_safe>::back() const {
    return allocator[element_count - 1];
};

// TODO shrink if needed
template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::clear() {
    element_count = 0;
};

// TODO probably needs to be deleted
template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::resize(size_t new_size) {
    allocator.resize(new_size);
    element_count = new_size;
};


} // namespace mmapped_vector