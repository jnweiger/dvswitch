// Copyright 2007 Ben Hutchings.
// See the file "COPYING" for licence details.

// Class template for fixed-size ring buffers

#ifndef DVSWITCH_RING_BUFFER_HPP
#define DVSWITCH_RING_BUFFER_HPP

#include <cassert>
#include <new>

#include <tr1/type_traits>

template<typename T, std::size_t N>
class ring_buffer
{
public:
    ring_buffer()
	: front_(0), back_(0)
    {}
    ring_buffer(const ring_buffer &);
    ~ring_buffer();
    ring_buffer & operator=(const ring_buffer &);

    std::size_t capacity() const { return N; }
    std::size_t size() const { return back_ - front_; }
    bool empty() const { return front_ == back_; }
    bool full() const { return back_ - front_ == N; }

    // Reader functions
    void pop();
    const T & front() const;

    // Writer functions
    void push(const T &);
    const T & back() const;

private:
    std::size_t front_, back_;
    typename std::tr1::aligned_storage<sizeof(T) * N,
				       std::tr1::alignment_of<T>::value>::type
    buffer_;
};

template<typename T, std::size_t N>
ring_buffer<T, N>::ring_buffer(const ring_buffer & other)
    : front_(0), back_(0)
{
    for (std::size_t i = other.front_; i != other.back_; ++i)
	push(reinterpret_cast<const T *>(&other.buffer_)[i % N]);
}

template<typename T, std::size_t N>
ring_buffer<T, N>::~ring_buffer()
{
    while (!empty())
	pop();
}

template<typename T, std::size_t N>
ring_buffer<T, N> & ring_buffer<T, N>::operator=(const ring_buffer & other)
{
    while (!empty())
	pop();

    for (std::size_t i = other.front_; i != other.back_; ++i)
	push(reinterpret_cast<const T *>(&other.buffer_)[i % N]);

    return *this;
}

template<typename T, std::size_t N>
void ring_buffer<T, N>::pop()
{
    assert(!empty());
    reinterpret_cast<T *>(&buffer_)[front_ % N].~T();
    ++front_;
}

template<typename T, std::size_t N>
const T & ring_buffer<T, N>::front() const
{
    assert(!empty());
    return reinterpret_cast<const T *>(&buffer_)[front_ % N];
}

template<typename T, std::size_t N>
void ring_buffer<T, N>::push(const T & value)
{
    assert(!full());
    new (reinterpret_cast<T *>(&buffer_) + back_ % N) T(value);
    ++back_;
}

template<typename T, std::size_t N>
const T & ring_buffer<T, N>::back() const
{
    assert(!empty());
    return reinterpret_cast<const T *>(&buffer_)[(back_ - 1) % N];
}

#endif // !defined(DVSWITCH_RING_BUFFER_HPP)
