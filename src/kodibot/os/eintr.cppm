module;

#include <cerrno>
#include <functional>

export module kodibot.os:eintr;

export namespace kodibot::os {

template<
    typename F,
    typename ...Args,
    typename Ret = std::invoke_result_t<F, Args &&...>>
Ret handle_eintr(F &&f, Args &&...args) {
    Ret ret;

    do {
        ret = std::invoke(f, args...); // NB: cannot `forward` in a loop
    } while(ret == -1 && errno == EINTR);

    return ret;
}

} // namespace kodibot::os
