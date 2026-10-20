#pragma once

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace coral::foundation {

// 非持有的连续视图。调用方负责底层存储的存活。
// at() 与 subview() 检查边界；operator[] 遵循容器惯例。
template <typename T>
class SliceView {
public:
    using value_type = std::remove_cv_t<T>;
    static constexpr std::size_t kRemaining = static_cast<std::size_t>(-1);

    constexpr SliceView() noexcept = default;
    constexpr SliceView(T* storage, std::size_t length)
        : storage_(storage), length_(length)
    {
        if (!storage_ && length_ != 0) {
            throw std::invalid_argument("nonempty view requires storage");
        }
    }

    template <typename U, std::size_t N,
              std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>, int> = 0>
    constexpr SliceView(U (&storage)[N]) noexcept
        : storage_(storage), length_(N) {}

    // 数组可转换性同样拒绝不安全的派生到基类数组视图。
    template <typename Container,
              typename Pointer = decltype(std::data(std::declval<Container&>())),
              std::enable_if_t<std::is_pointer_v<Pointer> &&
                  std::is_convertible_v<std::remove_pointer_t<Pointer> (*)[], T (*)[]>, int> = 0>
    explicit constexpr SliceView(Container& storage)
        : SliceView(std::data(storage), std::size(storage)) {}

    template <typename U,
              std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>, int> = 0>
    constexpr SliceView(const SliceView<U>& other) noexcept
        : storage_(other.data()), length_(other.size()) {}

    constexpr T* data() const noexcept { return storage_; }
    constexpr std::size_t size() const noexcept { return length_; }
    constexpr bool empty() const noexcept { return length_ == 0; }
    constexpr T* begin() const noexcept { return storage_; }
    constexpr T* end() const noexcept { return length_ == 0 ? storage_ : storage_ + length_; }
    constexpr T& operator[](std::size_t position) const noexcept { return storage_[position]; }

    constexpr T& at(std::size_t position) const
    {
        if (position >= length_) {
            throw std::out_of_range("view index out of range");
        }
        return storage_[position];
    }

    constexpr SliceView subview(std::size_t offset, std::size_t count = kRemaining) const
    {
        if (offset > length_) {
            throw std::out_of_range("view offset out of range");
        }
        const auto remaining = length_ - offset;
        if (count == kRemaining) {
            count = remaining;
        }
        if (count > remaining) {
            throw std::out_of_range("view length out of range");
        }
        return {offset == 0 ? storage_ : storage_ + offset, count};
    }

private:
    T* storage_ = nullptr;
    std::size_t length_ = 0;
};

} // 命名空间 coral::foundation 结束
