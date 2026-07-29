module;

#include <cerrno>
#include <functional>
#include <utility>

export module kodibot.os:eintr;

export namespace kodibot::os {

template<typename F, typename... Args>
int handle_eintr(F&& f, Args&&... args) {
    int ret;

    do {
        ret = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    } while(ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

} // namespace kodibot::os
