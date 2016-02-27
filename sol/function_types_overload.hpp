// The MIT License (MIT)

// Copyright (c) 2013-2016 Rapptz, ThePhD and contributors

// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SOL_FUNCTION_TYPES_OVERLOAD_HPP
#define SOL_FUNCTION_TYPES_OVERLOAD_HPP

#include "overload.hpp"
#include "function_types_core.hpp"
#include "function_types_usertype.hpp"

namespace sol {
namespace function_detail {
namespace internals {
template <typename T>
struct overload_traits : meta::function_traits<T> {};

template <typename T, typename Func, typename X>
struct overload_traits<functor<T, Func, X>> {
    typedef typename functor<T, Func, X>::args_type args_type;
    typedef typename functor<T, Func, X>::return_type return_type;
    static const std::size_t arity = functor<T, Func, X>::arity;
};

template <std::size_t... M, typename Match, typename... Args>
inline int overload_match_arity(types<>, std::index_sequence<>, std::index_sequence<M...>, Match&&, lua_State* L, int, int, Args&&...) {
    return luaL_error(L, "sol: no matching function call takes this number of arguments and the specified types");
}

template <typename Fx, typename... Fxs, std::size_t I, std::size_t... In, std::size_t... M, typename Match, typename... Args>
inline int overload_match_arity(types<Fx, Fxs...>, std::index_sequence<I, In...>, std::index_sequence<M...>, Match&& matchfx, lua_State* L, int fxarity, int start, Args&&... args) {
    typedef overload_traits<meta::Unqualified<Fx>> traits;
    typedef meta::tuple_types<typename traits::return_type> return_types;
    typedef typename traits::args_type args_type;
    typedef typename args_type::indices args_indices;
    // compile-time eliminate any functions that we know ahead of time are of improper arity
    if (meta::find_in_pack_v<Index<traits::arity>, Index<M>...>::value) {
        return overload_match_arity(types<Fxs...>(), std::index_sequence<In...>(), std::index_sequence<M...>(), std::forward<Match>(matchfx), L, fxarity, start, std::forward<Args>(args)...);
    }
    if (traits::arity != fxarity) {
        return overload_match_arity(types<Fxs...>(), std::index_sequence<In...>(), std::index_sequence<traits::arity, M...>(), std::forward<Match>(matchfx), L, fxarity, start, std::forward<Args>(args)...);
    }
    if (sizeof...(Fxs) != 0 && !function_detail::check_types(args_type(), args_indices(), L, start)) {
        return overload_match_arity(types<Fxs...>(), std::index_sequence<In...>(), std::index_sequence<M...>(), std::forward<Match>(matchfx), L, fxarity, start, std::forward<Args>(args)...);
    }
    return matchfx(meta::Bool<sizeof...(Fxs) != 0>(), types<Fx>(), Index<I>(), return_types(), args_type(), L, fxarity, start, std::forward<Args>(args)...);
}
} // internals

template <typename... Functions, typename Match, typename... Args>
inline int overload_match_arity(Match&& matchfx, lua_State* L, int fxarity, int start, Args&&... args) {
    return internals::overload_match_arity(types<Functions...>(), std::index_sequence_for<Functions...>(), std::index_sequence<>(), std::forward<Match>(matchfx), L, fxarity,  start, std::forward<Args>(args)...);
}

template <typename... Functions, typename Match, typename... Args>
inline int overload_match(Match&& matchfx, lua_State* L, int start, Args&&... args) {
     int fxarity = lua_gettop(L) - (start - 1);
     return overload_match_arity<Functions...>(std::forward<Match>(matchfx), L, fxarity, start, std::forward<Args>(args)...);
}

template <typename... Functions>
struct overloaded_function : base_function {
    typedef std::tuple<Functions...> overload_list;
    typedef std::index_sequence_for<Functions...> indices;
    overload_list overloads;

    overloaded_function(overload_set<Functions...> set)
    : overloaded_function(indices(), set) {}

    template <std::size_t... I>
    overloaded_function(std::index_sequence<I...>, overload_set<Functions...> set)
    : overloaded_function(std::get<I>(set)...) {}

    overloaded_function(Functions... fxs)
    : overloads(fxs...) {

    }

    template <bool b, typename Fx, std::size_t I, typename... R, typename... Args>
    int call(meta::Bool<b>, types<Fx>, Index<I>, types<R...> r, types<Args...> a, lua_State* L, int, int start) {
        auto& func = std::get<I>(overloads);
        return stack::call_into_lua<b ? false : stack::stack_detail::default_check_arguments>(r, a, func, L, start);
    }

    virtual int operator()(lua_State* L) override {
        auto mfx = [&](auto&&... args){ return this->call(std::forward<decltype(args)>(args)...); };
        return overload_match<Functions...>(mfx, L, 1);
    }
};

template <typename T, typename... Functions>
struct usertype_overloaded_function : base_function {
    typedef std::tuple<functor<T, std::remove_pointer_t<std::decay_t<Functions>>>...> overload_list;
    typedef std::index_sequence_for<Functions...> indices;
    overload_list overloads;
    
    usertype_overloaded_function(std::tuple<Functions...> set) : overloads(std::move(set)) {}

    template <bool b,typename Fx, std::size_t I, typename... R, typename... Args>
    int call(meta::Bool<b>, types<Fx>, Index<I>, types<R...> r, types<Args...> a, lua_State* L, int, int start) {
        auto& func = std::get<I>(overloads);
        func.item = detail::ptr(stack::get<T>(L, 1));
        return stack::call_into_lua<b ? false : stack::stack_detail::default_check_arguments>(r, a, func, L, start);
    }

    virtual int operator()(lua_State* L) override {
        auto mfx = [&](auto&&... args){ return this->call(std::forward<decltype(args)>(args)...); };
        return overload_match<functor<T, std::remove_pointer_t<std::decay_t<Functions>>>...>(mfx, L, 2);
    }
};
} // function_detail
} // sol

#endif // SOL_FUNCTION_TYPES_OVERLOAD_HPP
