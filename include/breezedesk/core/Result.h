#pragma once

#include "breezedesk/core/Error.h"

#include <optional>
#include <utility>
#include <variant>

namespace BreezeDesk {

template <typename T> class Result final {
  public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(UserFacingError error) { return Result(std::move(error)); }

    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<T>(m_storage); }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] const T& value() const& { return std::get<T>(m_storage); }
    [[nodiscard]] T& value() & { return std::get<T>(m_storage); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(m_storage)); }
    [[nodiscard]] const UserFacingError& error() const { return std::get<UserFacingError>(m_storage); }

  private:
    explicit Result(T value) : m_storage(std::move(value)) {}
    explicit Result(UserFacingError error) : m_storage(std::move(error)) {}

    std::variant<T, UserFacingError> m_storage;
};

template <> class Result<void> final {
  public:
    static Result success() { return Result(); }
    static Result failure(UserFacingError error) { return Result(std::move(error)); }

    [[nodiscard]] bool hasValue() const noexcept { return !m_error.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }
    [[nodiscard]] const UserFacingError& error() const { return m_error.value(); }

  private:
    Result() = default;
    explicit Result(UserFacingError error) : m_error(std::move(error)) {}

    std::optional<UserFacingError> m_error;
};

} // namespace BreezeDesk
