#pragma once

#include <type_traits>

template <class Comp>
struct CompTag
{
    static int id;
    inline static int id2 = 0;
};

class TypeIdGenerator
{
public:
    template <class U>
    static const int getNewID()
    {
        static const int idCounter = m_count++;
        return idCounter;
    }
private:
    inline static int m_count = 0;
};

#define REGISTER(Comp) \
    template <>        \
    int CompTag<Comp>::id = TypeIdGenerator::getNewID<Comp>();

//! https://isocpp.org/files/papers/N4860.pdf tells:
//! 6.8 Types: paragraph 2
//! For any object (other than a potentially-overlapping subobject) of trivially copyable type T, whether or not
//! the object holds a valid value of type T, the underlying bytes (6.7.1) making up the object can be copied into
//! an array of char, unsigned char, or std::byte (17.2.1).36 If the content of that array is copied back into
//! the object, the object shall subsequently hold its original value.

template <typename T>
concept Component = true;
    // std::is_base_of_v<CompTag<T>, T> &&
    // requires(T t) { T::id; };
// std::is_trivially_copyable_v<T>; //! no more needed :)
