#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct MCTS MCTS;

typedef struct MCTSGame
{
    ///////////
    // Moves
    ////////
    size_t move_size;
    size_t max_moves;
    bool   (*apply_move)(void* state, const void* move);
    size_t (*get_moves)(const void* state, void* out_moves);
    void   (*random_playout_step)(const void* state, void* out_move);
    bool   (*are_moves_equal)(const void* a, const void* b);

    ///////////
    // State
    /////////
    /*
     * MCTSGame owns state buffers of size `state_size`, however, all
     * memory inside those state buffers is to be managed by the game 
     * implementation. This can be achieved via the `init_state`, `copy_state`,
     * and `destroy_state` methods below.
    */

    size_t state_size;

    /*
     * Initialize an allocated state buffer.
     *
     * The caller provides `state_size` bytes of uninitialized memory and an
     * optional `params` args which can be used to pass external state needed
     * to initialize the state.
     * 
     * On success, the state is fully initialized and may be passed to
     * copy_state() and destroy_state().
     *
     * On failure, init_state() is responsible for cleaning up any resources
     * it allocated. The caller must not pass the failed state to
     * destroy_state().
    */
    bool (*init_state)(void* state, const void* params);

    /* Copies src state into dest state. `dest` and `src` must already be
     * allocated state buffers.
     * If not defined defaults to memcpy.
     */
    void (*copy_state)(void* dest, const void* src);

    /* Frees memory from a state. Does *not* free the state buffer itself,
     * only any memory allocated *inside* the state buffer.
     * If not defined this is a no-op.
     */
    void (*destroy_state)(void* state);

    /* Returns if the game is over. */
    bool   (*is_terminal)(const void* state);

    size_t num_players;

    /* Returns which player's turn it is. Necessary because we support games
     * with multiple players.
    */
    size_t (*get_turn)(const void* state);

    /* Calculate rewards for every player and put them in out_vector */
    void (*get_rewards)(const void* state, double* out_vector);

    ////////////////
    // MCTS Config
    //////////////
    size_t initial_pool_size;
    unsigned int rng_seed;

} MCTSGame;

MCTS* mcts_create(const MCTSGame *game, const void* params);

/* Get a move from a Monte Carlo Tree Search. Return success/fail */
const void* mcts_search(MCTS* mcts, void* initial_state, unsigned int think_time);

// MUST provide legal move. 
bool mcts_advance(MCTS* mcts, void* move);

void  mcts_destroy(MCTS* mcts);
