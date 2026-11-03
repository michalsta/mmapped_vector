/**
 * @file allocators.h
 * @brief File containing the Allocator class and its specializations.
 * @author Michał Startek
 * @version 0.1
 * @copyright Copyright (c) Michał Startek 2024
 */

#ifndef MMAPPED_VECTOR_ALLOCATORS_H
#define MMAPPED_VECTOR_ALLOCATORS_H

#include <cstddef>
#include <string>
#include <sys/mman.h>
#include <mutex>
#include <variant>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <stdexcept>
#if defined(__APPLE__) && defined(__MACH__) && !defined(MREMAP_MAYMOVE)
#define MMAPPED_VECTOR_USE_MACH_VM 1
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

#define MMV_DEBUG 0

#include "error_handling.h"
#include "misc.h"
#include <new>


namespace mmapped_vector {


// Initialised once per program (C++17 inline variable), not once per translation unit.
inline const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));

template <typename T, typename AllocatorType, bool thread_safe>
class MmappedVector;

template <typename T, typename AllocatorType>
class IndexHolder;

template <typename T>
class Allocator
{
protected:
    T* ptr;
    size_t capacity;
public:
    Allocator();
    Allocator(const Allocator&) = delete;
    virtual ~Allocator();
    Allocator& operator=(const Allocator&) = delete;


    virtual void resize(size_t new_size) = 0;
    void increase_capacity(size_t capacity_needed);
    size_t get_capacity() const;
    T* get_ptr() const;
    virtual size_t get_backing_size() const;
    virtual void sync(size_t used_elements);

    friend class MmappedVector<T, Allocator, false>;
    friend class MmappedVector<T, Allocator, true>;
    friend class IndexHolder<T, Allocator>;
};


template <typename T> Allocator<T>::Allocator() : ptr(nullptr), capacity(0) {};
template <typename T> Allocator<T>::~Allocator() {};


template <typename T> inline
size_t Allocator<T>::get_capacity() const {
    return this->capacity;
}

template <typename T> inline
T* Allocator<T>::get_ptr() const {
    return this->ptr;
}

template <typename T> inline
size_t Allocator<T>::get_backing_size() const {
    return 0;
}

template <typename T> inline
void Allocator<T>::increase_capacity(size_t capacity_needed) {
    if(this->capacity >= capacity_needed) return;
    size_t new_capacity;
    if(this->capacity <= 8)
        new_capacity = 16;
    else
        new_capacity = this->capacity;
    while(new_capacity < capacity_needed)
#if MMV_DEBUG
        new_capacity += 16;
#else
        new_capacity *= 2;
#endif
    resize(new_capacity);
}


template <typename T> inline
void Allocator<T>::sync(size_t) {};

/*
 * =================================================================================================
 */

template <typename T>
class MmapAllocator : public Allocator<T>
{

public:
    MmapAllocator();
    MmapAllocator(int flags);
    MmapAllocator(const MmapAllocator&) = delete;
    MmapAllocator(MmapAllocator&&) noexcept;
    MmapAllocator& operator=(MmapAllocator&& other) noexcept;
    ~MmapAllocator() override;
    MmapAllocator& operator=(const MmapAllocator&) = delete;

    void resize(size_t new_size) override;

    // A MAP_SHARED anonymous mapping cannot grow on Linux (mremap does not grow the shmem
    // object behind it: SIGBUS), so on every platform it holds at most this many bytes and
    // growing past them throws std::length_error, whatever the page size.
    static constexpr size_t shared_max_bytes = 4096;

    friend class MmappedVector<T, MmapAllocator, false>;
    friend class MmappedVector<T, MmapAllocator, true>;
private:
    int mmap_flags;
};


template <typename T>
MmapAllocator<T>::MmapAllocator() : MmapAllocator<T>(MAP_ANONYMOUS | MAP_PRIVATE) {};


template <typename T>
MmapAllocator<T>::MmapAllocator(int flags) : Allocator<T>(), mmap_flags(flags) {
    this->ptr = static_cast<T*>(mmap(nullptr, page_size, PROT_READ | PROT_WRITE, flags, -1, 0));
    if (this->ptr == MAP_FAILED) {
        throw std::runtime_error("MmapAllocator::ctor: mmap failed: " + mmapped_vector::get_error_message("mmap"));
    }
    this->capacity = ((flags & MAP_SHARED) ? shared_max_bytes : page_size) / sizeof(T);
}

template <typename T>
MmapAllocator<T>::MmapAllocator(MmapAllocator&& other) noexcept : Allocator<T>(), mmap_flags(other.mmap_flags) {
    this->ptr = other.ptr;
    this->capacity = other.capacity;
    other.ptr = nullptr;
    other.capacity = 0;
}

template <typename T>
MmapAllocator<T>& MmapAllocator<T>::operator=(MmapAllocator&& other) noexcept {
    if (this != &other) {
        if (this->ptr) {
            munmap(this->ptr, this->capacity * sizeof(T));
        }
        this->ptr = other.ptr;
        this->capacity = other.capacity;
        this->mmap_flags = other.mmap_flags;
        other.ptr = nullptr;
        other.capacity = 0;
    }
    return *this;
}

template <typename T>
MmapAllocator<T>::~MmapAllocator() {
    if (this->ptr) {
        munmap(this->ptr, this->capacity * sizeof(T));
        this->ptr = nullptr;
        this->capacity = 0;
    }
}

template <typename T>
void MmapAllocator<T>::resize(size_t new_capacity) {
    if (new_capacity == this->capacity) return;
    if ((this->mmap_flags & MAP_SHARED) && new_capacity > shared_max_bytes / sizeof(T))
        throw std::length_error("MmapAllocator::resize: a MAP_SHARED anonymous mapping cannot grow past "
                                + std::to_string(shared_max_bytes) + " bytes");

    // Capacity 0 holds no mapping at all (ptr == nullptr); growing from there maps afresh.
    if (new_capacity == 0) {
        if (this->ptr && munmap(this->ptr, this->capacity * sizeof(T)) == -1)
            throw std::runtime_error("MmapAllocator::resize: munmap failed: " + mmapped_vector::get_error_message("munmap"));
        this->ptr = nullptr;
        this->capacity = 0;
        return;
    }
    if (this->ptr == nullptr) {
        void* p = mmap(nullptr, new_capacity * sizeof(T), PROT_READ | PROT_WRITE, this->mmap_flags, -1, 0);
        if (p == MAP_FAILED)
            throw std::runtime_error("MmapAllocator::resize: mmap failed: " + mmapped_vector::get_error_message("mmap"));
        this->ptr = static_cast<T*>(p);
        this->capacity = new_capacity;
        return;
    }

#ifdef MREMAP_MAYMOVE
    void* new_ptr = mremap(this->ptr, this->capacity * sizeof(T), new_capacity * sizeof(T), MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED)
        throw std::runtime_error("MmapAllocator::resize: mremap failed: " + mmapped_vector::get_error_message("mremap"));
    this->ptr = static_cast<T*>(new_ptr);

#elif MMAPPED_VECTOR_USE_MACH_VM
    // macOS has no mremap. Grow without copying: first try to extend the region in place,
    // otherwise move its pages into a new region with mach_vm_remap (no data is copied).
    const mach_vm_size_t pg = vm_page_size;
    const mach_vm_size_t old_bytes = (this->capacity * sizeof(T) + pg - 1) & ~(pg - 1);
    const mach_vm_size_t new_bytes = (new_capacity * sizeof(T) + pg - 1) & ~(pg - 1);
    const mach_port_t task = mach_task_self();
    const mach_vm_address_t old_addr = reinterpret_cast<mach_vm_address_t>(this->ptr);
    auto fail = [](const char* what, kern_return_t kr) {
        throw std::runtime_error(std::string("MmapAllocator::resize: ") + what + " failed: " + mach_error_string(kr));
    };

    if (new_bytes < old_bytes) {
        kern_return_t kr = mach_vm_deallocate(task, old_addr + new_bytes, old_bytes - new_bytes);
        if (kr != KERN_SUCCESS) fail("mach_vm_deallocate", kr);
    } else if (new_bytes > old_bytes) {
        mach_vm_address_t tail = old_addr + old_bytes;
        mach_vm_address_t new_addr = old_addr;
        if (mach_vm_allocate(task, &tail, new_bytes - old_bytes, VM_FLAGS_FIXED) != KERN_SUCCESS) {
            // Address range after the region is taken: move the region.
            new_addr = 0;
            kern_return_t kr = mach_vm_allocate(task, &new_addr, new_bytes, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS) fail("mach_vm_allocate", kr);
            mach_vm_address_t dst = new_addr;
            vm_prot_t cur_prot, max_prot;
            kr = mach_vm_remap(task, &dst, old_bytes, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                               task, old_addr, FALSE, &cur_prot, &max_prot, VM_INHERIT_DEFAULT);
            if (kr != KERN_SUCCESS) {
                mach_vm_deallocate(task, new_addr, new_bytes);
                fail("mach_vm_remap", kr);
            }
            kr = mach_vm_deallocate(task, old_addr, old_bytes);
            if (kr != KERN_SUCCESS) fail("mach_vm_deallocate", kr);
        }
        this->ptr = reinterpret_cast<T*>(new_addr);
    }

#else
    void* new_ptr = mmap(nullptr, new_capacity * sizeof(T), PROT_READ | PROT_WRITE, this->mmap_flags, -1, 0);
    if (new_ptr == MAP_FAILED) {
        throw std::runtime_error("MmapAllocator::resize: mmap failed: " + mmapped_vector::get_error_message("mmap"));
    }
    std::copy(this->ptr, this->ptr + std::min(this->capacity, new_capacity), static_cast<T*>(new_ptr));
    if(munmap(this->ptr, this->capacity * sizeof(T)) == -1)
        throw std::runtime_error("MmapAllocator::resize: munmap failed: " + mmapped_vector::get_error_message("munmap"));
    this->ptr = static_cast<T*>(new_ptr);

#endif

    this->capacity = new_capacity;
}


/*
 * =================================================================================================
 */



template <typename T>
class MmapFileAllocator : public Allocator<T>
{
public:
    MmapFileAllocator(const std::string& file_name, int mmap_flags = MAP_SHARED, int open_flags = O_RDWR | O_CREAT, mode_t mode = S_IRUSR | S_IWUSR);
    MmapFileAllocator(const MmapFileAllocator&) = delete;
    MmapFileAllocator(MmapFileAllocator&&) noexcept;
    MmapFileAllocator& operator=(MmapFileAllocator&&) noexcept;
    ~MmapFileAllocator() override;
    MmapFileAllocator& operator=(const MmapFileAllocator&) = delete;

    void resize(size_t new_size) override;
    size_t get_backing_size() const override;
    void sync(size_t used_elements) override;

    friend class MmappedVector<T, MmapFileAllocator, false>;
    friend class MmappedVector<T, MmapFileAllocator, true>;
private:
    void self_close();
    std::string file_name;
    int file_descriptor;
    int mmap_flags;
    size_t backing_size;
};

template <typename T> inline
size_t MmapFileAllocator<T>::get_backing_size() const {
    return this->backing_size;
}

template <typename T>
MmapFileAllocator<T>::MmapFileAllocator(const std::string& file_name, int mmap_flags, int open_flags, mode_t mode) : Allocator<T>() {

    RAIIFileDescriptor fd(open(file_name.c_str(), open_flags, mode));
    if (fd.get() == -1) {
        std::string error_message = "MmapFileAllocator::ctor: " + file_name + ": " + mmapped_vector::get_error_message("open");
        throw std::runtime_error(error_message);
    }

    struct stat st;
    if (fstat(fd.get(), &st) == -1)
        throw std::runtime_error("MmapFileAllocator::ctor: fstat failed: " + mmapped_vector::get_error_message("fstat"));

    if(st.st_size % sizeof(T) != 0)
        throw std::runtime_error("MmapFileAllocator::ctor: file size is not a multiple of sizeof(T). It's probably corrupted.");

    this->backing_size = st.st_size / sizeof(T);
    this->capacity = this->backing_size;
    if(this->capacity < 16)
    {
        this->capacity = 16;
        if(ftruncate(fd.get(), this->capacity * sizeof(T)) == -1)
            throw std::runtime_error("MmapFileAllocator::ctor: ftruncate failed: " + mmapped_vector::get_error_message("ftruncate"));

    }

    this->ptr = static_cast<T*>(mmap(nullptr, this->capacity * sizeof(T), PROT_READ | PROT_WRITE, mmap_flags, fd.get(), 0));
    if (this->ptr == MAP_FAILED)
        throw std::runtime_error("MmapFileAllocator::ctor: mmap failed: " + mmapped_vector::get_error_message("mmap"));

    this->file_name = file_name;
    this->mmap_flags = mmap_flags;
    this->file_descriptor = fd.release();

}

template <typename T>
MmapFileAllocator<T>::MmapFileAllocator(MmapFileAllocator&& other) noexcept : Allocator<T>() {
    this->ptr = other.ptr;
    this->capacity = other.capacity;
    this->backing_size = other.backing_size;
    this->file_name = std::move(other.file_name);
    this->file_descriptor = other.file_descriptor;
    this->mmap_flags = other.mmap_flags;
    other.ptr = nullptr;
    other.capacity = 0;
    other.backing_size = 0;
    other.file_descriptor = -1;
}

template <typename T>
MmapFileAllocator<T>& MmapFileAllocator<T>::operator=(MmapFileAllocator&& other) noexcept {
    if (this != &other) {
        self_close();
        this->ptr = other.ptr;
        this->capacity = other.capacity;
        this->backing_size = other.backing_size;
        this->file_name = std::move(other.file_name);
        this->file_descriptor = other.file_descriptor;
        this->mmap_flags = other.mmap_flags;
        other.ptr = nullptr;
        other.capacity = 0;
        other.backing_size = 0;
        other.file_descriptor = -1;
    }
    return *this;
}

template <typename T>
void MmapFileAllocator<T>::self_close() {
    if (this->file_descriptor == -1) return;  // moved-from
    this->resize(this->get_backing_size());   // truncate the file to the actual size (may be 0)
    if (this->ptr)
        munmap(this->ptr, this->capacity * sizeof(T));
    close(this->file_descriptor);
    this->file_descriptor = -1;
    this->ptr = nullptr;
    this->backing_size = 0;
    this->capacity = 0;
}


template <typename T>
MmapFileAllocator<T>::~MmapFileAllocator() {
    self_close();
}

template <typename T>
void MmapFileAllocator<T>::resize(size_t new_capacity) {
    if (new_capacity == this->capacity) return;

    if (ftruncate(this->file_descriptor, new_capacity * sizeof(T)) == -1)
        throw std::runtime_error("MmapFileAllocator::resize: ftruncate failed: " + mmapped_vector::get_error_message("ftruncate"));

    // Capacity 0 holds no mapping at all (ptr == nullptr); growing from there maps afresh.
    if (new_capacity == 0) {
        if (this->ptr && munmap(this->ptr, this->capacity * sizeof(T)) == -1)
            throw std::runtime_error("MmapFileAllocator::resize: munmap failed: " + mmapped_vector::get_error_message("munmap"));
        this->ptr = nullptr;
        this->capacity = 0;
        return;
    }
    if (this->ptr == nullptr) {
        void* p = mmap(nullptr, new_capacity * sizeof(T), PROT_READ | PROT_WRITE, this->mmap_flags, this->file_descriptor, 0);
        if (p == MAP_FAILED)
            throw std::runtime_error("MmapFileAllocator::resize: mmap failed: " + mmapped_vector::get_error_message("mmap"));
        this->ptr = static_cast<T*>(p);
        this->capacity = new_capacity;
        return;
    }

#ifdef MREMAP_MAYMOVE
    void* new_ptr = mremap(this->ptr, this->capacity * sizeof(T), new_capacity * sizeof(T), MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED) {
        throw std::runtime_error("MmapFileAllocator: mremap failed: " + mmapped_vector::get_error_message("mremap"));
    }
#else
    if(munmap(this->ptr, this->capacity * sizeof(T)) == -1)
        throw std::runtime_error("MmapFileAllocator::resize: munmap failed: " + mmapped_vector::get_error_message("munmap"));
    this->ptr = nullptr;  // prevent double-munmap if the re-map below fails
    this->capacity = 0;
    void* new_ptr = mmap(nullptr, new_capacity * sizeof(T), PROT_READ | PROT_WRITE, this->mmap_flags, this->file_descriptor, 0);
    if (new_ptr == MAP_FAILED) {
        throw std::runtime_error("MmapFileAllocator::resize: mmap failed: " + mmapped_vector::get_error_message("mmap"));
    }
#endif

    this->ptr = static_cast<T*>(new_ptr);
    this->capacity = new_capacity;
}

template <typename T>
void MmapFileAllocator<T>::sync(size_t used_elements) {
    this->backing_size = used_elements;
}

/*
 * =================================================================================================
 */


template <typename T>
class MallocAllocator : public Allocator<T>
{
public:
    MallocAllocator();
    MallocAllocator(const MallocAllocator&) = delete;   // Copy constructor is not allowed unless elided
    MallocAllocator(MallocAllocator&&) noexcept;
    MallocAllocator& operator=(MallocAllocator&&) noexcept;
    ~MallocAllocator() override;
    MallocAllocator& operator=(const MallocAllocator&) = delete;

    void resize(size_t new_size) override;

    friend class MmappedVector<T, MallocAllocator, false>;
    friend class MmappedVector<T, MallocAllocator, true>;
};

template <typename T>
MallocAllocator<T>::MallocAllocator() : Allocator<T>() {
    this->ptr = static_cast<T*>(malloc(16 * sizeof(T)));
    if (!this->ptr) {
        throw std::runtime_error("MallocAllocator: malloc failed");
    }
    this->capacity = 16;

}

template <typename T>
MallocAllocator<T>::MallocAllocator(MallocAllocator&& other) noexcept : Allocator<T>() {
    this->ptr = other.ptr;
    this->capacity = other.capacity;
    other.ptr = nullptr;
    other.capacity = 0;
}

template <typename T>
MallocAllocator<T>& MallocAllocator<T>::operator=(MallocAllocator&& other) noexcept {
    if (this != &other) {
        if (this->ptr) {
            free(this->ptr);
        }
        this->ptr = other.ptr;
        this->capacity = other.capacity;
        other.ptr = nullptr;
        other.capacity = 0;
    }
    return *this;
}

template <typename T>
MallocAllocator<T>::~MallocAllocator() {

    if (this->ptr) {
        free(this->ptr);
        this->ptr = nullptr;
        this->capacity = 0;
    }
}

template <typename T>
void MallocAllocator<T>::resize(size_t new_capacity) {
    if (new_capacity == this->capacity) return;

    // realloc(p, 0) may free p and return NULL; release explicitly instead.
    if (new_capacity == 0) {
        free(this->ptr);
        this->ptr = nullptr;
        this->capacity = 0;
        return;
    }

    void* new_ptr = realloc(this->ptr, new_capacity * sizeof(T));  // realloc(NULL, n) == malloc(n)
    if (!new_ptr) {
        throw std::runtime_error("MallocAllocator: realloc failed");
    }

    this->ptr = static_cast<T*>(new_ptr);
    this->capacity = new_capacity;
}

} // namespace mmapped_vector

#endif // MMAPPED_VECTOR_ALLOCATORS_H
