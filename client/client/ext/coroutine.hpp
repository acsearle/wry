//
//  coroutine.hpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#ifndef coroutine_hpp
#define coroutine_hpp

#include <coroutine>

#include <chrono>
#include <deque>
#include <exception>
#include <semaphore>
#include <thread>
#include <queue>
#include <stop_token>

#include "assert.hpp"
#include "atomic.hpp"
#include "utility.hpp"
#include "mutex.hpp"
#include "variant.hpp"
#include "memory.hpp"
#include "stdint.hpp"

#include "epoch_allocator.hpp"
#include "global_work_queue.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<>);

    void collector_register_cycle_callback(uint64_t k,
                                           void* callback) noexcept;

} // namespace wry

namespace wry::Coroutine {

#pragma mark Explicit frame header

    struct Header {
        void (*resume )(void*);
        void (*destroy)(void*);
    };

    inline void resume_by_address(void* ptr) {
        [[clang::musttail]] return ((Header*)ptr)->resume(ptr);
    }

    inline void destroy_by_address(void* ptr) {
        [[clang::musttail]] return ((Header*)ptr)->destroy(ptr);
    }

    inline void global_work_queue_schedule_destroy(void* ptr) {
        auto header = (Header*)ptr;
        header->resume = header->destroy;
        global_work_queue_schedule(ptr);
    }

#pragma mark Helper functions

    inline std::coroutine_handle<> null_to_noop(std::coroutine_handle<> handle) {
        return handle ? handle : std::noop_coroutine();
    }

    void global_work_queue_schedule_after(std::chrono::steady_clock::time_point when, void* address);

#pragma mark Basic Awaitables

    struct ResumeNever : std::suspend_always {
        void await_resume() const noexcept {
            std::unreachable();
        }
    };
    
    struct SuspendAndDestroy : ResumeNever {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            handle.destroy();
        }
    };
    
    struct DebugSuspendAndLeak : ResumeNever {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {            
        }
    };
    
    struct TransferToPoolExecutor : std::suspend_always {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            global_work_queue_schedule(handle);
        }
    };

    struct TransferToBlockableExecutor : std::suspend_always {
        void await_suspend(std::coroutine_handle<>) const noexcept;
    };

    struct DebugWaitForCollectionCycles {
        uint64_t cycles;
        bool await_ready() const noexcept { return cycles == 0; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            collector_register_cycle_callback(cycles, handle.address());
        }
        void await_resume() const noexcept {}
    };

    struct Until {
        std::chrono::steady_clock::time_point _when;
        bool await_ready() const noexcept {
            return std::chrono::steady_clock::now() >= _when;
        }
        void await_suspend(std::coroutine_handle<>) const noexcept;
        void await_resume() const noexcept {}
    };



    // Hoist value, error, stopped into value

    enum TerminalReceiverState {
        TERMINAL_RECEIVER_STATE_NONE,
        TERMINAL_RECEIVER_STATE_VALUE,
        TERMINAL_RECEIVER_STATE_ERROR,
        TERMINAL_RECEIVER_STATE_STOPPED,
        TERMINAL_RECEIVER_STATE_FINAL,
    };

    template<typename T>
    struct TerminalReceiver {

        TerminalReceiverState _state = TERMINAL_RECEIVER_STATE_NONE;
        union {
            T _value;
            std::exception_ptr _error;
        };

        TerminalReceiver()
        : _state(TERMINAL_RECEIVER_STATE_NONE) {
        }

        TerminalReceiver(TerminalReceiver const&) = delete;
        TerminalReceiver(TerminalReceiver&&) = delete;

        ~TerminalReceiver() {
            switch (_state) {
                case TERMINAL_RECEIVER_STATE_VALUE:
                    std::destroy_at(&_value);
                    break;
                case TERMINAL_RECEIVER_STATE_ERROR:
                    std::destroy_at(&_error);
                    break;
                default:
                    break;
            }
        }

        template<typename... Args>
        void set_value(Args&&... args) {
            switch (_state) {
                case TERMINAL_RECEIVER_STATE_NONE:
                    std::construct_at(&_value, std::forward<Args>(args)...);
                    _state = TERMINAL_RECEIVER_STATE_VALUE;
                    break;
                default:
                    abort();
            }
        }

        template<typename... Args>
        void set_error(std::exception_ptr error) {
            switch (_state) {
                case TERMINAL_RECEIVER_STATE_NONE:
                    std::construct_at(&_error, std::move(error));
                    _state = TERMINAL_RECEIVER_STATE_ERROR;
                    break;
                default:
                    abort();
            }
        }

        void set_stopped() {
            switch (_state) {
                case TERMINAL_RECEIVER_STATE_NONE:
                    _state = TERMINAL_RECEIVER_STATE_STOPPED;
                    break;
                default:
                    abort();
            }
        }

        bool is_value() const { return _state == TERMINAL_RECEIVER_STATE_VALUE; };
        bool is_error() const { return _state == TERMINAL_RECEIVER_STATE_ERROR; };
        bool is_stopped() const { return _state == TERMINAL_RECEIVER_STATE_STOPPED; };

        T get_value() {
            switch (_state) {
                case TERMINAL_RECEIVER_STATE_VALUE:
                    // TODO: Tricky empty-by-exceptional-move state
                    _state = TERMINAL_RECEIVER_STATE_FINAL;
                    return std::move(_value);
                case TERMINAL_RECEIVER_STATE_ERROR:
                    _state = TERMINAL_RECEIVER_STATE_FINAL;
                    std::rethrow_exception(_error);
                default:
                    abort();
            }
        }

    };


#pragma mark Future

    template<typename T>
    struct ReturnChannel {
        using stored_type = T;
        T* _target = nullptr;
        void set_target(T* target) noexcept {
            assert(!_target);
            _target = target;
        }
        template<typename U>
        void return_value(U&& u) const noexcept(std::is_nothrow_assignable_v<T&, U&&>) {
            *_target = std::forward<U>(u);
        }
    };

    template<>
    struct ReturnChannel<void> {
        using stored_type = std::monostate;
        void return_void() const noexcept {}
    };

    template<typename = void> struct Future;
    using Task = Future<>;

    template<typename T>
    struct Promise : ReturnChannel<T> {

        std::coroutine_handle<> _continuation = nullptr;
        std::stop_token _stop_token;
        std::exception_ptr* _exception = nullptr;

        constexpr std::suspend_always initial_suspend() const noexcept {
            return std::suspend_always{};
        }

        Future<T> get_return_object();

        void unhandled_exception() noexcept {
            if (!_exception) // Awaiter does not accept exceptions
                abort();
            if (*_exception) // An exception was already set
                abort();
            *_exception = std::current_exception();
        }

        struct FinalAwaitable : ResumeNever {
            template<typename DerivedPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<DerivedPromise> handle) const noexcept {
                std::coroutine_handle<> continuation = std::exchange(handle.promise()._continuation, nullptr);
                handle.destroy();
                assert(continuation);
                return continuation;
            }
        };

        auto final_suspend() const noexcept {
            return FinalAwaitable{};
        }

        ~Promise() {
            if (_continuation) {
                // We did not resume our continuation, therefore we are
                // participants in a cancellation unwinding
                _continuation.destroy();
            }
        }

        void set_continuation(std::coroutine_handle<> continuation) {
            assert(!_continuation);
            _continuation = std::move(continuation);
        }

        void set_continuation(void* ptr) {
            assert(!_continuation);
            _continuation = std::coroutine_handle<>::from_address(ptr);
        }

        void set_stop_token(std::stop_token t) {
            _stop_token = t;
        }

        void set_exception_target(std::exception_ptr* xpp) {
            assert(!_exception && xpp);
            _exception = xpp;
        }

    }; // Promise<T>

    template<typename T>
    [[nodiscard]] std::stop_token get_stop_token(Promise<T> const& promise) {
        return promise._stop_token;
    }

    template<typename T>
    [[nodiscard]] std::stop_token get_stop_token(std::coroutine_handle<Promise<T>> handle) {
        return get_stop_token(handle.promise());
    }

    template<typename T>
    std::coroutine_handle<Promise<T>> handle_from_promise(Promise<T>* promise) {
        assert(promise);
        return std::coroutine_handle<typename Future<T>::promise_type>::from_promise(*promise);
    }

    // SuspendAndDestroy, but restricted to Promise<T> that perform unwinding
    struct SuspendAndCancel : ResumeNever {
        template<typename T>
        void await_suspend(std::coroutine_handle<Promise<T>> handle) {
            handle.destroy();
        }
    };

    template<typename T>
    struct GetPromise {
        Promise<T>* _promise;
        constexpr bool await_ready() const noexcept { return false; }
        std::coroutine_handle<Promise<T>> await_suspend(std::coroutine_handle<Promise<T>> continuation) {
            _promise = &continuation.promise();
            return std::move(continuation);
        }
        Promise<T>& await_resume() {
            return *_promise;
        }
    };

    struct GetStopToken {
        std::stop_token _stop_token;
        constexpr bool await_ready() const noexcept { return false; }
        template<typename T>
        std::coroutine_handle<Promise<T>> await_suspend(std::coroutine_handle<Promise<T>> continuation) {
            _stop_token = get_stop_token(continuation);
            return continuation;
        }
        std::stop_token await_resume() {
            return _stop_token;
        }
    };

    template<typename T>
    struct Future {

        using promise_type = Promise<T>;
        promise_type* _promise = nullptr;
        explicit Future(promise_type* promise) : _promise(promise) {}

        Future() = delete;
        Future(Future const&) = delete;
        Future(Future&& other) : _promise(exchange(other._promise, nullptr)) {}
        ~Future() {
            // This could happen if we create a future, hold it across a
            // suspension point, and then resume it.  But it's more likely to
            // just have been forgotten.  See if we ever have a compelling need
            // to do the former.
            if (_promise)
                abort();
        }
        Future& operator=(Future const&) = delete;
        Future& operator=(Future&& other) {
            Future a{std::move(other)};
            using std::swap;
            swap(_promise, a._promise);
            return *this;
        }

        struct Awaitable {
            promise_type* _promise = nullptr;
            [[no_unique_address]] ReturnChannel<T>::stored_type _target;
            std::exception_ptr _exception = nullptr;
            constexpr bool await_ready() const noexcept { return false; }
            template<typename OuterPromise>
            std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<OuterPromise> continuation) noexcept {
                if constexpr (!std::is_void_v<T>)
                    _promise->set_target(&_target);
                _promise->set_exception_target(&_exception);
                _promise->set_stop_token(get_stop_token(continuation));
                _promise->set_continuation(std::move(continuation));
                return handle_from_promise(std::exchange(_promise, nullptr));
            }
            auto await_resume() {
                if (_exception)
                    std::rethrow_exception(_exception);
                if constexpr (!std::is_void_v<T>)
                    return std::move(_target);
            }
        };

        auto operator co_await() {
            return Awaitable{std::exchange(this->_promise, nullptr)};
        }

    };

    template<typename T>
    Future<T> Promise<T>::get_return_object() {
        return Future<T>{this};
    }

    template<typename T>
    std::coroutine_handle<typename Future<T>::promise_type> handle_from_future(Future<T>&& future) {
        return handle_from_promise(std::exchange(future._promise, nullptr));
    }

#pragma mark Complex Awaitables


    struct RequestStopAfterAwaitable {

        struct Frame {

            Header _header = { &static_resume, &static_destroy };
            std::stop_source _inner_stop_source;
            std::stop_token _outer_stop_token;
            std::atomic<void*> _continuation{nullptr};
            std::atomic<std::ptrdiff_t> _reference_count_minus_one{0};

            explicit Frame(std::stop_source ss)
            : _inner_stop_source(std::move(ss)) {
            }

            void acquire() {
                _reference_count_minus_one.fetch_add(1, std::memory_order_relaxed);
            }

            void release() {
                if (!_reference_count_minus_one.fetch_sub(1, std::memory_order_release)) {
                    (void) _reference_count_minus_one.load(std::memory_order_acquire);
                    [[maybe_unused]] auto observed = _continuation.load(std::memory_order_relaxed);
                    assert((observed == nullptr) || (observed == this));
                    delete this;
                }
            }

            static void static_resume(void* ptr) {
                Frame* self = (Frame*)ptr;
                // Race to stop inner (idempotent)
                self->_inner_stop_source.request_stop();
                // Race to take the continuation
                void* continuation = self->_continuation.exchange(nullptr, std::memory_order_acquire);
                // Poll cancellation
                bool stopped = self->_outer_stop_token.stop_requested();
                // Release ownership
                self->release();
                // *self might now be destroyed
                if (continuation == ptr) {
                    // The continuation was taken by stop_callback
                    // We are done.
                } else {
                    assert(continuation != nullptr);
                    // We took the continuation
                    if (stopped) {
                        [[clang::musttail]] return destroy_by_address(continuation);
                    } else {
                        [[clang::musttail]] return resume_by_address(continuation);
                    }
                }
            }

            static void static_destroy(void*) {
                std::unreachable();
            }

        }; // struct Frame

        struct Callback {

            Frame* _frame;

            void operator()() {
                // We were called because outer was stopped
                assert(_frame->_outer_stop_token.stop_requested());
                // Race to stop inner (idempotent)
                _frame->_inner_stop_source.request_stop();
                // Race to take the continuation and replace it with the
                // final state marked by the frame address
                void* continuation = _frame->_continuation.exchange(_frame, std::memory_order_acquire);
                if (continuation == 0) {
                    // The real continuation was not installed yet.
                    // Our exchange notifies await_suspend to clean up.
                } else if (continuation == _frame) {
                    // The real continuation was already consumed.
                    // The timer ran before the outer scope was cancelled.
                    // The outer source canceled before the callback was torn
                    // down.
                    // We are done.
                } else {
                    // The real continuation is taken by us.
                    // The outer scope was cancelled before the timer ran.
                    global_work_queue_schedule_destroy(continuation);
                }
                // *this might now be destroyed
            }

        }; // struct Callback

        Frame* _frame;
        std::chrono::steady_clock::time_point _deadline;
        std::optional<std::stop_callback<Callback>> _callback;

        RequestStopAfterAwaitable() = delete;

        RequestStopAfterAwaitable(std::stop_source stop_source, std::chrono::steady_clock::time_point deadline)
        : _frame(new Frame(std::move(stop_source)))
        , _deadline(deadline) {
        }

        RequestStopAfterAwaitable(RequestStopAfterAwaitable const&) = delete;
        RequestStopAfterAwaitable(RequestStopAfterAwaitable&&) = delete;

        ~RequestStopAfterAwaitable() {
            // Make sure we tear down the stop_callback before we potentially
            // destroy the Frame
            _callback.reset();
            _frame->release();
        }

        RequestStopAfterAwaitable& operator=(RequestStopAfterAwaitable const&) = delete;
        RequestStopAfterAwaitable& operator=(RequestStopAfterAwaitable&&) = delete;

        constexpr bool await_ready() const noexcept {
            return false;
        }

        template<typename OuterPromise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise> continuation) noexcept {
            // Copies guard against the Awaitable being destroyed under us
            auto deadline = _deadline;
            auto frame = _frame;
            frame->acquire();
            frame->_outer_stop_token = get_stop_token(continuation);
            // The stop callback and the timer race to cancel the inner.
            // Arm the stop callback before we have installed the continuation
            _callback.emplace(frame->_outer_stop_token, Callback{frame});
            // Install the continuation
            void* before = frame->_continuation.exchange(continuation.address(), std::memory_order_acq_rel);
            if (before == 0) {
                // The stop callback did not run before we installed the real continuation
                global_work_queue_schedule_after(deadline, frame);
            } else {
                assert(before == frame);
                // The stop callback did run before we installed the real continuation
                frame->release();
                destroy_by_address(continuation.address());
            }
            // *this might now be destroyed
            return std::noop_coroutine();
        }

        void await_resume() const noexcept {
        }

    };

    inline auto request_stop_after(std::stop_source stop_source, std::chrono::steady_clock::duration timeout) {
        return RequestStopAfterAwaitable(std::move(stop_source), std::chrono::steady_clock::now() + timeout);
    }



#pragma mark Nursery / counted_scope

    struct Nursery {

        Header _header = { &_static_resume, &_static_destroy };
        Atomic<std::ptrdiff_t> _counter{0};
        Atomic<std::ptrdiff_t> _cancelled_count{0};
        std::ptrdiff_t _children = 0;
        std::coroutine_handle<> _continuation;
        std::stop_token _outer_stop_token;
        std::stop_source _inner_stop_source;

        struct Callback {
            std::stop_source _inner_stop_source;
            void operator()() {
                _inner_stop_source.request_stop();
            }
        };

        std::stop_callback<Callback> _outer_stop_callback;

        static void _static_resume(void* ptr) {
            auto self = (Nursery*)ptr;
            auto count = self->_counter.sub_fetch_release(1);
            if (count == 0) {
                (void) self->_counter.load_acquire();
                ptr = std::exchange(self->_continuation, nullptr).address();
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
        , _outer_stop_callback(_outer_stop_token, Callback{_inner_stop_source}) {
        }

        ~Nursery() {
            // Detect destruction of a coroutine containing a running nursery
            assert(!_children && !_counter.load_relaxed());
        }

        struct Factory {
            std::stop_token _stop_token;
            constexpr bool await_ready() const noexcept {
                return false;
            }
            template<typename T>
            bool await_suspend(std::coroutine_handle<Promise<T>> continuation) noexcept {
                _stop_token = continuation.promise()._stop_token;
                return false;
            }
            [[nodiscard]] Nursery await_resume() noexcept {
                return Nursery(std::move(_stop_token));
            }
        };

        // co_await nursery.fork(foo(x)) immediately starts foo on the current thread
        // and schedules the caller to execute soon
        [[nodiscard]] auto fork(Future<>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<>::promise_type* _promise;
                std::coroutine_handle<Future<>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _promise->set_continuation(_nursery);
                    _promise->set_stop_token(_nursery->_inner_stop_source.get_token());
                    // set_exception_target deliberately omitted
                    auto handle = handle_from_promise(std::exchange(_promise, nullptr));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, std::exchange(future._promise, nullptr)};
        }
        
        // co_await nursery.fork(y, bar(x)) immediately starts bar on the current
        // thread and schedules the caller to execute soon.  When bar completes
        // it assigns to y; it is racy to access y until the nursery has been
        // joined
        template<typename T>
        [[nodiscard]] auto fork(T& target, Future<T>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<T>::promise_type* _promise;
                T* _target;
                std::coroutine_handle<typename Future<T>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _promise->set_continuation(_nursery);
                    _promise->set_stop_token(_nursery->_inner_stop_source.get_token());
                    _promise->set_target(_target);
                    // set_exception_target deliberately omitted
                    auto handle = handle_from_promise(std::exchange(_promise, nullptr));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, std::exchange(future._promise, nullptr), &target};
        }

        // nursery.soon(foo(x)) schedules foo to execute soon and continues
        // the calling context normally.  The calling context does not have to
        // be a coroutine
        void soon(Future<>&& future) {
            ++_children;
            future._promise->set_continuation(this);
            future._promise->set_stop_token(_inner_stop_source.get_token());
            // set_exception_target deliberately omitted
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // nursery.soon(y, bar(x)) schedules bar to execute soon and
        // continues the calling context normally.  The calling context does
        // not have to be a coroutine.  When bar completes, it assigns to y;
        // it is racy to access y until the nursery has been joined
        template<typename T>
        void soon(T& target, Future<T>&& future) {
            _children++;
            future._promise->set_continuation(this);
            future._promise->set_target(&target);
            // set_exception_target deliberately omitted
            future._promise->set_stop_token(_inner_stop_source.get_token());
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // co await nursery.join() suspends the caller and resumes it after
        // all forks are complete.  The nursery may then be resumed.
        [[nodiscard]] auto join() {
            struct Awaitable {
                Nursery* _nursery;
                bool await_ready() noexcept {
                    auto count = _nursery->_counter.load_relaxed();
                    bool result = (count == -_nursery->_children) && !_nursery->_outer_stop_token.stop_requested();
                    if (result) {
                        _nursery->_counter.exchange_acquire(0);
                        _nursery->_children = 0;
                    }
                    return result;
                }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _nursery->_continuation = std::move(continuation);
                    auto count = _nursery->_counter.add_fetch_release(std::exchange(_nursery->_children, 0));
                    if (count == 0) {
                        (void) _nursery->_counter.load_acquire();
                        if (_nursery->_outer_stop_token.stop_requested()) {
                            continuation.destroy();
                            return std::noop_coroutine();
                        }
                        return _nursery->_continuation;
                    } else {
                        return std::noop_coroutine();
                    }
                }
                std::ptrdiff_t await_resume() noexcept {
                    return std::exchange(_nursery, nullptr)->_cancelled_count.nonatomic_exchange(0);
                }
                ~Awaitable() {
                    assert(!_nursery || _nursery->_outer_stop_token.stop_requested());
                }
            };
            return Awaitable{this};
        }

    }; // struct Nursery

    // Block the current thread until the awaitable completes.
    
    template<typename T>
    auto sync_wait(T&& awaitable) {
        struct Frame {
            void (*_resume)(void*) = &_static_resume;
            void (*_destroy)(void*) = &_static_resume;
            std::binary_semaphore _semaphore{0}; // start in unavailable state
            static void _static_resume(void* ptr) {
                auto self = (Frame*)ptr;
                self->_semaphore.release();
            }
            // TODO: We have no way to communicate cancellation into a generic awaitable
        };
        Frame frame;
        if (!awaitable.await_ready()) {
            global_work_queue_schedule(awaitable.await_suspend(std::coroutine_handle<>::from_address(&frame)));
            frame._semaphore.acquire();
        }
        return awaitable.await_resume();
    }





    template<typename T>
    struct SingleProducerSingleConsumerQueue {

        std::mutex _mutex;
        std::queue<T> _queue;
        std::coroutine_handle<> _pop_continuation;

        void emplace(auto&&... args) {
            std::unique_lock guard{_mutex};
            _queue.emplace(FORWARD(args)...);
            if (_pop_continuation)
                global_work_queue_schedule(std::exchange(_pop_continuation, nullptr));
        }

        struct _pop_awaitable : std::suspend_always {
            SingleProducerSingleConsumerQueue* _context;
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) {
                assert(_context);
                std::unique_lock guard{_context->_mutex};
                if (_context->_queue.empty()) {
                    assert(!_context->_pop_continuation); // single consumer violated
                    _context->_pop_continuation = continuation;
                    return std::noop_coroutine();
                } else {
                    return continuation;
                }
            }
            T await_resume() {
                assert(_context);
                std::unique_lock guard{_context->_mutex};
                assert(!_context->_queue.empty());
                T result{std::move(_context->_queue.front())};
                _context->_queue.pop();
                _context = nullptr;
                return result;
            }
        };

        [[nodiscard]] auto pop() {
            return _pop_awaitable{this};
        };

        bool try_pop(T& victim) {
            std::unique_lock guard{_mutex};
            assert(!_pop_continuation); // single consumer violated
            bool result = !_queue.empty();
            if (result) {
                victim = std::move(_queue.front());
                _queue.pop();
            }
            return result;
        }

    };




    // TODO: OneShotEvent's single usage circumvents structured concurrency
    // and leaves the non-timed-out branch detached (hence the shared_ptr).
    // Since the use case is a unit test, maybe OK

    // ---- OneShotEvent: timed one-shot completion / cancellable timer -------
    //
    // One cell, two ends.  co_await cell->wait_until(t) is simultaneously
    //   - a completion wait that signal() resolves early (returns true), and
    //   - a timer that signal() cancels (the same race, seen from the other
    //     end); if the deadline arrives first the wait resolves false.
    // signal() may be called from any thread, before or after the wait
    // begins, and repeatedly (idempotent).  Single waiter, single use.
    //
    // The waiter resumes on the GC pool via global_work_queue_schedule (a
    // pinned mutator worker) -- unlike Until, which resumes on a dispatch
    // thread.
    //
    // Lifetime: the cell is shared_ptr-managed (construct via make()) so the
    // coordination state outlives whichever of {signaler, deadline fire,
    // waiter} finishes last; it must not live in the winner's scope.  A
    // timed-out wait leaves a detached signaler (e.g. a still-running
    // background save) holding its own reference harmlessly.
    //
    // The deadline is dispatch_after_f, which has no cancellation:
    // "cancelling the timer" is losing the race -- the fire still happens at
    // the deadline, no-ops against the decided state, and drops its
    // reference.  Upgrade the backend to a dispatch source if early physical
    // release ever matters.
    //
    // State (one atomic word): EMPTY, SIGNALED, TIMED_OUT, or the waiter's
    // coroutine handle address (frame allocations are aligned, so 1 and 2
    // cannot alias a real handle).
    //
    //   EMPTY -> SIGNALED       signal() before the wait; wait is ready
    //   EMPTY -> <handle>       await_suspend installs the waiter
    //   <handle> -> SIGNALED    signal() wins and schedules the waiter
    //   <handle> -> TIMED_OUT   deadline wins and schedules the waiter
    //
    // Terminal states absorb the loser's attempt.  (EMPTY -> TIMED_OUT is
    // unreachable single-use: the timer is armed only after the install.)
    //
    // ORDER: await_suspend publishes the suspended frame with a release CAS;
    // the deciding CAS in _decide is acq_rel (acquire: take ownership of
    // that frame before scheduling it; release: publish the signaler's
    // preceding writes -- the payload -- into the state word).  await_ready
    // and await_resume load with acquire, closing the payload edge when the
    // waiter observes SIGNALED.

    struct OneShotEvent : std::enable_shared_from_this<OneShotEvent> {

        static constexpr uintptr_t EMPTY = 0;
        static constexpr uintptr_t SIGNALED = 1;
        static constexpr uintptr_t TIMED_OUT = 2;

        Atomic<uintptr_t> _state{EMPTY};

        static std::shared_ptr<OneShotEvent> make() {
            return std::make_shared<OneShotEvent>();
        }

        void _decide(uintptr_t terminal) {
            uintptr_t expected = _state.load_relaxed();
            for (;;) {
                if ((expected == SIGNALED) || (expected == TIMED_OUT))
                    return;  // already decided; late or duplicate, a no-op
                if (_state.compare_exchange_weak_acq_rel_relaxed(expected,
                                                                 terminal)) {
                    if (expected != EMPTY)
                        global_work_queue_schedule((void*)expected);
                    return;
                }
            }
        }

        void signal() { _decide(SIGNALED); }

        struct WaitUntil {
            std::shared_ptr<OneShotEvent> _cell;
            std::chrono::steady_clock::time_point _when;

            bool await_ready() const noexcept {
                return _cell->_state.load_acquire() == SIGNALED;
            }
            // Defined in coroutine.cpp (libdispatch).  Returns false --
            // resume immediately -- when signal() beat the install.
            bool await_suspend(std::coroutine_handle<> handle) noexcept;
            bool await_resume() const noexcept {
                return _cell->_state.load_acquire() == SIGNALED;
            }
        };

        [[nodiscard]] WaitUntil wait_until(std::chrono::steady_clock::time_point when) {
            return WaitUntil{shared_from_this(), when};
        }

        [[nodiscard]] WaitUntil wait_for(std::chrono::nanoseconds duration) {
            return wait_until(std::chrono::steady_clock::now() + duration);
        }

    }; // struct OneShotEvent


    template<typename T>
    struct BasicFromStopped {

        void (*_resume)(void*) = &_static_resume;
        void (*_destroy)(void*) = &_static_destroy;
        Promise<T>* _inner_promise;
        std::coroutine_handle<> _continuation = nullptr;

        T _value = {};
        std::exception_ptr _exception = nullptr;
        bool _stopped = false;

        static void _static_resume(void* ptr) {
            auto self = (BasicFromStopped*)ptr;
            // Normal flow ==> tail call the continuation
            [[clang::musttail]] return resume_by_address(std::exchange(self->_continuation, nullptr).address());
        }

        static void _static_destroy(void* ptr) {
            auto self = (BasicFromStopped*)ptr;
            self->_stopped = true;
            // Stack unwinding, we are in nested destructors ==> schedule the continuation
            global_work_queue_schedule(std::exchange(self->_continuation, nullptr));
        }

        explicit BasicFromStopped(Future<T>&& future)
        : _inner_promise{std::exchange(future._promise, nullptr)} {
        }

        BasicFromStopped(BasicFromStopped const&) = delete;
        BasicFromStopped(BasicFromStopped&&) = delete;

        ~BasicFromStopped() {
            if (_inner_promise) {
                abort();
            }
        }

        constexpr bool await_ready() const noexcept {
            return false;
        }

        template<typename OuterPromise>
        std::coroutine_handle<Promise<T>> _await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            _continuation = continuation;
            _inner_promise->set_continuation(this);
            _inner_promise->set_target(&_value);
            _inner_promise->set_exception_target(&_exception);
            return std::coroutine_handle<Promise<T>>::from_promise(*std::exchange(_inner_promise, nullptr));
        }

        std::optional<T> await_resume() {
            if (_exception)
                std::rethrow_exception(_exception);
            if (_stopped)
                return {};
            else
                return { std::move(_value) };
        }

    }; // OptionalFromStopped

    template<typename T>
    struct OptionalFromStopped : BasicFromStopped<T> {

        explicit OptionalFromStopped(Future<T>&& future) : BasicFromStopped<T>(std::move(future)) {}

        template<typename OuterPromise>
        std::coroutine_handle<Promise<T>> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            this->_inner_promise->set_stop_token(get_stop_token(continuation));
            return this->_await_suspend(std::move(continuation));
        }

    };

    template<typename T>
    struct Shield : BasicFromStopped<T> {

        std::stop_source _stop_source;

        explicit Shield(Future<T>&& future) : BasicFromStopped<T>(std::move(future)) {}

        template<typename OuterPromise>
        std::coroutine_handle<Promise<T>> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            this->_inner_promise->set_stop_token(_stop_source.get_token());
            return this->_await_suspend(std::move(continuation));
        }
    };


    template<typename T>
    auto stopped_as_optional(Future<T>&& future) {
        return OptionalFromStopped<T>{std::move(future)};
    }

    template<typename T>
    auto shield(Future<T>&& future) {
        return Shield<T>{std::move(future)};
    }



    struct Race : Nursery {

        Atomic<std::ptrdiff_t> _winner{-1};

        bool claim(std::ptrdiff_t index) {
            std::ptrdiff_t expected = -1;
            if (!_winner.compare_exchange_strong_release_relaxed(expected, index))
                return false;
            this->request_stop();            // hasten the losers
            return true;
        }

    };


    template<typename T>
    Coroutine::Task race_entry(Race* race, std::ptrdiff_t index,
                               std::optional<T>* target, Future<T> inner) {
        T value = co_await std::move(inner);    // cancelled while parked -> this
                                                // frame unwinds, nursery tallies
        if (race->claim(index))
            *target = std::move(value);
        // a tie-loser falls through: its value is destroyed right here
    }








} // namespace wry::Coroutine

namespace wry {

    // Process-lifetime work anchor (a Go-style wait group).  Background tasks
    // spawned onto the work queue register here so that main can block until
    // they finish before cancelling the thread pool -- otherwise a task that has
    // yielded mid-flight (e.g. a chunked background save) would be abandoned at
    // shutdown, leaking its frame and leaving a half-written file.
    //
    // It is inherently a single process-global instance (blocking; no other use
    // case), so it is expressed as static methods over hidden state

    // must call before wait is called
    void wait_group_spawn(Coroutine::Task task);

    // blocking; consumes main's sentinel, so call exactly once
    void wait_group_wait();

    // consumes main's sentinel, so call exactly once
    void wait_group_set_callback(void* callback);

} // namespace wry

#endif /* coroutine_hpp */
