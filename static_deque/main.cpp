
// MIT License
//
// Copyright (c) 2020 degski
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <array>
#include <memory>
#include <sax/iostream.hpp>
#include <random>
#include <string>
#include <type_traits>
#include <variant>

#include <sax/splitmix.hpp>
#include <sax/uniform_int_distribution.hpp>

#include <static_deque.hpp>

// -fsanitize=address
/*
C:\Program Files\LLVM\lib\clang\10.0.0\lib\windows\clang_rt.asan_cxx-x86_64.lib;
C:\Program Files\LLVM\lib\clang\10.0.0\lib\windows\clang_rt.asan-preinit-x86_64.lib;
C:\Program Files\LLVM\lib\clang\10.0.0\lib\windows\clang_rt.asan-x86_64.lib
*/

void handleEptr ( std::exception_ptr eptr ) { // Passing by value is ok.
    try {
        if ( eptr )
            std::rethrow_exception ( eptr );
    }
    catch ( std::exception const & e ) {
        std::cout << "Caught exception \"" << e.what ( ) << "\"\n";
    }
}

int counter = 0;

template<typename T>
using unique_weak_ptr = std::variant<std::unique_ptr<T>, T *>;

template<typename... Ts>
struct overloaded : Ts... {
    using Ts::operator( )...;
};
template<typename... Ts>
overloaded ( Ts... ) -> overloaded<Ts...>;

template<typename Type>
struct wobble_ptr {
    public:
    using value_type    = Type;
    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    explicit wobble_ptr ( ) noexcept {}
    wobble_ptr ( pointer p_, bool is_unique_ = true ) noexcept : m_raw ( is_unique_ ? p_ : p_ | one_mask ) {}

    wobble_ptr ( wobble_ptr const & ) = delete;
    wobble_ptr ( wobble_ptr && rhs_ ) noexcept : m_raw ( std::exchange ( rhs_.m_raw, nullptr ) ) {}

    ~wobble_ptr ( ) noexcept { destruct ( ); }

    wobble_ptr & operator= ( wobble_ptr const & ) noexcept = delete;
    wobble_ptr & operator                                  = ( wobble_ptr && rhs_ ) noexcept {
        destruct ( );
        m_raw = std::exchange ( rhs_.m_raw, nullptr );
    }

    [[nodiscard]] const_pointer get ( ) const noexcept {
        return reinterpret_cast<const_pointer> ( reinterpret_cast<std::uintptr_t> ( m_raw ) & ptr_mask );
    }
    [[nodiscard]] pointer get ( ) noexcept { return const_cast<pointer> ( std::as_const ( *this ).get ( ) ); }

    [[nodiscard]] const_pointer operator-> ( ) const noexcept { return get ( ); }
    [[nodiscard]] pointer operator-> ( ) noexcept { return const_cast<pointer> ( std::as_const ( *this ).operator-> ( ) ); }

    void make_weak ( ) noexcept { m_raw |= one_mask; }
    void make_unique ( ) noexcept { m_raw &= ptr_mask; }

    [[nodiscard]] bool is_weak ( ) const noexcept { return reinterpret_cast<std::uintptr_t> ( m_raw ) & one_mask; }
    [[nodiscard]] bool is_unique ( ) const noexcept { return not is_weak ( ); }

    private:
    static constexpr std::uintptr_t ptr_mask = 0x00FF'FFFF'FFFF'FFF0;
    static constexpr std::uintptr_t one_mask = 0x0000'0000'0000'0001;

    pointer m_raw = nullptr;

    void destruct ( ) noexcept {
        if ( is_unique ( ) )
            delete m_raw;
    }
};
template<std::size_t N, std::size_t alignment = alignof ( std::max_align_t )>
struct chunk {
    using chunk_uptr = std::unique_ptr<chunk>;
    static_assert ( N > 16, "chunk: N is too small" );
    static constexpr std::size_t char_size = N - 3 * sizeof ( char * );
    alignas ( alignment ) char m_buf[ char_size ];
    char * m_ptr = nullptr;
    unique_weak_ptr<chunk> m_next;
    int c = counter++;

    constexpr chunk ( ) noexcept : m_next ( std::in_place_type_t<chunk *> ( ), nullptr ) {
        std::cout << "c'tor " << c << " called" << nl;
    };

    ~chunk ( ) noexcept { std::cout << "d'tor " << c << " called" << nl; }

    static constexpr std::size_t capacity ( ) noexcept { return char_size; };
};

template<typename Type, typename SizeType, std::size_t ChunkSize = 512u>
class mempool {

    static_assert ( is_power_2 ( ChunkSize ), "Template parameter 3 must be an integral value with a value a power of 2" );

    public:
    using value_type    = Type;
    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using size_type       = SizeType;
    using difference_type = std::make_signed<size_type>;

    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = pointer;
    using const_reverse_iterator = const_pointer;

    using void_ptr = void *;
    using char_ptr = char *;

    using chunk      = chunk<ChunkSize, alignof ( value_type )>;
    using chunk_ptr  = chunk *;
    using chunk_uptr = std::unique_ptr<chunk>;
    using chunk_sptr = unique_weak_ptr<chunk>; // smarter.

    static constexpr size_type chunck_size = static_cast<size_type> ( chunk::capacity ( ) / sizeof ( value_type ) );

    chunk_sptr m_data;
    chunk_ptr m_last_data = nullptr;

    chunk_ptr last ( ) const noexcept {
        chunk_ptr ptr = as_raw ( m_data );
        if ( ptr )
            while ( is_chunk_uptr ( ptr->m_next ) )
                ptr = as_raw<chunk_uptr> ( ptr->m_next );
        return ptr;
    }

    template<typename T = void>
    [[nodiscard]] constexpr chunk_ptr as_raw ( chunk_sptr const & sp_ ) const noexcept {
        if constexpr ( std::is_same<chunk_uptr, T>::value ) {
            return std::get<T> ( sp_ ).get ( );
        }
        else if constexpr ( std::is_same<chunk_ptr, T>::value ) {
            return std::get<T> ( sp_ );
        }
        else {
            chunk_ptr v = nullptr;
            std::visit (
                overloaded{ [ &v ] ( chunk_uptr const & arg ) { v = arg.get ( ); }, [ &v ] ( chunk_ptr const & arg ) { v = arg; } },
                sp_ );
            assert ( v );
            return v;
        }
    }

    [[nodiscard]] constexpr bool is_chunk_ptr ( unique_weak_ptr<chunk> const & sp_ ) const noexcept {
        return std::holds_alternative<chunk_ptr> ( sp_ );
    }
    [[nodiscard]] constexpr bool is_chunk_uptr ( unique_weak_ptr<chunk> const & sp_ ) const noexcept {
        return std::holds_alternative<chunk_uptr> ( sp_ );
    }

    void grow ( ) noexcept {
        if ( m_last_data ) {
            m_last_data->m_next = std::make_unique<chunk> ( ); // Constuct new chunk.
            m_last_data         = as_raw<chunk_uptr> ( m_last_data->m_next );
            m_last_data->m_next = as_raw<chunk_uptr> ( m_data ); // Set next to raw-pointer data, which is begin.
        }
        else {
            m_data              = std::make_unique<chunk> ( );
            m_last_data         = as_raw<chunk_uptr> ( m_data );
            m_last_data->m_next = m_last_data; // Circular list.
        }
    }

    pointer m_front = nullptr, m_back = nullptr;
};

int main ( ) {

    mempool<int, std::size_t, 64> pool;

    std::exception_ptr eptr;

    try {

        { pool.grow ( ); }

        std::cout << "leaving scope 1" << nl;

        { pool.grow ( ); }

        std::cout << "leaving scope 2" << nl;

        { pool.grow ( ); }

        std::cout << "leaving scope 3" << nl;
    }
    catch ( ... ) {
        eptr = std::current_exception ( ); // Capture.
    }
    handleEptr ( eptr );

    std::cout << "leaving scope 4" << nl;

    return EXIT_SUCCESS;
}
