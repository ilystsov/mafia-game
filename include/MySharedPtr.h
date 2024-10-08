#ifndef MYSHAREDPTR_H
#define MYSHAREDPTR_H

#include <cassert>
#include <type_traits>  // подключим для std::enable_if std::is_convertible

template <typename T>
class MySharedPtr {
private:
    T* ptr;
    unsigned int* refCount;

    template <typename U>
    friend class MySharedPtr;

public:
    MySharedPtr() : ptr(nullptr), refCount(new unsigned int(0)) {}
    MySharedPtr(T* p) : ptr(p), refCount(new unsigned int(1)) {}
    MySharedPtr(const MySharedPtr& other) : ptr(other.ptr), refCount(other.refCount) {
        if (ptr) {
            (*refCount)++;
        }
    }

    template <typename U>
    MySharedPtr(const MySharedPtr<U>& other, typename std::enable_if<std::is_convertible<U*, T*>::value>::type* = nullptr)
        : ptr(other.ptr), refCount(other.refCount) {
        if (ptr) {
            (*refCount)++;
        }
    }

    ~MySharedPtr() {
        if (refCount) {
            (*refCount)--;
            if (*refCount == 0) {
                delete ptr;
                delete refCount;
            }
        }
    }

    MySharedPtr& operator=(const MySharedPtr& other) {
        if (this != &other) {
            if (refCount) {
                (*refCount)--;
                if (*refCount == 0) {
                    delete ptr;
                    delete refCount;
                }
            }
            ptr = other.ptr;
            refCount = other.refCount;
            if (ptr) {
                (*refCount)++;
            }
        }
        return *this;
    }

    template <typename U>
    MySharedPtr& operator=(const MySharedPtr<U>& other) {
        if (ptr != other.ptr) {
            if (refCount) {
                (*refCount)--;
                if (*refCount == 0) {
                    delete ptr;
                    delete refCount;
                }
            }
            ptr = other.ptr;
            refCount = other.refCount;
            if (ptr) {
                (*refCount)++;
            }
        }
        return *this;
    }

    MySharedPtr& operator=(MySharedPtr&& other) {
        if (this != &other) {
            if (refCount) {
                (*refCount)--;
                if (*refCount == 0) {
                    delete ptr;
                    delete refCount;
                }
            }
            ptr = other.ptr;
            refCount = other.refCount;
            other.ptr = nullptr;
            other.refCount = nullptr;
        }
        return *this;
    }

    T& operator*() const {
        assert(ptr != nullptr && "Dereferencing a nullptr!");
        return *ptr;
    }

    T* operator->() const {
        assert(ptr != nullptr && "Accessing member of a nullptr!");
        return ptr;
    }

    explicit operator bool() const {
        return ptr != nullptr;
    }

    void reset(T* p = nullptr) {
        if (ptr != p) {
            if (refCount) {
                (*refCount)--;
                if (*refCount == 0) {
                    delete ptr;
                    delete refCount;
                }
            }
            ptr = p;
            if (ptr) {
                refCount = new unsigned int(1);
            } else {
                refCount = new unsigned int(0);
            }
        }
    }

    void swap(MySharedPtr& other) {
        std::swap(ptr, other.ptr);
        std::swap(refCount, other.refCount);
    }

    T* get() const {
        return ptr;
    }

    bool operator==(const MySharedPtr& other) const {
        return ptr == other.ptr;
    }

    bool operator!=(const MySharedPtr& other) const {
        return ptr != other.ptr;
    }

    unsigned int use_count() const {
        return refCount ? *refCount : 0;
    }
};

#endif // MYSHAREDPTR_H
