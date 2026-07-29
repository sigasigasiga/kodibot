module;

#include <csignal>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <system_error>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

export module kodibot.os:signal_monitor;

import :eintr;
import :unique_fd;

export namespace kodibot::os {

class signal_monitor
{
public:
    signal_monitor(std::initializer_list<int> signals) {
        ::sigemptyset(&m_sigset);

        for (int sig : signals) {
            ::sigaddset(&m_sigset, sig);
        }

        if (int err = ::pthread_sigmask(SIG_BLOCK, &m_sigset, nullptr)) {
            throw std::system_error(err, std::generic_category(), "pthread_sigmask");
        }

        m_signal_fd.reset(::signalfd(-1, &m_sigset, SFD_CLOEXEC));
        if (!m_signal_fd) {
            throw std::system_error(errno, std::generic_category(), "signalfd");
        }

        m_stop_fd.reset(::eventfd(0, EFD_CLOEXEC));
        if (!m_stop_fd) {
            throw std::system_error(errno, std::generic_category(), "eventfd");
        }
    }

    std::optional<int> wait() {
        ::pollfd fds[2] = {
            {m_signal_fd.get(), POLLIN, 0},
            {m_stop_fd.get(), POLLIN, 0},
        };

        int ret = handle_eintr(::poll, fds, 2, -1);
        if (ret == -1) {
            throw std::system_error(errno, std::generic_category(), "poll");
        }

        if (fds[1].revents & POLLIN) {
            std::uint64_t val;

            ::ssize_t res = handle_eintr(::read, m_stop_fd.get(), &val, sizeof(val));
            if (res == -1) {
                throw std::system_error(errno, std::generic_category(), "read(eventfd)");
            } else {
                return std::nullopt;
            }
        }

        if (fds[0].revents & POLLIN) {
            ::signalfd_siginfo info;
            ::ssize_t n = handle_eintr(::read, m_signal_fd.get(), &info, sizeof(info));
            if (n != sizeof(info)) {
                throw std::runtime_error("short read on signalfd");
            }
            return static_cast<int>(info.ssi_signo);
        }

        throw std::runtime_error("poll returned but no fd is ready");
    }

    void stop() {
        std::uint64_t one = 1;
        ::ssize_t n = handle_eintr(::write, m_stop_fd.get(), &one, sizeof(one));

        if (n != sizeof(one)) {
            throw std::system_error(errno, std::generic_category(), "write(eventfd)");
        }
    }

private:
    ::sigset_t m_sigset;
    unique_fd m_signal_fd;
    unique_fd m_stop_fd;
};

} // namespace kodibot::os
