#define _GNU_SOURCE
#include <sys/mman.h>
#include <string>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring> // for std::memcpy
#include <mutex>

#if defined(__APPLE__) || defined(__MACH__)
    #define IS_MACOS 1
#else
    #define IS_MACOS 0
#endif


template <typename T, bool thread_safe = false>
class MmappedVector {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable for safe memory movement");

private:
    std::string filename;
    int fd;
    void* addr;
    std::conditional_t<thread_safe, std::atomic<size_t>, size_t> element_count;
    size_t allocated_size;
    typename std::enable_if<thread_safe, std::mutex>::type realloc_guard;

public:
    // Default constructor: Creates an empty vector with initial capacity
    MmappedVector();

    // Constructor with initial capacity
    explicit MmappedVector(size_t initial_capacity);

    // Constructor with filename: Creates or opens a file-backed vector
    explicit MmappedVector(const std::string& filename);

    // Destructor: Cleans up resources
    ~MmappedVector();

    // Deleted copy constructor and assignment operator
    MmappedVector(const MmappedVector& other) = delete;
    MmappedVector& operator=(const MmappedVector& other) = delete;

    // Move constructor
    MmappedVector(MmappedVector&& other) noexcept;

    // Move assignment operator
    MmappedVector& operator=(MmappedVector&& other) noexcept;

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

    // Returns the filename if the vector is file-backed
    const std::string& get_filename() const;

    // Comparison operators
    bool operator==(const MmappedVector& other) const;
    bool operator!=(const MmappedVector& other) const;

private:
    // Ensures that the vector has at least the specified capacity
    void ensure_capacity(size_t new_capacity);
    void resize_mapping(size_t new_capacity) {
        if (new_capacity != allocated_size) {

            try {
                if (fd != -1) {
                    if (ftruncate(fd, sizeof(T) * new_capacity) == -1) {
                        throw std::runtime_error("Failed to resize file: " + std::string(strerror(errno)));
                    }
                }

                #if IS_MACOS
                    // macOS implementation
                    void* new_addr = mmap(nullptr, sizeof(T) * new_capacity,
                                          PROT_READ | PROT_WRITE,
                                          fd == -1 ? MAP_PRIVATE | MAP_ANONYMOUS : MAP_SHARED,
                                          fd, 0);
                    if (new_addr == MAP_FAILED) {
                        throw std::runtime_error("mmap failed: " + std::string(strerror(errno)));
                    }
                    std::memcpy(new_addr, addr, sizeof(T) * std::min(element_count, new_capacity));
                    if (munmap(addr, sizeof(T) * allocated_size) == -1) {
                        throw std::runtime_error("munmap failed: " + std::string(strerror(errno)));
                    }
                    addr = new_addr;
                #else
                    // Linux implementation
                    addr = mremap(addr, sizeof(T) * allocated_size, sizeof(T) * new_capacity, MREMAP_MAYMOVE);
                    if (addr == MAP_FAILED) {
                        throw std::runtime_error("mremap failed: " + std::string(strerror(errno)));
                    }
                #endif

                allocated_size = new_capacity;
            } catch (const std::exception& e) {
                throw std::runtime_error("Failed to resize mapping: " + std::string(e.what()));
            } catch (...) {
                throw std::runtime_error("Unknown error occurred while resizing mapping");
            }
        }
    }
};

// Method implementations

template <typename T>
MmappedVector<T>::MmappedVector() : fd(-1), element_count(0), allocated_size(16) {
    addr = mmap(nullptr, sizeof(T) * allocated_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        throw std::runtime_error(mmapped_vector_get_error_message("Memory mapping for default constructor"));
    }
}

template <typename T>
MmappedVector<T>::MmappedVector(size_t initial_capacity)
    : fd(-1), element_count(0), allocated_size(initial_capacity) {
    addr = mmap(nullptr, sizeof(T) * allocated_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        throw std::runtime_error(mmapped_vector_get_error_message("Memory mapping for constructor with initial capacity"));
    }
}

template <typename T>
MmappedVector<T>::MmappedVector(const std::string& filename)
    : filename(filename) {
    fd = open(filename.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        throw std::runtime_error(mmapped_vector_get_error_message("Opening file '" + filename + "'"));
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        throw std::runtime_error(mmapped_vector_get_error_message("Getting file stats for '" + filename + "'"));
    }

    if (sb.st_size == 0) {
        // File is empty, initialize with a minimum size
        element_count = 0;
        allocated_size = 16; // Or any other suitable initial size
        if (ftruncate(fd, sizeof(T) * allocated_size) == -1) {
            close(fd);
            throw std::runtime_error(mmapped_vector_get_error_message("Initializing empty file '" + filename + "'"));
        }
    } else {
        element_count = sb.st_size / sizeof(T);
        allocated_size = element_count;
    }

    addr = mmap(nullptr, sizeof(T) * allocated_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error(mmapped_vector_get_error_message("Memory mapping file '" + filename + "'"));
    }
}
template <typename T>
MmappedVector<T>::~MmappedVector() {
    for (size_t i = 0; i < element_count; ++i) {
        static_cast<T*>(addr)[i].~T();
    }
    munmap(addr, sizeof(T) * allocated_size);
    if (fd != -1) {
        if (ftruncate(fd, sizeof(T) * element_count) == -1) {
            // Handle error (perhaps log it, as we can't throw in a destructor)
            // For now, we'll just ignore the error
        }
        close(fd);
    }
}

template <typename T>
MmappedVector<T>::MmappedVector(MmappedVector&& other) noexcept
    : filename(std::move(other.filename)), fd(other.fd), addr(other.addr),
      element_count(other.element_count), allocated_size(other.allocated_size) {
    other.fd = -1;
    other.addr = nullptr;
    other.element_count = 0;
    other.allocated_size = 0;
}

template <typename T>
MmappedVector<T>& MmappedVector<T>::operator=(MmappedVector&& other) noexcept {
    if (this != &other) {
        this->~MmappedVector();
        filename = std::move(other.filename);
        fd = other.fd;
        addr = other.addr;
        element_count = other.element_count;
        allocated_size = other.allocated_size;
        other.fd = -1;
        other.addr = nullptr;
        other.element_count = 0;
        other.allocated_size = 0;
    }
    return *this;
}

template <typename T>
void MmappedVector<T>::push_back(const T& value) {
    if (element_count == allocated_size) {
        ensure_capacity(allocated_size * 2);  // Growth factor hardcoded to 2
    }
    *(static_cast<T*>(addr) + element_count) = value;
    ++element_count;
}

template <typename T>
template<typename... Args>
void MmappedVector<T>::emplace_back(Args&&... args) {
    if (element_count == allocated_size) {
        ensure_capacity(allocated_size * 2);  // Growth factor hardcoded to 2
    }
    new (static_cast<T*>(addr) + element_count) T(std::forward<Args>(args)...);
    ++element_count;
}

template <typename T, bool thread_safe>
void MmappedVector<T, thread_safe>::pop_back() {

    size_t current_count;
    if constexpr (thread_safe) {
        current_count = element_count.fetch_sub(1, std::memory_order_relaxed);
    } else {
        current_count = --element_count;
    }
    if (current_count == 0) {
        throw std::out_of_range("Vector is empty");
    }
    static_cast<T*>(addr)[current_count].~T();
}

template <typename T>
T& MmappedVector<T>::operator[](size_t index) {
    return static_cast<T*>(addr)[index];
}

template <typename T>
const T& MmappedVector<T>::operator[](size_t index) const {
    return static_cast<const T*>(addr)[index];
}

template <typename T>
size_t MmappedVector<T>::size() const {
    return element_count;
}

template <typename T>
size_t MmappedVector<T>::capacity() const {
    return allocated_size;
}

template <typename T>
bool MmappedVector<T>::empty() const {
    return element_count == 0;
}

template <typename T>
T& MmappedVector<T>::front() {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return static_cast<T*>(addr)[0];
}

template <typename T>
const T& MmappedVector<T>::front() const {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return static_cast<const T*>(addr)[0];
}

template <typename T>
T& MmappedVector<T>::back() {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return static_cast<T*>(addr)[element_count - 1];
}

template <typename T>
const T& MmappedVector<T>::back() const {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return static_cast<const T*>(addr)[element_count - 1];
}

template <typename T>
void MmappedVector<T>::clear() {
    for (size_t i = 0; i < element_count; ++i) {
        static_cast<T*>(addr)[i].~T();
    }
    element_count = 0;
}

template <typename T>
void MmappedVector<T>::resize(size_t new_size) {
    if (new_size > element_count) {
        ensure_capacity(new_size);
        for (size_t i = element_count; i < new_size; ++i) {
            new (static_cast<T*>(addr) + i) T();
        }
    } else if (new_size < element_count) {
        for (size_t i = new_size; i < element_count; ++i) {
            static_cast<T*>(addr)[i].~T();
        }
    }
    element_count = new_size;
}

template <typename T>
void MmappedVector<T>::reserve(size_t new_capacity) {
    if (new_capacity > allocated_size) {
        ensure_capacity(new_capacity);
    }
}

template <typename T>
void MmappedVector<T>::shrink_to_fit() {
    if (allocated_size > element_count) {
        resize_mapping(element_count);
    }
}

template <typename T>
T* MmappedVector<T>::data() {
    return static_cast<T*>(addr);
}

template <typename T>
const T* MmappedVector<T>::data() const {
    return static_cast<const T*>(addr);
}

template <typename T>
T* MmappedVector<T>::begin() {
    return static_cast<T*>(addr);
}

template <typename T>
T* MmappedVector<T>::end() {
    return static_cast<T*>(addr) + element_count;
}

template <typename T>
const T* MmappedVector<T>::begin() const {
    return static_cast<const T*>(addr);
}

template <typename T>
const T* MmappedVector<T>::end() const {
    return static_cast<const T*>(addr) + element_count;
}

template <typename T>
const T* MmappedVector<T>::cbegin() const {
    return static_cast<const T*>(addr);
}

template <typename T>
const T* MmappedVector<T>::cend() const {
    return static_cast<const T*>(addr) + element_count;
}

template <typename T>
T& MmappedVector<T>::at(size_t pos) {
    if (pos >= element_count) {
        throw std::out_of_range("Index out of range");
    }
    return static_cast<T*>(addr)[pos];
}

template <typename T>
const T& MmappedVector<T>::at(size_t pos) const {
    if (pos >= element_count) {
        throw std::out_of_range("Index out of range");
    }
    return static_cast<const T*>(addr)[pos];
}

template <typename T>
void MmappedVector<T>::sync() {
    if (fd != -1) {
        if (msync(addr, sizeof(T) * element_count, MS_SYNC) == -1) {
            throw std::runtime_error("msync failed");
        }
    }
}

template <typename T>
const std::string& MmappedVector<T>::get_filename() const {
    return filename;
}

template <typename T>
bool MmappedVector<T>::operator==(const MmappedVector& other) const {
    if (element_count != other.element_count) return false;
    return std::equal(begin(), end(), other.begin());
}

template <typename T>
bool MmappedVector<T>::operator!=(const MmappedVector& other) const {
    return !(*this == other);
}

template <typename T>
void MmappedVector<T>::ensure_capacity(size_t new_capacity) {
    if (new_capacity > allocated_size) {
        resize_mapping(new_capacity);
    }
}