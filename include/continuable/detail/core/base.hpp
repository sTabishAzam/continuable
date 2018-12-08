
/*

                        /~` _  _ _|_. _     _ |_ | _
                        \_,(_)| | | || ||_|(_||_)|(/_

                    https://github.com/Naios/continuable
                                   v3.0.0

  Copyright(c) 2015 - 2018 Denis Blank <denis.blank at outlook dot com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files(the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions :

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
**/

#ifndef CONTINUABLE_DETAIL_BASE_HPP_INCLUDED
#define CONTINUABLE_DETAIL_BASE_HPP_INCLUDED

#include <tuple>
#include <type_traits>
#include <utility>
#include <continuable/continuable-primitives.hpp>
#include <continuable/continuable-result.hpp>
#include <continuable/detail/core/annotation.hpp>
#include <continuable/detail/core/types.hpp>
#include <continuable/detail/features.hpp>
#include <continuable/detail/utility/result-trait.hpp>
#include <continuable/detail/utility/traits.hpp>
#include <continuable/detail/utility/util.hpp>

#if defined(CONTINUABLE_HAS_EXCEPTIONS)
#include <exception>
#endif // CONTINUABLE_HAS_EXCEPTIONS

namespace cti {
namespace detail {
/// The namespace `base` provides the low level API for working
/// with continuable types.
///
/// Important methods are:
/// - Creating a continuation from a callback taking functional
///   base::attorney::create_from(auto&& callback)
///     -> base::continuation<auto>
/// - Chaining a continuation together with a callback
///   base::chain_continuation(base::continuation<auto> continuation,
///                            auto&& callback)
///     -> base::continuation<auto>
/// - Finally invoking the continuation chain
///    base::finalize_continuation(base::continuation<auto> continuation)
///     -> void
namespace base {
template <typename T>
struct is_continuable : std::false_type {};
template <typename Data, typename Annotation>
struct is_continuable<continuable_base<Data, Annotation>> : std::true_type {};

template <typename... Args>
struct ready_continuable {
  std::tuple<Args...> values_;

  ready_continuable(ready_continuable&&) = default;
  ready_continuable(ready_continuable const&) = default;
  ready_continuable& operator=(ready_continuable&&) = default;
  ready_continuable& operator=(ready_continuable const&) = default;

  explicit ready_continuable(Args... values) : values_(std::move(values)...) {
  }

  template <typename Callback>
  void operator()(Callback&& callback) {
    traits::unpack(std::forward<Callback>(callback), std::move(values_));
  }

  /*bool operator()(is_ready_arg_t) const noexcept {
    return true;
  }

  std::tuple<Args...> operator()(get_arg_t) && {
    return std::move(values_);
  }*/
};
template <>
struct ready_continuable<> {
  template <typename Callback>
  void operator()(Callback&& callback) {
    util::invoke(std::forward<Callback>(callback));
  }

  /*bool operator()(is_ready_arg_t) const noexcept {
  return true;
  }

  void operator()(get_arg_t) && {
  }*/
};

/*template <typename Continuation, typename Args>
struct proxy_continuable : Continuation {
  using Continuation::Continuation;

  using Continuation::operator();

  bool operator()(is_ready_arg_t) const noexcept {
    return false;
  }

  std::tuple<T...> operator()(get_arg_t) && {
    util::unreachable();
  }
};*/

struct attorney {
  /// Creates a continuable_base from the given continuation, annotation
  /// and ownership.
  template <
      typename T, typename A,
      typename Continuable = continuable_base<std::decay_t<T>, std::decay_t<A>>>
  static auto create_from(T&& continuation, A annotation,
                          util::ownership ownership) {
    (void)annotation;
    return Continuable({std::forward<T>(continuation)}, ownership);
  }

  /// Returns the ownership of the given continuable_base
  template <typename Continuable>
  static util::ownership ownership_of(Continuable&& continuation) noexcept {
    return continuation.ownership_;
  }

  template <typename Data, typename Annotation>
  static Data&& consume(continuable_base<Data, Annotation>&& continuation) {
    return std::move(continuation).consume();
  }
};

/// Returns the signature hint of the given continuable
template <typename Data, typename... Args>
constexpr traits::identity<Args...>
annotation_of(traits::identity<continuable_base<Data, //
                                                traits::identity<Args...>>>) {
  return {};
}

/// Invokes a continuation object in a reference correct way
template <typename Data, typename Annotation, typename Callback>
void invoke_continuation(continuable_base<Data, Annotation>&& continuation,
                         Callback&& callback) noexcept {
  util::invoke(attorney::consume(std::move(continuation).finish()),
               std::forward<Callback>(callback));
}

// Returns the invoker of a callback, the next callback
// and the arguments of the previous continuation.
//
// The return type of the invokerOf function matches a callable of:
//   void(auto&& callback, auto&& next_callback, auto&&... args)
//
// The invoker decorates the result type in the following way
// - void              -> next_callback()
// - ?                 -> next_callback(?)
// - std::pair<?, ?>   -> next_callback(?, ?)
// - std::tuple<?...>  -> next_callback(?...)
//
// When the result is a continuation itself pass the callback to it
// - continuation<?...> -> result(next_callback);
namespace decoration {
/// Helper class wrapping the underlaying unwrapping lambda
/// in order to extend it with a hint method.
template <typename T, typename Hint>
class invoker : public T {
public:
  constexpr explicit invoker(T invoke) : T(std::move(invoke)) {
  }

  using T::operator();

  /// Returns the underlaying signature hint
  static constexpr Hint hint() noexcept {
    return {};
  }
};

#if defined(CONTINUABLE_HAS_EXCEPTIONS)
#define CONTINUABLE_BLOCK_TRY_BEGIN try {
#define CONTINUABLE_BLOCK_TRY_END                                              \
  }                                                                            \
  catch (...) {                                                                \
    std::forward<decltype(next_callback)>(next_callback)(                      \
        exception_arg_t{}, std::current_exception());                          \
  }

#else // CONTINUABLE_HAS_EXCEPTIONS
#define CONTINUABLE_BLOCK_TRY_BEGIN {
#define CONTINUABLE_BLOCK_TRY_END }
#endif // CONTINUABLE_HAS_EXCEPTIONS

/// Invokes the callback partially, keeps the exception_arg_t such that
/// we don't jump accidentally from the exception path to the result path.
template <typename T, typename... Args>
constexpr auto invoke_callback(T&& callable, exception_arg_t exception_arg,
                               Args&&... args) {
  return util::partial_invoke(std::integral_constant<std::size_t, 01>{},
                              std::forward<T>(callable), exception_arg,
                              std::forward<Args>(args)...);
}
template <typename T, typename... Args>
constexpr auto invoke_callback(T&& callable, Args&&... args) {
  return util::partial_invoke(std::integral_constant<std::size_t, 0U>{},
                              std::forward<T>(callable),
                              std::forward<Args>(args)...);
}

/// Invokes the given callable object with the given arguments while
/// marking the operation as non exceptional.
template <typename T, typename... Args>
constexpr void invoke_no_except(T&& callable, Args&&... args) noexcept {
  std::forward<T>(callable)(std::forward<Args>(args)...);
}

template <typename... Args, typename T>
void invoke_void_no_except(traits::identity<exception_arg_t, Args...>,
                           T&& /*callable*/) noexcept {
  // Don't invoke the next failure handler when being in an exception handler
}
template <typename... Args, typename T>
void invoke_void_no_except(traits::identity<Args...>, T&& callable) noexcept {
  std::forward<T>(callable)();
}

template <typename T, typename... Args>
constexpr auto make_invoker(T&& invoke, traits::identity<Args...>) {
  return invoker<std::decay_t<T>, traits::identity<Args...>>(
      std::forward<T>(invoke));
}

/// - continuable<?...> -> result(next_callback);
template <typename Data, typename Annotation>
constexpr auto
invoker_of(traits::identity<continuable_base<Data, Annotation>>) {
  /// Get the hint of the unwrapped returned continuable
  using Type =
      decltype(std::declval<continuable_base<Data, Annotation>>().finish());

  auto constexpr const hint = base::annotation_of(traits::identify<Type>{});

  return make_invoker(
      [](auto&& callback, auto&& next_callback, auto&&... args) {
        CONTINUABLE_BLOCK_TRY_BEGIN
          auto continuation_ =
              invoke_callback(std::forward<decltype(callback)>(callback),
                              std::forward<decltype(args)>(args)...);

          invoke_continuation(
              std::move(continuation_),
              std::forward<decltype(next_callback)>(next_callback));
        CONTINUABLE_BLOCK_TRY_END
      },
      hint);
}

/// - ? -> next_callback(?)
template <typename T>
constexpr auto invoker_of(traits::identity<T>) {
  return make_invoker(
      [](auto&& callback, auto&& next_callback, auto&&... args) {
        CONTINUABLE_BLOCK_TRY_BEGIN
          auto result =
              invoke_callback(std::forward<decltype(callback)>(callback),
                              std::forward<decltype(args)>(args)...);

          invoke_no_except(std::forward<decltype(next_callback)>(next_callback),
                           std::move(result));
        CONTINUABLE_BLOCK_TRY_END
      },
      traits::identify<T>{});
}

/// - void -> next_callback()
inline auto invoker_of(traits::identity<void>) {
  return make_invoker(
      [](auto&& callback, auto&& next_callback, auto&&... args) {
        CONTINUABLE_BLOCK_TRY_BEGIN
          invoke_callback(std::forward<decltype(callback)>(callback),
                          std::forward<decltype(args)>(args)...);
          invoke_void_no_except(
              traits::identity<traits::unrefcv_t<decltype(args)>...>{},
              std::forward<decltype(next_callback)>(next_callback));
        CONTINUABLE_BLOCK_TRY_END
      },
      traits::identity<>{});
}

/// - empty_result -> <cancel>
inline auto invoker_of(traits::identity<empty_result>) {
  return make_invoker(
      [](auto&& callback, auto&& next_callback, auto&&... args) {
        (void)next_callback;
        CONTINUABLE_BLOCK_TRY_BEGIN
          empty_result result =
              invoke_callback(std::forward<decltype(callback)>(callback),
                              std::forward<decltype(args)>(args)...);

          // Don't invoke anything here since returning an empty result
          // cancels the asynchronous chain effectively.
          (void)result;
        CONTINUABLE_BLOCK_TRY_END
      },
      traits::identity<>{});
}

/// - exceptional_result -> <throw>
inline auto invoker_of(traits::identity<exceptional_result>) {
  return make_invoker(
      [](auto&& callback, auto&& next_callback, auto&&... args) {
        util::unused(callback, next_callback, args...);
        CONTINUABLE_BLOCK_TRY_BEGIN
          exceptional_result result =
              invoke_callback(std::forward<decltype(callback)>(callback),
                              std::forward<decltype(args)>(args)...);

          // Forward the exception to the next available handler
          invoke_no_except(std::forward<decltype(next_callback)>(next_callback),
                           exception_arg_t{},
                           std::move(result).get_exception());
        CONTINUABLE_BLOCK_TRY_END
      },
      traits::identity<>{});
}

/// - result<?...> -> next_callback(?...)
template <typename... Args>
auto invoker_of(traits::identity<result<Args...>>) {
  return make_invoker(
      [](auto&& callback, auto&& next_callback, auto&&... args) {
        CONTINUABLE_BLOCK_TRY_BEGIN
          result<Args...> result =
              invoke_callback(std::forward<decltype(callback)>(callback),
                              std::forward<decltype(args)>(args)...);
          if (result.is_value()) {
            // Workaround for MSVC not capturing the reference
            // correctly inside the lambda.
            using Next = decltype(next_callback);

            traits::unpack(
                [&](auto&&... values) {
                  invoke_no_except(std::forward<Next>(next_callback),
                                   std::forward<decltype(values)>(values)...);
                },
                std::move(result));

          } else if (result.is_exception()) {
            // Forward the exception to the next available handler
            invoke_no_except(
                std::forward<decltype(next_callback)>(next_callback),
                exception_arg_t{}, std::move(result).get_exception());
          }

        // Otherwise the result is empty and we are cancelling our
        // asynchronous chain.
        CONTINUABLE_BLOCK_TRY_END
      },
      traits::identity<Args...>{});
}

/// Returns a sequenced invoker which is able to invoke
/// objects where std::get is applicable.
inline auto sequenced_unpack_invoker() {
  return [](auto&& callback, auto&& next_callback, auto&&... args) {
    CONTINUABLE_BLOCK_TRY_BEGIN
      auto result = invoke_callback(std::forward<decltype(callback)>(callback),
                                    std::forward<decltype(args)>(args)...);

      // Workaround for MSVC not capturing the reference
      // correctly inside the lambda.
      using Next = decltype(next_callback);

      traits::unpack(
          [&](auto&&... values) {
            invoke_no_except(std::forward<Next>(next_callback),
                             std::forward<decltype(values)>(values)...);
          },
          std::move(result));
    CONTINUABLE_BLOCK_TRY_END
  };
} // namespace decoration

// - std::pair<?, ?> -> next_callback(?, ?)
template <typename First, typename Second>
constexpr auto invoker_of(traits::identity<std::pair<First, Second>>) {
  return make_invoker(sequenced_unpack_invoker(),
                      traits::identity<First, Second>{});
}

// - std::tuple<?...>  -> next_callback(?...)
template <typename... Args>
constexpr auto invoker_of(traits::identity<std::tuple<Args...>>) {
  return make_invoker(sequenced_unpack_invoker(), traits::identity<Args...>{});
}

#undef CONTINUABLE_BLOCK_TRY_BEGIN
#undef CONTINUABLE_BLOCK_TRY_END
} // namespace decoration

/// Invoke the callback immediately
template <typename Invoker, typename... Args>
void on_executor(types::this_thread_executor_tag, Invoker&& invoker,
                 Args&&... args) {

  // Invoke the callback with the decorated invoker immediately
  std::forward<Invoker>(invoker)(std::forward<Args>(args)...);
}

/// Invoke the callback through the given executor
template <typename Executor, typename Invoker, typename... Args>
void on_executor(Executor&& executor, Invoker&& invoker, Args&&... args) {

  // Create a worker object which when invoked calls the callback with the
  // the returned arguments.
  auto work = [invoker = std::forward<Invoker>(invoker),
               args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
    traits::unpack(
        [&](auto&&... captured_args) {
          // Just use the packed dispatch method which dispatches the work on
          // the current thread.
          on_executor(types::this_thread_executor_tag{}, std::move(invoker),
                      std::forward<decltype(captured_args)>(captured_args)...);
        },
        std::move(args));
  };

  // Pass the work callable object to the executor
  std::forward<Executor>(executor)(std::move(work));
}

/// Tells whether we potentially move the chain upwards and handle the result
enum class handle_results {
  no, //< The result is forwarded to the next callable
  yes //< The result is handled by the current callable
};

// Silences a doxygen bug, it tries to map forward to std::forward
/// \cond false
/// Tells whether we handle the error through the callback
enum class handle_errors {
  no,     //< The error is forwarded to the next callable
  forward //< The error is forwarded to the callable while keeping its tag
};
/// \endcond

namespace callbacks {
namespace proto {
template <handle_results HandleResults, typename Base, typename Hint>
struct result_handler_base;
template <typename Base, typename... Args>
struct result_handler_base<handle_results::no, Base,
                           traits::identity<Args...>> {
  void operator()(Args... args) && {
    // Forward the arguments to the next callback
    std::move(static_cast<Base*>(this)->next_callback_)(std::move(args)...);
  }
};
template <typename Base, typename... Args>
struct result_handler_base<handle_results::yes, Base,
                           traits::identity<Args...>> {
  /// The operator which is called when the result was provided
  void operator()(Args... args) && {
    // In order to retrieve the correct decorator we must know what the
    // result type is.
    constexpr auto result =
        traits::identify<decltype(decoration::invoke_callback(
            std::move(static_cast<Base*>(this)->callback_),
            std::move(args)...))>{};

    // Pick the correct invoker that handles decorating of the result
    auto invoker = decoration::invoker_of(result);

    // Invoke the callback
    on_executor(std::move(static_cast<Base*>(this)->executor_),
                std::move(invoker),
                std::move(static_cast<Base*>(this)->callback_),
                std::move(static_cast<Base*>(this)->next_callback_),
                std::move(args)...);
  }
};

template <handle_errors HandleErrors, typename Base>
struct error_handler_base;
template <typename Base>
struct error_handler_base<handle_errors::no, Base> {
  /// The operator which is called when an error occurred
  void operator()(exception_arg_t tag, exception_t exception) && {
    // Forward the error to the next callback
    std::move(static_cast<Base*>(this)->next_callback_)(tag,
                                                        std::move(exception));
  }
};
template <typename Base>
struct error_handler_base<handle_errors::forward, Base> {
  /// The operator which is called when an error occurred
  void operator()(exception_arg_t, exception_t exception) && {
    constexpr auto result =
        traits::identify<decltype(decoration::invoke_callback(
            std::move(static_cast<Base*>(this)->callback_), exception_arg_t{},
            std::move(exception)))>{};

    auto invoker = decoration::invoker_of(result);

    // Invoke the error handler
    on_executor(std::move(static_cast<Base*>(this)->executor_),
                std::move(invoker),
                std::move(static_cast<Base*>(this)->callback_),
                std::move(static_cast<Base*>(this)->next_callback_),
                exception_arg_t{}, std::move(exception));
  }
};
} // namespace proto

template <typename Hint, handle_results HandleResults,
          handle_errors HandleErrors, typename Callback, typename Executor,
          typename NextCallback>
struct callback_base;

template <typename... Args, handle_results HandleResults,
          handle_errors HandleErrors, typename Callback, typename Executor,
          typename NextCallback>
struct callback_base<traits::identity<Args...>, HandleResults, HandleErrors,
                     Callback, Executor, NextCallback>
    : proto::result_handler_base<
          HandleResults,
          callback_base<traits::identity<Args...>, HandleResults, HandleErrors,
                        Callback, Executor, NextCallback>,
          traits::identity<Args...>>,
      proto::error_handler_base<
          HandleErrors,
          callback_base<traits::identity<Args...>, HandleResults, HandleErrors,
                        Callback, Executor, NextCallback>>,
      util::non_copyable {

  Callback callback_;
  Executor executor_;
  NextCallback next_callback_;

  explicit callback_base(Callback callback, Executor executor,
                         NextCallback next_callback)
      : callback_(std::move(callback)), executor_(std::move(executor)),
        next_callback_(std::move(next_callback)) {
  }

  /// Pull the result handling operator() in
  using proto::result_handler_base<
      HandleResults,
      callback_base<traits::identity<Args...>, HandleResults, HandleErrors,
                    Callback, Executor, NextCallback>,
      traits::identity<Args...>>::operator();

  /// Pull the error handling operator() in
  using proto::error_handler_base<
      HandleErrors,
      callback_base<traits::identity<Args...>, HandleResults, HandleErrors,
                    Callback, Executor, NextCallback>>::operator();

  /// Resolves the continuation with the given values
  void set_value(Args... args) {
    std::move (*this)(std::move(args)...);
  }

  /// Resolves the continuation with the given error variable.
  void set_exception(exception_t error) {
    std::move (*this)(exception_arg_t{}, std::move(error));
  }
};

template <typename Hint, handle_results HandleResults,
          handle_errors HandleErrors, typename Callback, typename Executor,
          typename NextCallback>
auto make_callback(Callback&& callback, Executor&& executor,
                   NextCallback&& next_callback) {
  return callback_base<Hint, HandleResults, HandleErrors,
                       std::decay_t<Callback>, std::decay_t<Executor>,
                       std::decay_t<NextCallback>>{
      std::forward<Callback>(callback), std::forward<Executor>(executor),
      std::forward<NextCallback>(next_callback)};
}

// TODO fixate the args
/// Once this was a workaround for GCC bug:
/// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=64095
struct final_callback : util::non_copyable {
  template <typename... Args>
  void operator()(Args... /*args*/) && {
  }

  void operator()(exception_arg_t, exception_t error) && {
    (void)error;
#ifndef CONTINUABLE_WITH_UNHANDLED_EXCEPTIONS
    // There were unhandled errors inside the asynchronous call chain!
    // Define `CONTINUABLE_WITH_UNHANDLED_EXCEPTIONS` in order
    // to ignore unhandled errors!"
#if defined(CONTINUABLE_HAS_EXCEPTIONS)
    try {
      std::rethrow_exception(error);
    } catch (std::exception const& exception) {
      (void)exception;
      util::trap();
    } catch (...) {
      util::trap();
    }
#else
    util::trap();
#endif
#endif // CONTINUABLE_WITH_UNHANDLED_EXCEPTIONS
  }

  template <typename... Args>
  void set_value(Args... args) {
    std::move (*this)(std::forward<Args>(args)...);
  }

  void set_exception(exception_t error) {
    // NOLINTNEXTLINE(hicpp-move-const-arg, performance-move-const-arg)
    std::move (*this)(exception_arg_t{}, std::move(error));
  }
};
} // namespace callbacks

/// Returns the next hint when the callback is invoked with the given hint
template <typename T, typename... Args>
constexpr auto
next_hint_of(std::integral_constant<handle_results, handle_results::yes>,
             traits::identity<T> /*callback*/,
             traits::identity<Args...> /*current*/) {
  // Partial Invoke the given callback
  using Result = decltype(
      decoration::invoke_callback(std::declval<T>(), std::declval<Args>()...));

  // Return the hint of thr given invoker
  return decltype(decoration::invoker_of(traits::identify<Result>{}).hint()){};
}
/// Don't progress the hint when we don't continue
template <typename T, typename... Args>
constexpr auto
next_hint_of(std::integral_constant<handle_results, handle_results::no>,
             traits::identity<T> /*callback*/,
             traits::identity<Args...> current) {
  return current;
}

namespace detail {
template <typename Callable>
struct exception_stripper_proxy {
  Callable callable_;
  template <typename... Args>
  auto operator()(exception_arg_t, Args&&... args)
      -> decltype(util::invoke(std::declval<Callable>(), //
                               std::declval<Args>()...)) {
    return util::invoke(std::move(callable_), //
                        std::forward<decltype(args)>(args)...);
  }
};
} // namespace detail

/// Removes the exception_arg_t from the arguments passed to the given callable
template <typename Callable>
auto strip_exception_arg(Callable&& callable) {
  using proxy = detail::exception_stripper_proxy<traits::unrefcv_t<Callable>>;
  return proxy{std::forward<Callable>(callable)};
}

/// Chains a callback together with a continuation and returns a continuation:
///
/// For example given:
/// - Continuation: continuation<[](auto&& callback) { callback("hi"); }>
/// - Callback: [](std::string) { }
///
/// This function returns a function accepting the next callback in the chain:
/// - Result: continuation<[](auto&& callback) { /*...*/ }>
///
template <handle_results HandleResults, handle_errors HandleErrors,
          typename Continuation, typename Callback, typename Executor>
auto chain_continuation(Continuation&& continuation, Callback&& callback,
                        Executor&& executor) {
  static_assert(is_continuable<std::decay_t<Continuation>>{},
                "Expected a continuation!");

  using Hint = decltype(base::annotation_of(traits::identify<Continuation>()));
  constexpr auto next_hint =
      next_hint_of(std::integral_constant<handle_results, HandleResults>{},
                   traits::identify<decltype(callback)>{}, Hint{});

  // TODO consume only the data here so the freeze isn't needed
  auto ownership_ = attorney::ownership_of(continuation);
  continuation.freeze();

  return attorney::create_from(
      [continuation = std::forward<Continuation>(continuation),
       callback = std::forward<Callback>(callback),
       executor = std::forward<Executor>(executor)] //
      (auto&& next_callback) mutable {
        // Invokes a continuation with a given callback.
        // Passes the next callback to the resulting continuable or
        // invokes the next callback directly if possible.
        //
        // For example given:
        // - Continuation: continuation<[](auto&& callback) { callback("hi"); }>
        // - Callback: [](std::string) { }
        // - NextCallback: []() { }
        auto proxy =
            callbacks::make_callback<Hint, HandleResults, HandleErrors>(
                std::move(callback), std::move(executor),
                std::forward<decltype(next_callback)>(next_callback));

        // Invoke the continuation with a proxy callback.
        // The proxy callback is responsible for passing
        // the result to the callback as well as decorating it.
        invoke_continuation(std::move(continuation), std::move(proxy));
      },
      next_hint, ownership_);
}

/// Final invokes the given continuation chain:
///
/// For example given:
/// - Continuation: continuation<[](auto&& callback) { callback("hi"); }>
template <typename Continuation>
void finalize_continuation(Continuation&& continuation) {
  invoke_continuation(std::forward<Continuation>(continuation),
                      callbacks::final_callback{});
}

/// Workaround for GCC bug:
/// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=64095
template <typename T>
class supplier_callback {
  T data_;

public:
  explicit supplier_callback(T data) : data_(std::move(data)) {
  }

  template <typename... Args>
  auto operator()(Args...) {
    return std::move(data_);
  }
};

/// Returns a continuable into a callable object returning the continuable
template <typename Continuation>
auto wrap_continuation(Continuation&& continuation) {
  continuation.freeze();
  return supplier_callback<std::decay_t<Continuation>>(
      std::forward<Continuation>(continuation));
}
} // namespace base
} // namespace detail
} // namespace cti

#endif // CONTINUABLE_DETAIL_BASE_HPP_INCLUDED
