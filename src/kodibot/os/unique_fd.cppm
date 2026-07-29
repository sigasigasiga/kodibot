module;

#include <utility>

#include <unistd.h>

export module kodibot.os:unique_fd;

export namespace kodibot::os {

// TODO: use `boost::scope::unique_fd` when boost 1.85 is in alpine
class unique_fd
{
public:
    unique_fd() : m_fd(-1) {}
    explicit unique_fd(int fd) : m_fd(fd) {}
    ~unique_fd() { reset(); }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

    unique_fd(unique_fd&& other) noexcept
        : m_fd(std::exchange(other.m_fd, -1))
    {
    }

    unique_fd& operator=(unique_fd&& other) noexcept {
        if (this != &other) {
            reset();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }

    int get() const { return m_fd; }

    void reset(int fd = -1) {
        if (m_fd != -1 && m_fd != fd) {
            ::close(m_fd);
        }
        m_fd = fd;
    }

    int release() { return std::exchange(m_fd, -1); }

    explicit operator bool() const { return m_fd != -1; }

    friend bool operator==(const unique_fd& lhs, const unique_fd& rhs) = default;
    friend auto operator<=>(const unique_fd& lhs, const unique_fd& rhs) = default;

private:
    int m_fd;
};

} // namespace kodibot::os
