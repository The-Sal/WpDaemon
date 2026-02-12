//
// Created by opencode on 12/02/2026.
//

#include "wpmd/state_machine.hpp"

namespace wpmd {

    bool StateMachine::is_valid_transition(State from, State to) {
        // Define valid state transitions
        switch (from) {
            case State::IDLE:
                // From IDLE, can only go to STARTING
                return to == State::STARTING;
                
            case State::STARTING:
                // From STARTING, can go to RUNNING (success) or IDLE (failure)
                return to == State::RUNNING || to == State::IDLE;
                
            case State::RUNNING:
                // From RUNNING, can go to STOPPING (command) or IDLE (died)
                return to == State::STOPPING || to == State::IDLE;
                
            case State::STOPPING:
                // From STOPPING, can only go to IDLE
                return to == State::IDLE;
                
            default:
                return false;
        }
    }

    bool StateMachine::transition_to(State new_state) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        State current = current_state_.load();
        
        if (!is_valid_transition(current, new_state)) {
            return false;
        }
        
        current_state_.store(new_state);
        return true;
    }

} // namespace wpmd
