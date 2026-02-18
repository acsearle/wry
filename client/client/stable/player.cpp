//
//  player.cpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#include "player.hpp"
#include "transaction.hpp"
#include "world.hpp"

namespace wry {
    
    void garbage_collected_scan(const Player::Action& x) {
        switch (x.tag) {
            case Player::Action::NONE:
                break;
            case Player::Action::WRITE_VALUE_FOR_COORDINATE:
                garbage_collected_scan(x.coordinate);
                garbage_collected_scan(x.value);
                break;
            default:
                abort();
        }
    }
    
    void Player::_garbage_collected_scan() const {
        garbage_collected_scan(_queue);
    }

    
    void Player::notify(TransactionContext* context) const {
        
        // always wait again
        Transaction* tx = Transaction::make(context, this, 2);
        tx->wait_on_time(context->_world->_time + 1);
        
        Action action = {};
        if (_queue.try_pop_front(action)) {
            switch (action.tag) {
                case Player::Action::NONE:
                    break;
                case Player::Action::WRITE_VALUE_FOR_COORDINATE:
                    tx->write_value_for_coordinate(action.coordinate, action.value);
                    break;
                default:
                    abort();
            };
        }        
    }
    
} // namespace wry
