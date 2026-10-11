/*
BStone: A Source port of
Blake Stone: Aliens of Gold and Blake Stone: Planet Strike

Copyright (c) 1992-2013 Apogee Entertainment, LLC
Copyright (c) 2013-2015 Boris I. Bendovsky (bibendovsky@hotmail.com)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the
Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/


#ifndef BSTONE_STRING_HELPER_INCLUDED
#define BSTONE_STRING_HELPER_INCLUDED

#ifdef BS32X
// BlakeStone32X: no iostreams/locale on the 32X; same interface, plain C conversions
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>
#include <type_traits>

namespace bstone {

using StringList = std::vector<std::string>;

class StringHelper {
public:
    StringHelper() = delete;

    static char to_lower(
        char value);

    static std::string to_lower(
        const std::string& value);

    template<typename T, typename U>
    static T lexical_cast(
        const U& src_value)
    {
        static_assert(std::is_same<T, std::string>::value, "BS32X lexical_cast: only to std::string");
        return std::to_string(src_value);
    }

    template<typename T, typename U>
    static bool lexical_cast(
        const T& src_value,
        U& dst_value)
    {
        return lc_assign(src_value, dst_value);
    }

private:
    template<typename T>
    static bool lc_assign(const T& src_value, std::string& dst_value)
    {
        dst_value = std::to_string(src_value);
        return true;
    }

    template<typename U>
    static bool lc_assign(const std::string& src_value, U& dst_value)
    {
        static_assert(std::is_integral<U>::value, "BS32X lexical_cast: only to integers");
        const char* text = src_value.c_str();
        char* end = nullptr;
        long value = std::strtol(text, &end, 10);
        if (end == text || *end != '\0') {
            return false;
        }
        dst_value = static_cast<U>(value);
        return true;
    }

public:
    static bool is_iequal(
        const std::string& a,
        const std::string& b);

    static const std::string& get_empty();
}; // StringHelper

} // bstone

#else // BS32X



#include "bs32x.h"
#include <locale>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>


namespace bstone {


using StringList = std::vector<std::string>;


class StringHelper {
public:
    StringHelper() = delete;

    StringHelper(
        const StringHelper& that) = delete;

    StringHelper& operator=(
        const StringHelper& that) = delete;

    ~StringHelper() = delete;


    static char to_lower(
        char value);

    static std::string to_lower(
        const std::string& value);

    template<typename T, typename U>
    static T lexical_cast(
        const U& src_value)
    {
        std::stringstream oss;
        oss << src_value;

        T result;
        oss >> result;

        if (oss) {
            return result;
        }

        bs32x_fatal("lexical_cast");
    }

    template<typename T, typename U>
    static bool lexical_cast(
        const T& src_value,
        U& dst_value)
    {
        std::stringstream oss;
        oss.unsetf(std::ios_base::skipws);
        oss << src_value;
        oss >> dst_value;
        return !oss.fail();
    }

    static bool is_iequal(
        const std::string& a,
        const std::string& b);

    static bool is(
        std::ctype_base::mask mask,
        char value);

    static const std::string& get_empty();
}; // StringHelper


} // bstone


#endif // BS32X
#endif // BSTONE_STRING_HELPER_INCLUDED
