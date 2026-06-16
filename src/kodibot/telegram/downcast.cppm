module;

#include <type_traits>

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

} // namespace kodibot::telegram
