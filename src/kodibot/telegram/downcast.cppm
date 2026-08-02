module;

#include <type_traits>

#include <td/telegram/td_api.hpp>

export module kodibot.telegram:downcast;

export namespace kodibot::telegram {

template<typename To, typename From>
To downcast(From *from) {
    if (from->get_id() == std::remove_pointer_t<To>::ID) {
        return static_cast<To>(from);
    } else {
        return nullptr;
    }
}

template<typename Obj, typename F>
bool downcast_call(Obj &obj, const F &fn) {
    return td::td_api::downcast_call(obj, fn);
}

template<typename Obj, typename F>
bool downcast_call(const Obj &obj, const F &fn) {
    // duh...
    return td::td_api::downcast_call(
        const_cast<Obj &>(obj),
        [&](auto &cast_obj) {
            return fn(std::as_const(cast_obj));
        }
    );
}

} // namespace kodibot::telegram
