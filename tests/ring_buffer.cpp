#ifdef NDEBUG
#error "This is a test program and requires assertions to be enabled."
#endif

#include "ring_buffer.hpp"

int main()
{
    ring_buffer<int> buf(2);
    assert(buf.size() == 0);
    assert(buf.empty());
    buf.push(1);
    assert(buf.front() == 1);
    assert(buf.back() == 1);
    assert(buf.size() == 1);
    assert(!buf.empty() && !buf.full());
    buf.push(2);
    assert(buf.front() == 1);
    assert(buf.back() == 2);
    assert(buf.size() == 2);
    assert(!buf.empty() && buf.full());
    buf.pop();
    assert(buf.front() == 2);
    assert(buf.back() == 2);
    assert(buf.size() == 1);
    assert(!buf.empty() && !buf.full());
    ring_buffer<int> buf2(buf);
    assert(buf2.front() == 2);
    assert(buf2.back() == 2);
    assert(buf2.size() == 1);
    assert(!buf2.empty() && !buf2.full());
    buf.push(3);
    assert(buf.front() == 2);
    assert(buf.back() == 3);
    assert(buf.size() == 2);
    assert(!buf.empty() && buf.full());
    assert(buf2.front() == 2);
    assert(buf2.back() == 2);
    assert(buf2.size() == 1);
    assert(!buf2.empty() && !buf2.full());
    buf.pop();
    assert(buf.size() == 1);
    buf.pop();
    assert(buf.size() == 0);
    assert(buf.empty());
    buf = buf2;
    assert(buf.front() == 2);
    assert(buf.back() == 2);
    assert(buf.size() == 1);
    assert(!buf.empty() && !buf.full());
    ring_buffer<int> buf3(0);
    swap(buf2, buf3);
    assert(buf2.empty());
    assert(buf3.front() == 2);
    assert(buf3.back() == 2);
    assert(buf3.size() == 1);
    assert(!buf3.empty() && !buf3.full());
}
