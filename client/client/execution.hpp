//
//  execution.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef execution_hpp
#define execution_hpp

#include <cstdio>

#include <tuple>
#include <utility>

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
    
} // namespace wry

#endif /* execution_hpp */
