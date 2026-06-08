module;

#include <boost/signals2.hpp>

export module kodibot.util:signal;

template<typename Sig>
struct make_extended_slot_function_signature;

template<typename R, typename ...Args>
struct make_extended_slot_function_signature<R (Args ...)>
{
    using type = R (const boost::signals2::connection &, Args ...);
};

template<typename Sig>
struct returns_void : std::false_type {};

template<typename ...Args>
struct returns_void<void (Args ...)> : std::true_type {};

export namespace kodibot::util {

template<typename Signature>
requires returns_void<Signature>::value
using signal = boost::signals2::signal<Signature>;

struct [[nodiscard]] scoped_connection : boost::signals2::scoped_connection {
    using boost::signals2::scoped_connection::scoped_connection;
};

} // namespace kodibot::util
