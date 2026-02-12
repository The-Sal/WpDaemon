/**
 * @file state_machine.hpp
 * @brief State machine for managing WireProxy connection states
 * 
 * This header defines the state machine that tracks the lifecycle of
 * a WireProxy connection. The states transition as follows:
 * 
 * IDLE → STARTING → RUNNING → STOPPING → IDLE
 *   ↑                                    |
 *   └────────────────────────────────────┘ (on error or stop)
 * 
 * The state machine is thread-safe and can be accessed from multiple
 * threads (e.g., TCP command handler and process monitor).
 */

#pragma once

#include <mutex>
#include <atomic>
#include <string>

namespace wpmd {

    /**
     * @brief Enumeration of possible WireProxy states
     * 
     * IDLE:     No WireProxy process is running, ready to start
     * STARTING: WireProxy process is being spawned (validation, log setup)
     * RUNNING:  WireProxy process is active and running
     * STOPPING: WireProxy process is being terminated
     */
    enum class State {
        IDLE,       ///< No process running, ready to accept spin_up
        STARTING,   ///< Process spawn in progress
        RUNNING,    ///< Process is running
        STOPPING    ///< Process termination in progress
    };

    /**
     * @brief Converts a State enum to human-readable string
     * 
     * @param state The state to convert
     * @return std::string String representation ("IDLE", "STARTING", etc.)
     */
    inline std::string state_to_string(State state) {
        switch (state) {
            case State::IDLE:     return "IDLE";
            case State::STARTING: return "STARTING";
            case State::RUNNING:  return "RUNNING";
            case State::STOPPING: return "STOPPING";
            default:              return "UNKNOWN";
        }
    }

    /**
     * @brief Thread-safe state machine for WireProxy lifecycle management
     * 
     * This class manages the state transitions of the WireProxy daemon.
     * All state changes are protected by a mutex to ensure thread safety
     * when accessed from the TCP server thread and process monitor thread.
     * 
     * Usage:
     *   StateMachine sm;
     *   sm.transition_to(State::STARTING);  // From IDLE only
     *   auto current = sm.get_state();       // Get current state
     */
    class StateMachine {
    public:
        /**
         * @brief Constructs a state machine initialized to IDLE
         */
        StateMachine() : current_state_(State::IDLE) {}

        /**
         * @brief Gets the current state (thread-safe)
         * 
         * @return State The current state
         */
        State get_state() const {
            return current_state_.load();
        }

        /**
         * @brief Attempts to transition to a new state
         * 
         * Validates that the transition is valid before changing state.
         * Valid transitions:
         * - IDLE → STARTING (spin_up command)
         * - STARTING → RUNNING (process verified alive)
         * - STARTING → IDLE (startup failure)
         * - RUNNING → STOPPING (spin_down command)
         * - RUNNING → IDLE (process died unexpectedly)
         * - STOPPING → IDLE (process terminated)
         * 
         * @param new_state The desired new state
         * @return true if transition was successful
         * @return false if transition is invalid
         */
        bool transition_to(State new_state);

        /**
         * @brief Checks if a state transition is valid without performing it
         * 
         * @param from Current state
         * @param to Desired new state
         * @return true if transition is valid
         * @return false if transition is invalid
         */
        static bool is_valid_transition(State from, State to);

    private:
        /** @brief Atomic state storage for lock-free reads */
        std::atomic<State> current_state_;
        
        /** @brief Mutex for state change validation and synchronization */
        mutable std::mutex mutex_;
    };

} // namespace wpmd
