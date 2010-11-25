// Copyright 2005 Ben Hutchings <ben@decadent.org.uk>.
// See the file "COPYING" for licence details.

#ifndef INC_AUTO_HANDLE_HPP
#define INC_AUTO_HANDLE_HPP

// Like auto_ptr, but for arbitrary "handle" types.
// The parameters are:
// - handle_type: the type of the raw handle to be wrapped
// - closer_type: a function object type whose operator() takes a raw handle
//                and closes it (or does nothing if it is a null handle)
// - factory_type: a function object type whose operator() returns a null
//                 handle

template<typename handle_type, typename closer_type, typename factory_type>
class auto_handle_ref;

template<typename handle_type, typename closer_type, typename factory_type>
class auto_handle
    // Use inheritance so we can benefit from the empty base optimisation
    : private closer_type, private factory_type
{
    typedef auto_handle_ref<handle_type, closer_type, factory_type> ref_type;
public:
    auto_handle()
	    : handle_(factory_type::operator()())
	{}
    explicit auto_handle(handle_type handle)
	    : handle_(handle)
	{}
    auto_handle(auto_handle & other)
	    : handle_(other.release())
	{}
    auto_handle(ref_type other)
	    : handle_(other.release())
	{}
    auto_handle & operator=(auto_handle & other)
	{
	    reset(other.release());
	    return *this;
	}
    ~auto_handle()
	{
	    reset();
	}
    handle_type get() const
	{
	    return handle_;
	}
    handle_type release()
	{
	    handle_type handle(handle_);
	    handle_ = factory_type::operator()();
	    return handle;
	}
    void reset()
	{
	    closer_type::operator()(handle_);
	    handle_ = factory_type::operator()();
	}
    void reset(handle_type handle)
	{
	    closer_type::operator()(handle_);
	    handle_ = handle;
	}
    operator ref_type()
	{
	    return ref_type(*this);
	}
private:
    handle_type handle_;
};

template<typename handle_type, typename closer_type, typename factory_type>
class auto_handle_ref
{
    typedef auto_handle<handle_type, closer_type, factory_type> target_type;
public:
    explicit auto_handle_ref(target_type & target)
	    : target_(target)
	{}
    handle_type release()
	{
	    return target_.release();
	}
private:
    target_type & target_;
};

#endif // !defined(INC_AUTO_HANDLE_HPP)
