//
//  nursery.hpp
//  client
//
//  Created by Antony Searle on 17/10/2026.
//

#ifndef nursery_hpp
#define nursery_hpp

#include "coroutine.hpp"

namespace wry::coroutine {

#pragma mark Nursery / counted_scope

    template<typename T> struct NurseryChildDelegate;
    template<typename T> struct NurseryChildValueDelegate;

    struct Nursery {

        Header _header = { &_static_resume, &_static_destroy };
        Atomic<std::ptrdiff_t> _counter{0};
        Atomic<std::ptrdiff_t> _cancelled_count{0};
        std::coroutine_handle<> _continuation;
        std::stop_token _outer_stop_token;
        std::stop_source _inner_stop_source;

        struct Inner : Delegate<> {

            Nursery* _self;

            explicit Inner(Nursery* self) : _self(self) {}

            virtual void return_void() noexcept override {}
            virtual void unhandled_exception() noexcept override { abort(); }
            virtual void unhandled_stopped() noexcept override { _static_destroy(_self); }
            virtual std::stop_token get_stop_token() noexcept override { return _self->_inner_stop_source.get_token(); }
            virtual std::coroutine_handle<> final_await_suspend() noexcept override {
                return std::coroutine_handle<>::from_address(_self);
            }

        };
        Inner _inner;


        struct Callback {
            std::stop_source _inner_stop_source;
            void operator()() {
                // A child parked in a cancelable leaf (recv_some) unwinds
                // synchronously inside this request_stop; the last child's
                // retire can destroy the joiner frame -- this Nursery, this
                // functor, and _inner_stop_source with it -- and free the
                // inner stop state under the iteration.  Pin it on the
                // native stack for the call (cf. WithDeadlineAwaitable).
                std::stop_source keepalive = _inner_stop_source;
                keepalive.request_stop();
            }
        };

        std::stop_callback<Callback> _outer_stop_callback;

        static void _static_resume(void* ptr) {
            auto self = (Nursery*)ptr;
            auto count = self->_counter.fetch_sub_release(1);
            assert(count >= 0);
            if (count == 0) {
                (void) self->_counter.load_acquire();
                ptr = take(self->_continuation).address();
                if (!self->_outer_stop_token.stop_requested()) {
                    [[clang::musttail]] return resume_by_address(ptr);
                } else {
                    [[clang::musttail]] return destroy_by_address(ptr);
                }
            }
        }

        static void _static_destroy(void* ptr) {
            auto self = (Nursery*)ptr;
            self->_cancelled_count.fetch_add_relaxed(1);
            _static_resume(ptr);
        }

        bool request_stop() {
            return _inner_stop_source.request_stop();
        }

        Nursery() : Nursery(std::stop_token{}) {}
        explicit Nursery(std::stop_token token)
        : _outer_stop_token(token)
        , _inner(this)
        , _outer_stop_callback(_outer_stop_token, Callback{_inner_stop_source}) {
        }

        // A nursery must be joined exactly once before destruction
        ~Nursery() {
            assert((_counter.nonatomic_load() == -1)
                   && "nursery was not joined");
        }

        // co_await nursery.fork(foo(x)) immediately starts foo on the current
        // thread and schedules the caller to execute soon.  No target: foo's
        // value, if any, is discarded and an unhandled exception aborts (see
        // BasicReturnChannel)
        [[nodiscard]] auto fork(Future<>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<>::promise_type* _promise;
                std::coroutine_handle<Future<>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    [[maybe_unused]] auto count = _nursery->_counter.fetch_add_relaxed(1);
                    assert(count >= 0);
                    _promise->set_delegate(&_nursery->_inner);
                    auto handle = handle_from_promise(take(_promise));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, take(future._promise)};
        }

        // co_await nursery.fork(y, bar(x)) immediately starts bar on the current
        // thread and schedules the caller to execute soon.  When bar completes
        // it assigns to y; it is racy to access y until the nursery has been
        // joined
        template<typename T>
        [[nodiscard]] auto fork(execution::Outcome<T>& target, Future<T>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                execution::Outcome<T>* _target;
                Future<T>::promise_type* _promise;
                std::coroutine_handle<typename Future<T>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    [[maybe_unused]] auto count = _nursery->_counter.fetch_add_relaxed(1);
                    assert(count >= 0);
                    _promise->template emplace_delegate<NurseryChildDelegate<T>>(_nursery, _target);
                    auto handle = handle_from_promise(take(_promise));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, &target, take(future._promise)};
        }

        // co_await nursery.fork(y, bar(x)) with a bald target: bar's value is
        // assigned to y on completion, and the paths an Outcome would carry,
        // an exception or a stop, abort instead.  For work that has neither,
        // which then needs no Outcome per child and no await per result
        template<typename T>
        [[nodiscard]] auto fork(T& target, Future<T>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                T* _target;
                Future<T>::promise_type* _promise;
                std::coroutine_handle<typename Future<T>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    [[maybe_unused]] auto count = _nursery->_counter.fetch_add_relaxed(1);
                    assert(count >= 0);
                    _promise->template emplace_delegate<NurseryChildValueDelegate<T>>(_nursery, _target);
                    auto handle = handle_from_promise(take(_promise));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, &target, take(future._promise)};
        }

        // nursery.soon(foo(x)) schedules foo to execute soon and continues
        // the calling context normally.  The calling context does not have to
        // be a coroutine.  No target: see fork(Future<>&&)
        void soon(Future<>&& future) {
            [[maybe_unused]] auto count = _counter.fetch_add_relaxed(1);
            assert(count >= 0);
            future._promise->set_delegate(&_inner);
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // nursery.soon(y, bar(x)) schedules bar to execute soon and
        // continues the calling context normally.  The calling context does
        // not have to be a coroutine.  When bar completes, it assigns to y;
        // it is racy to access y until the nursery has been joined
        template<typename T>
        void soon(execution::Outcome<T>& target, Future<T>&& future) {
            [[maybe_unused]] auto count = _counter.fetch_add_relaxed(1);
            assert(count >= 0);
            future._promise->template emplace_delegate<NurseryChildDelegate<T>>(this, &target);
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // nursery.soon(y, bar(x)) with a bald target: see fork(T&, Future<T>&&)
        template<typename T>
        void soon(T& target, Future<T>&& future) {
            [[maybe_unused]] auto count = _counter.fetch_add_relaxed(1);
            assert(count >= 0);
            future._promise->template emplace_delegate<NurseryChildValueDelegate<T>>(this, &target);
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // co await nursery.join() suspends the caller and resumes it after
        // all forks are complete.
        [[nodiscard]] auto join() {
            struct Awaitable {
                Nursery* _nursery;
                bool await_ready() noexcept {
                    auto count = _nursery->_counter.load_relaxed();
                    assert(count >= 0);
                    bool result = (count == 0) && !_nursery->_outer_stop_token.stop_requested();
                    if (result) {
                        // All children had already completed and stop was not requested
                        // Establish child completions happens-before
                        count = _nursery->_counter.exchange_acquire(-1);
                        assert(count == 0);
                    } else {
                        // Some children had not completed or stop was requested
                    }
                    return result;
                }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _nursery->_continuation = std::move(continuation);
                    // Arm the join and publish the continuation
                    auto count = _nursery->_counter.fetch_sub_release(1);
                    assert(count >= 0);
                    if (count == 0) {
                        // All children had already completed
                        // The continuation has not run.
                        // *this is live
                        // Establish child completions happens-before
                        (void) _nursery->_counter.load_acquire();
                        // Cancellation point
                        if (_nursery->_outer_stop_token.stop_requested()) {
                            continuation.destroy();
                            return std::noop_coroutine();
                        }
                        return _nursery->_continuation;
                    } else {
                        // Some children had not completed.
                        // The continuation may have run.
                        // *this may be destroyed.
                        return std::noop_coroutine();
                    }
                }
                std::ptrdiff_t await_resume() noexcept {
                    return take(_nursery)->_cancelled_count.nonatomic_load();
                }
                ~Awaitable() {
                    assert(!_nursery || _nursery->_outer_stop_token.stop_requested());
                }
            };
            return Awaitable{this};
        }

        // TODO: How to we fork / spawn Senders?  We have to put the
        // OperationState on the heap, is using a coroutine thunk therefore as
        // good as a bespoke solution?

    }; // struct Nursery

    // Per-child delegate for a targeted fork or soon.  It lives in the
    // child's promise (emplace_delegate): the only object with the child's
    // lifetime, since the fork awaitable is gone once the child is running.
    template<typename T>
    struct NurseryChildDelegate : OutcomeDelegate<NurseryChildDelegate<T>, T> {
        Nursery* _nursery;
        execution::Outcome<T>* _target;
        NurseryChildDelegate(Nursery* nursery, execution::Outcome<T>* target)
        : _nursery(nursery)
        , _target(target) {
        }
        execution::Outcome<T>& outcome() noexcept {
            return *_target;
        }
        virtual void unhandled_stopped() noexcept override {
            set_stopped(*_target);
            // TODO: Is this type-info-discarding static call a wart or a
            // virtuous sign of same-interface everywhere.  Is _static_destroy
            // called exclusively via this path?
            Nursery::_static_destroy(_nursery);
        }
        virtual std::stop_token get_stop_token() noexcept override {
            return _nursery->_inner_stop_source.get_token();
        }
        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return std::coroutine_handle<>::from_address(_nursery);
        }
    };

    // Per-child delegate for a fork or soon with a bald target: only the
    // value channel is wired, and an exception or a stop takes Delegate's
    // default, which aborts.  Parked like NurseryChildDelegate
    template<typename T>
    struct NurseryChildValueDelegate : Delegate<T> {
        Nursery* _nursery;
        T* _target;
        NurseryChildValueDelegate(Nursery* nursery, T* target)
        : _nursery(nursery)
        , _target(target) {
        }
        virtual void return_value(T value) noexcept override {
            *_target = std::move(value);
        }
        virtual std::stop_token get_stop_token() noexcept override {
            return _nursery->_inner_stop_source.get_token();
        }
        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return std::coroutine_handle<>::from_address(_nursery);
        }
    };

} // namespace wry::coroutine

#endif /* nursery_hpp */
