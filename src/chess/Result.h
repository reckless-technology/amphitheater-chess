#pragma once
/// @file Result.h A value or the reason there is none.
///
/// What every parser and every fallible operation in the libraries returns: the value on success, a message in plain
/// words otherwise. C++20 has no std::expected, and the messages are all a caller ever shows, so this is the whole of
/// what is needed.

#include <optional>
#include <string>
#include <utility>

namespace amphitheater
{

/** The reason an operation failed, in words a person can read. */
struct Error
{
    std::string message; ///< What went wrong, without a trailing full stop.
};

/**
 * A value of type T, or an Error.
 * @tparam T The value's type.
 */
template <typename T> class Result
{
  public:
    /**
     * A successful result.
     * @param value The value.
     */
    Result(T value) : value_(std::move(value))
    {
    }

    /**
     * A failed result.
     * @param error Why.
     */
    Result(Error error) : error_(std::move(error.message))
    {
    }

    /** @return Whether the result holds a value. */
    bool HasValue() const
    {
        return value_.has_value();
    }

    /** @return Whether the result holds a value. */
    explicit operator bool() const
    {
        return value_.has_value();
    }

    /** @return The value; the result must hold one. */
    T &Value() &
    {
        return *value_;
    }

    /** @return The value; the result must hold one. */
    const T &Value() const &
    {
        return *value_;
    }

    /** @return The value, moved out; the result must hold one. */
    T &&Value() &&
    {
        return std::move(*value_);
    }

    /** @return The value; the result must hold one. */
    T &operator*() &
    {
        return *value_;
    }

    /** @return The value; the result must hold one. */
    const T &operator*() const &
    {
        return *value_;
    }

    /** @return The value's members; the result must hold one. */
    T *operator->()
    {
        return &*value_;
    }

    /** @return The value's members; the result must hold one. */
    const T *operator->() const
    {
        return &*value_;
    }

    /** @return Why the operation failed; empty when it did not. */
    const std::string &ErrorMessage() const
    {
        return error_;
    }

  private:
    std::optional<T> value_; ///< The value, when there is one.
    std::string      error_; ///< The reason, when there is none.
};

/** The value of an operation that returns nothing but may fail. */
struct Ok
{
};

/** The result of an operation that returns nothing but may fail: Ok, or an Error. */
using Status = Result<Ok>;

} // namespace amphitheater
