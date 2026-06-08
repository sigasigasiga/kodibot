export module kodibot.util:functional;

export namespace kodibot::util {

template <class... Fs>
struct overload : Fs... {
    using Fs::operator()...;
};

template <class... Fs>
overload(Fs...) -> overload<Fs...>;

} // namespace kodibot::util
