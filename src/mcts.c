#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../include/mcts.h"
#include "../include/pool.h"

typedef struct Node {
    /** Use pointers to first child and next sibling to create a n-ary tree
     * structure while maintaining a fixed Node size for compatability with
     * our pool allocator.
     */
    struct Node* parent;
    struct Node* first_child;
    struct Node* next_sibling;

    size_t visits;

    /* We will add a chunk of memory for the move associated with this
     * node in pool_create and then copy it in.
     * We will also allocate space for the reward vector.
     * IT IS IMPORTANT THAT WE ALLOCATE SPACE FOR THE REWARD VECTOR *FIRST* for
     * alignment reasons.
    */
    uint8_t data[];
    
} Node;

static inline double* node_reward_vector(const Node *n)
{
    return (double *)(n->data);
}

static inline void* node_move(const Node *n, size_t num_players)
{
    return n->data + sizeof(double) * num_players;
}

static inline void game_copy_state(
    const MCTSGame* game,
    void* dest,
    const void* src
)
{
    if (game->copy_state)
    {
        game->copy_state(dest, src);
    }
    else
    {
        memcpy(dest, src, game->state_size);
    }
}

static inline void game_destroy_state(const MCTSGame* game, void* state)
{
    if (game->destroy_state)
    {
        game->destroy_state(state);
    }
}

typedef struct MCTS
{
    Pool*               node_pool;

    /* The MASTER root that holds the whole tree */
    Node*               root;

    /* Moveable root used for subsequent searches.
       Might be useful to keep whole tree off of root for eventual
       transpositions in sibling / cousin nodes instead of freeing
       siblings and their children?
       TODO: Verify this strategy
     */
    Node*               root_iter;

    const MCTSGame*     game;
    void*               game_state;

    /* We only expand one node at a time, so it is useful to have a move
     * buffer that we can rewrite to when generating moves.
     * Moves will then be copied into newly expanded nodes.
    */
    void* move_buffer;

    /* Keep a buffer to be used as a state scratchpad as we simulate
     * outcomes
     */
    void* state_buffer;
} MCTS;

/** Add potential moves from parent. Return success/failure. */
static bool tree_expand(MCTS* mcts, Node* parent);

static bool tree_is_leaf(const Node* node);

/** Perform a random rollout of a game until a termination state is reached.
 * Puts the end "reward" for each player in `out_rewards`.
 * Returns success/failure.
 */
static bool simulate(MCTS* mcts, double* out_rewards, size_t num_players);

static void backpropagate(Node* node, const double* reward_vector, size_t num_players);

static Node* reset_selection(MCTS* mcts);

static Node* select_child(MCTS* mcts, const Node* current);

MCTS* mcts_create(const MCTSGame* game)
{
    if (!game)
    {
        return NULL;
    }

    assert(game->apply_move != NULL);
    assert(game->get_moves != NULL);
    assert(game->random_playout_step != NULL);
    assert(game->get_rewards != NULL);
    assert(game->is_terminal != NULL);
    assert(game->are_moves_equal != NULL);
    assert(game->get_turn != NULL);

    MCTS* mcts = malloc(sizeof(MCTS));
    if (mcts == NULL)
    {
        return NULL;
    }

    mcts->game = game;

    // Each node will contain one move and a reward vector.
    mcts->node_pool = pool_create(
        sizeof(Node) + 
        game->move_size +
        sizeof(double) * game->num_players,
        game->initial_pool_size
    );
    if (!mcts->node_pool)
    {
        free(mcts);
        return NULL;
    }
    mcts->game_state = malloc(game->state_size);
    if (!mcts->game_state)
    {
        pool_destroy(mcts->node_pool);
        free(mcts);
        return NULL;
    }

    mcts->move_buffer = malloc(game->move_size * game->max_moves);
    if (!mcts->move_buffer)
    {
        pool_destroy(mcts->node_pool);
        free(mcts->game_state);
        free(mcts);
        return NULL;
    }

    mcts->state_buffer = malloc(game->state_size);
    if (!mcts->state_buffer)
    {
        free(mcts->move_buffer);
        free(mcts->game_state);
        pool_destroy(mcts->node_pool);
        free(mcts);
        return NULL;
    }

    mcts->root = pool_alloc(mcts->node_pool);
    mcts->root->first_child = NULL;
    mcts->root->next_sibling = NULL;
    mcts->root->parent = NULL;
    mcts->root->visits = 0;
    memset(node_reward_vector(mcts->root),
       0,
       sizeof(double) * game->num_players
    );
    mcts->root_iter = mcts->root;

    // Seed pseudorandom number generator for MCTS rollouts
    srand(game->rng_seed);

    return mcts;
}

const void* mcts_search(MCTS* mcts, void* initial_state, unsigned int think_time)
{
    clock_t start = clock();
    if (start < 0)
    {
        perror("ERROR: clock() failed!");
        return NULL;
    }

    game_copy_state(mcts->game, mcts->game_state, initial_state);

    double* sim_reward_vec = malloc(sizeof(double) * mcts->game->num_players);
    if (!sim_reward_vec)
    {
        return NULL;
    }

    while (clock() - start < think_time * CLOCKS_PER_SEC)
    {
        Node* node = reset_selection(mcts);

        while (!tree_is_leaf(node))
        {
            node = select_child(mcts, node);
        }

        if (mcts->game->is_terminal(mcts->state_buffer))
        {
            mcts->game->get_rewards(mcts->state_buffer, sim_reward_vec);
            backpropagate(node, sim_reward_vec, mcts->game->num_players);
            continue;
        }
        
        tree_expand(mcts, node);
        node = select_child(mcts, node);
        simulate(mcts, sim_reward_vec, mcts->game->num_players);
        backpropagate(node, sim_reward_vec, mcts->game->num_players);
    }

    // Find node with most visits (best move)
    Node* max_node = NULL;
    size_t max = 0;
    for (
        Node* child = mcts->root_iter->first_child;
        child != NULL;
        child = child->next_sibling
    ) {
        if (child->visits > max)
        {
            max = child->visits;
            max_node = child;
        }
    }

    free(sim_reward_vec);

    if (!max_node)
    {
        return NULL;
    }

    return node_move(max_node, mcts->game->num_players);
}

bool mcts_advance(MCTS* mcts, void* move)
{
    assert(mcts->root_iter != NULL);
    // Check all children of root_iter and find the node that has
    // the matching move. If it doesn't exist, add it.
    Node* last_node = NULL;
    for (Node* n = mcts->root_iter->first_child;
         n != NULL;
         n = n->next_sibling)
    {
        void* child_move = node_move(n, mcts->game->num_players);
        if (mcts->game->are_moves_equal(move, child_move))
        {
            mcts->root_iter = n;
            return true;
        }
        last_node = n;
    }

    // We didn't find it in children, so lets add it.
    // Note: This puts the burden of checking the validity of the move on the
    // caller.
    Node* new_node = (last_node) ? last_node->next_sibling : NULL;
    new_node = pool_alloc(mcts->node_pool);
    if (!new_node)
    {
        return false;
    }
    new_node->parent = mcts->root_iter;
    new_node->first_child = NULL;
    new_node->next_sibling = NULL;
    new_node->visits = 0;
    memset(
        node_reward_vector(new_node),
        0, 
        sizeof(double) * mcts->game->num_players
    );
    memcpy(
        node_move(new_node, mcts->game->num_players),
        move,
        mcts->game->move_size
    );

    mcts->root_iter = new_node;
    return true;
}

void mcts_destroy(MCTS* mcts)
{
    assert(mcts != NULL);
    pool_destroy(mcts->node_pool);

    game_destroy_state(mcts->game, mcts->game_state);
    free(mcts->game_state);

    game_destroy_state(mcts->game, mcts->state_buffer);
    free(mcts->state_buffer);

    free(mcts->move_buffer);
    free(mcts);
}

static bool tree_expand(MCTS* mcts, Node* parent)
{
    assert(mcts != NULL);
    assert(parent != NULL);

    size_t nmoves = mcts->game->get_moves(
        mcts->state_buffer,
        mcts->move_buffer
    );

    Node* prev_child = NULL;
    for (size_t i = 0; i < nmoves; i++)
    {
        void* move = (char*)mcts->move_buffer + (mcts->game->move_size * i);
        Node* node = pool_alloc(mcts->node_pool);
        if (!node)
        {
            return false;
        }

        node->parent = parent;
        node->first_child = NULL;
        node->next_sibling = NULL;
        node->visits = 0;
        memset(
            node_reward_vector(node),
            0, 
            sizeof(double) * mcts->game->num_players
        );
        memcpy(
            node_move(node, mcts->game->num_players),
            move,
            mcts->game->move_size
        );

        // Link up node into tree
        if (!parent->first_child)
        {
            parent->first_child = node;
        }
        if (prev_child)
        {
            prev_child->next_sibling = node;
        }
        prev_child = node;
    }

    return true;
}

static bool tree_is_leaf(const Node* node)
{
    assert(node != NULL);
    return node->first_child == NULL;
}

static bool simulate(MCTS* mcts, double* out_rewards, size_t num_players)
{
    const size_t MAX_SIM_STEPS = 100000;
    assert(mcts != NULL);

    void* state = mcts->state_buffer;
    
    size_t nmoves = 0;
    while(!mcts->game->is_terminal(state))
    {
        if (nmoves > MAX_SIM_STEPS)
        {
            fprintf(
                stderr,
                "simulate exceeded MAX_SIM_STEPS\n"
            );
            return false;
        }
        mcts->game->random_playout_step(state, mcts->move_buffer);
        mcts->game->apply_move(state, mcts->move_buffer);
        nmoves++;
    }

    mcts->game->get_rewards(state, out_rewards);

    return true;
}

static void backpropagate(
    Node* node,
    const double* reward_vector,
    size_t num_players
)
{
    while (node)
    {
        node->visits++;
        for (size_t i = 0; i < num_players; i++)
        {
            node_reward_vector(node)[i] += reward_vector[i];
        }
        node = node->parent;
    }
}

static Node* reset_selection(MCTS* mcts)
{
    assert(mcts != NULL);

    game_copy_state(mcts->game, mcts->state_buffer, mcts->game_state);
    return mcts->root_iter;
}

static Node* select_child(MCTS* mcts, const Node* from)
{
    assert(mcts != NULL);
    assert(from != NULL);
    assert(from->first_child != NULL);

    size_t turn = mcts->game->get_turn(mcts->state_buffer);

    Node* selected = NULL;
    double max_ucb = -DBL_MAX;
    for (Node* child = from->first_child; child != NULL; child = child->next_sibling)
    {
        if (child->visits == 0)
        {
            selected = child;
            break;
        }
        double exploit = node_reward_vector(child)[turn] / child->visits;
        double explore = sqrt(2) * sqrt(log(from->visits) / child->visits);
        double ucb = exploit + explore;
        if (ucb > max_ucb)
        {
            max_ucb = ucb;
            selected = child;
        }
    }

    mcts->game->apply_move(
        mcts->state_buffer,
        node_move(selected, mcts->game->num_players)
    );

    return selected;
}
