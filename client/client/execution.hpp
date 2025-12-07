//
//  execution.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef execution_hpp
#define execution_hpp

#include <cstdio>
#include <cstdlib>

#include <tuple>
#include <utility>

#include "coroutine.hpp"

namespace wry {
    
    namespace execution {
        
        // Toy implementation of ideas from std::execution
        
        // std::execution strives to
        // - decompose async operations
        // - use a common interface
        // - not require type erasure, indirection, or heap allocation
        
        // We do not support the extra channels of .set_error, .set_stopped;
        // we may in the future restrict .set_value to a single argument and no
        // overloads.  This greatly simplifies the implementation and most of
        // the functionality can be recovered by passing a variant of the
        // different "receptors".
        //
        // The fly in the ointment is zero argument receivers
        
#pragma mark - Basic semantics
        
        struct _trivial_receiver {
            void set_value(auto&&) {}
            void set_value() {}
        };
        
        template<typename Receiver>
        struct _trivial_operation {
            Receiver _receiver;
            void start() {
                _receiver.set_value();
            }
        };
        
        struct _trivial_sender {
            template<typename Receiver>
            auto connect(Receiver receiver) && {
                return _trivial_operation<Receiver> { std::move(receiver) };
            }
        };        
        
#pragma mark - Type erasure
        
        struct any_operation {
            
            struct _base_t {
                virtual ~_base_t() = default;
                virtual void start() = 0;
            };
            
            template<typename Operation>
            struct _derived_t : _base_t {
                Operation _operation;
                explicit _derived_t(Operation op) : _operation(std::move(op)) {}
                virtual ~_derived_t() override = default;
                virtual void start() override { _operation.start(); }
            };
            
            _base_t* _ptr = nullptr;
            
            any_operation() = default;
            any_operation(any_operation const&) = delete;
            any_operation(any_operation&& other) : _ptr(std::exchange(other._ptr, nullptr)) {}
            
            ~any_operation() { delete _ptr; }
            
            void swap(any_operation& other) {
                using std::swap;
                swap(_ptr, other._ptr);
            }
            
            any_operation& operator=(any_operation const&) = delete;
            any_operation& operator=(any_operation&& other) {
                any_operation(std::move(other)).swap(*this);
                return *this;
            }
            
            template<typename Operation>
            void emplace(Operation op) {
                delete std::exchange(_ptr, new _derived_t<Operation>(std::move(op)));
            }
            
            void start() {
                _ptr->start();
            }
            
        };
        
        
        // fundamental receivers
                
        // template<typename Sender>
        // void debug_detach(Sender sender) {
            // using Operation = decltype(std::move(sender).connect(_detached_receiver{}));
        // }
        
        
        
        
        
        
        
        
        
        // Partial implementation of the sender-receiver ideas from P2300
        
        

        
        template<typename R, typename... Args>
        struct _just_operation {
            R _receiver;
            std::tuple<Args...> _tuple;
            void start() {
                std::apply([this](auto&&... args) {
                    std::move(_receiver).set_value(std::forward<decltype(args)>(args)...);
                }, std::move(_tuple));
            }
        };
        
        template<typename... Args>
        struct _just_sender {
            std::tuple<Args...> _tuple;
            template<typename R>
            auto connect(R receiver) && {
                return _just_operation<R, Args...>{std::move(receiver), std::move(_tuple)};
            }
        };
        
        auto just(auto&&... args) {
            return _just_sender<decltype(args)...>{{FWD(args)...}};
        }
        
        
        
        template<typename Receiver, typename Invocable>
        struct _then_receiver : Receiver {
            Invocable _invocable;
            void set_value(auto&&... args) && {
                std::move(*(Receiver*)this).set_value(std::move(_invocable)(FWD(args)...));
            }
        };
        
        template<typename Sender, typename Invocable>
        struct _then_sender {
            Sender _sender;
            Invocable _invocable;
            template<typename Receiver>
            auto connect(Receiver receiver) && {
                return std::move(_sender).connect(_then_receiver<Receiver, Invocable>{std::move(receiver), std::move(_invocable)});
            }
        };
        
        template<typename Sender, typename Invocable>
        auto then(Sender sender, Invocable invocable) {
            return _then_sender<Sender, Invocable>{std::move(sender), std::move(invocable)};
        }
        
        
        

        
        
        
        
        

        
        
        
        template<typename Scheduler, typename Receiver>
        struct _continues_on_receiver {
            Scheduler _scheduler;
            Receiver _receiver;
            // We can either type-erase the operation and store it on the heap,
            // or build the machinery to expose and store all possible kinds of
            // sends.  Which is a variant of variants of tuples, and might as well be
            // the type of a single channel without futher support.
            any_operation _operation;
            void set_value(auto&& x) {
                _operation.emplace(then(schedule(std::move(_scheduler)),
                                        [=]() mutable { return std::move(x); })
                                   .connect(std::move(_receiver)));
                _operation.start();
            }
        };
        
        
        template<typename Sender, typename Scheduler>
        struct _continues_on_sender {
            Sender _sender;
            Scheduler _scheduler;
            template<typename Receiver>
            auto connect(Receiver receiver) && {
                return std::move(_sender)
                    .connect(_continues_on_receiver<Scheduler, Receiver>{
                        std::move(_scheduler),
                        std::move(receiver)
                    });
            }
        };
        
        
        template<typename Sender, typename Scheduler>
        auto continues_on(Sender input, Scheduler scheduler) {
            return _continues_on_sender<Sender, Scheduler>(std::move(input), std::move(scheduler));
        }
        
    } // namespace execution
    
    
    namespace coroutine {
        
        template<typename>
        struct callback_handle;
        
        template<typename R, typename... Args>
        struct callback_handle<R(Args...)> {
            R _callback(void*, Args...);
            R operator()(Args... args) {
                return _callback((void*)this, std::forward<Args>(args)...);
            };
        };
        
        template<typename... Args>
        struct receiver_of {
            virtual void set_value(Args...) = 0;
        };
        
        
        template<typename>
        struct sender_traits {};
        
        template<typename T>
        using sender_traits_t = typename sender_traits<T>::type;
        
        template<typename...>
        struct receiver_traits {};
        
        template<typename T>
        using receiver_traits_t = typename receiver_traits<T>::type;
        
        
        
        
        
        template<typename...>
        struct _coroutine_handle_receiver;
        
        template<>
        struct _coroutine_handle_receiver<> {
            std::coroutine_handle<> _handle;
            void set_value() {
                _handle.resume();
            }
        };
        
        template<typename T>
        struct _coroutine_handle_receiver<T> {
            std::coroutine_handle<> _handle;
            T* _value;
            void set_value(T value) {
                *_value = value;
                _handle.resume();
            }
        };
        
        template<typename...>
        struct _co_sender_promise;
        
        template<typename... Args>
        struct co_sender {
            using promise_type = _co_sender_promise<Args...>;
            promise_type* _promise;
            template<typename Receiver> auto connect(Receiver receiver);
        };
        
        
        template<typename Sender>
        auto _common_await_transform(Sender sender) {
            using T = sender_traits_t<Sender>;
            using U = decltype(std::move(sender).connect());
            struct awaitable {
                Sender _sender;
                T _value;
                U _operation;
                constexpr bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<> handle) {
                    _operation = std::move(_sender).connect(_coroutine_handle_receiver<T>{std::move(handle), &_value});
                    _operation.start();
                }
                T await_resume() {
                    return std::move(_value);
                }
            };
            return awaitable{std::move(sender)};
        }
        
        auto _common_await_transform(co_sender<> sender);
        
        template<typename T>
        auto _common_await_transform(co_sender<T> sender) {
            struct awaitable : receiver_of<T> {
                std::coroutine_handle<> _handle;
                T _value;
                explicit awaitable(std::coroutine_handle<> handle)
                : _handle(std::move(handle)) {
                }
                constexpr bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) noexcept {
                    std::coroutine_handle<typename co_sender<T>::promise_type>::from_address(_handle.address()).promise()._receiver = this;
                    return std::exchange(_handle, handle);
                }
                T await_resume() {
                    return std::move(_value);
                }
                virtual void set_value(T value) override {
                    _value = std::move(value);
                }
            };
            return awaitable{std::coroutine_handle<typename co_sender<T>::promise_type>::from_promise(*sender._promise)};
            
        }
        
        
        template<>
        struct _co_sender_promise<> {
            receiver_of<>* _receiver = nullptr;
            co_sender<> get_return_object() { return co_sender<>{this}; }
            auto initial_suspend() noexcept { return std::suspend_always{}; }
            void return_void() noexcept {}
            void unhandled_exception() noexcept { abort(); }
            
            auto final_suspend() noexcept {
                struct awaitable : ResumeNever {
                    void await_suspend(std::coroutine_handle<_co_sender_promise<>> handle) noexcept {
                        receiver_of<>* receiver = handle.promise()._receiver;
                        handle.destroy();
                        receiver->set_value();
                    }
                };
                return awaitable{};
            }
            template<typename Sender>
            auto await_transform(Sender sender) {
                return _common_await_transform(std::move(sender));
            }
        };
        
        
        template<typename T>
        struct _co_sender_promise<T> {
            receiver_of<T>* _receiver = nullptr;
            T _value;
            co_sender<T> get_return_object() { return co_sender<T>{this}; }
            auto initial_suspend() noexcept { return std::suspend_always{}; }
            void return_value(auto&& expr) noexcept {
                _value = std::forward<decltype(expr)>(expr);
            }
            void unhandled_exception() noexcept { abort(); }
            auto final_suspend() noexcept {
                struct awaitable : ResumeNever {
                    void await_suspend(std::coroutine_handle<_co_sender_promise> handle) noexcept {
                        T value = std::move(handle.promise()._value);
                        receiver_of<T>* receiver = handle.promise()._receiver;
                        handle.destroy();
                        receiver->set_value(std::move(value));
                    }
                };
                return awaitable{};
            }
            template<typename Sender>
            auto await_transform(Sender sender) {
                return _common_await_transform(std::move(sender));
            }
        };
        
        
        
        template<typename...>
        struct _co_sender_operation;
        
        template<typename T, typename Receiver>
        struct _co_sender_operation<T, Receiver> : receiver_of<T> {
            co_sender<T> _sender;
            Receiver _receiver;
            _co_sender_operation(co_sender<T> sender, Receiver receiver)
            : _sender(std::move(sender))
            , _receiver(std::move(receiver)) {
            }
            void start() {
                _sender._promise->_receiver = this;
                std::coroutine_handle<typename co_sender<T>::promise_type>::from_promise(*_sender._promise).resume();
            }
            virtual void set_value(T value) override {
                _receiver.set_value(std::move(value));
            }
        };
        
        template<typename Receiver>
        struct _co_sender_operation<Receiver> : receiver_of<> {
            co_sender<> _sender;
            Receiver _receiver;
            _co_sender_operation(co_sender<> sender, Receiver receiver)
            : _sender(std::move(sender))
            , _receiver(std::move(receiver)) {
            }
            void start() {
                _sender._promise->_receiver = this;
                std::coroutine_handle<typename co_sender<>::promise_type>::from_promise(*_sender._promise).resume();
            }
            virtual void set_value() override {
                _receiver.set_value();
            }
        };
        
        
        template<typename... Args> template<typename Receiver>
        auto co_sender<Args...>::connect(Receiver receiver) {
            return _co_sender_operation<Args..., Receiver>{
                std::move(*this),
                std::move(receiver)
            };
        }
        
        
        
        inline auto _common_await_transform(co_sender<> sender) {
            struct awaitable : receiver_of<> {
                std::coroutine_handle<> _handle;
                explicit awaitable(std::coroutine_handle<> handle) : _handle(handle) {}
                constexpr bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) noexcept {
                    std::coroutine_handle<typename co_sender<>::promise_type>::from_address(_handle.address()).promise()._receiver = this;
                    return std::exchange(_handle, handle);
                }
                void await_resume() const noexcept {}
                virtual void set_value() {
                    _handle.resume();
                }
            };
            return awaitable{std::coroutine_handle<co_sender<>::promise_type>::from_promise(*sender._promise)};
        }
        
        
        
        
        
    }
    
} // namespace wry

#endif /* execution_hpp */
