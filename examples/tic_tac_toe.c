#include "../include/mcts.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

const size_t MAX_MOVES = 9;

enum Square {
    EMPTY,
    X,
    O
};

struct Move {
    int index;
};

struct TicTacToeState {
    enum Square board[9];
    char turn;
};

static void   apply_move(void* state, const void* move);
static size_t get_moves(const void* state, void* out_moves);
static void   random_playout_step(const void* state, void* out_move);
static void   get_rewards(const void* state, double* out_vector);
static bool   is_terminal(const void* state);
static bool   are_moves_equal(const void* a, const void* b);
static size_t get_turn(const void* state);

static bool   is_winner(const struct TicTacToeState* state);
static void   print_board(const struct TicTacToeState* state);

MCTSGame tictactoe = {
    .move_size = sizeof(struct Move),
    .max_moves = MAX_MOVES,
    .state_size = sizeof(struct TicTacToeState),
    .num_players = 2,
    .initial_pool_size = 6000,
    .apply_move = &apply_move,
    .get_moves = &get_moves,
    .random_playout_step = &random_playout_step,
    .get_rewards = &get_rewards,
    .is_terminal = &is_terminal,
    .are_moves_equal = &are_moves_equal,
    .get_turn = &get_turn,
};

int main()
{
    srand(time(NULL));
    MCTS* mcts = mcts_create(&tictactoe);
    if (!mcts)
    {
        printf("Could not create MCTS\n");
        return 1;
    }

    struct TicTacToeState state = {
        .board = { EMPTY },
        .turn = 0
    };

    while (!is_terminal(&state)) {
        print_board(&state);
        if (state.turn == 0) {
            int index;
            printf("\nEnter square 0-8: ");
            scanf("%d", &index);
            struct Move m = {
                .index = index,
            };
            apply_move(&state, &m);
            mcts_advance(mcts, &m);
        } else {
            struct Move* m = mcts_search(mcts, &state, 5);
            apply_move(&state, m);
            mcts_advance(mcts, m);
            printf("Computer goes %d\n", m->index);
        }
        printf("\n");
    }

    print_board(&state);

    if (is_winner(&state))
    {
        if (state.turn == 1) // X just went
        {
            printf("Human player wins!\n");
        }
        else 
        {
            printf("Computer player wins!\n");
        }
    }
    else
    {
        printf("Cat game!\n");
    }

    mcts_destroy(mcts);
    return 0;
}

static void apply_move(void* state, const void* move)
{
    struct Move* _move = (struct Move*)move;
    struct TicTacToeState* _state = (struct TicTacToeState*)state;
    if (_move->index >= 0 && _move->index < 9) {
        if (_state->turn == 0) {
            _state->board[_move->index] = X;
            _state->turn = 1;
        } else {
            _state->board[_move->index] = O;
            _state->turn = 0;
        }
    }
}

static size_t get_moves(const void* state, void* out_moves)
{
    const struct TicTacToeState* _state = (struct TicTacToeState*)state;
    struct Move* moves = (struct Move*)out_moves;
    size_t nmoves = 0;
    for (size_t i = 0; i < 9; i++) {
        if (_state->board[i] == EMPTY) {
            moves[nmoves++].index = i;
        }
    }
    return nmoves;
}

static void random_playout_step(const void* state, void* out_move)
{
    struct Move moves[MAX_MOVES];
    size_t nmoves = get_moves(state, moves);
    size_t index = rand() % nmoves;
    memcpy(out_move, &moves[index], sizeof(struct Move));
}

static bool is_winner(const struct TicTacToeState* state)
{
    const enum Square* b = state->board;
    // left to right diagonal
    if (b[0] != EMPTY && b[0] == b[4] && b[4] == b[8]) {
        return true;
    }
    // right to left diagonal
    if (b[2] != EMPTY && b[2] == b[4] && b[4] == b[6]) {
        return true;
    }
    
    // top row
    if (b[0] != EMPTY && b[0] == b[1] && b[1] == b[2]) {
        return true;
    }

    // middle row
    if (b[3] != EMPTY && b[3] == b[4] && b[4] == b[5]) {
        return true;
    }

    // bottom row
    if (b[6] != EMPTY && b[6] == b[7] && b[7] == b[8]) {
        return true;
    }

    // left column
    if (b[0] != EMPTY && b[0] == b[3] && b[3] == b[6]) {
        return true;
    }

    // middle column
    if (b[1] != EMPTY && b[1] == b[4] && b[4] == b[7]) {
        return true;
    }

    // last column
    if (b[2] != EMPTY && b[2] == b[5] && b[5] == b[8]) {
        return true;
    }

    return false;
}

static void get_rewards(const void* state, double* out_vector)
{
    const struct TicTacToeState* _state = state;
    // cat game
    out_vector[0] = out_vector[1] = 0.0f;

    if (is_winner(_state))
    {
        if (_state->turn == 1) {        // X just moved
            out_vector[0] = 1;
            out_vector[1] = -1;
        } else {                        // O just moved
            out_vector[0] = -1;
            out_vector[1] = 1;
        }
    }
}

static bool is_terminal(const void* state)
{
    if (is_winner(state))
    {
        return true;
    }
    const struct TicTacToeState* _state = (struct TicTacToeState*)state;
    for (size_t i = 0; i < 9; i++) {
        if (_state->board[i] == EMPTY) {
            return false;
        }
    }
    return true;
}

static bool are_moves_equal(const void* a, const void* b)
{
    return ((struct Move*)a)->index == ((struct Move*)b)->index;
}

static size_t get_turn(const void* state)
{
    return ((struct TicTacToeState*)state)->turn;
}

static void print_board(const struct TicTacToeState* state)
{
    struct TicTacToeState* _state = (struct TicTacToeState*)state;
    for (size_t i = 0; i < 9; i++) {
        char c = '_';
        if (_state->board[i] == X) {
            c = 'X';
        } else if (_state->board[i] == O) {
            c = 'O';
        }
        printf("%c ", c);
        if ((i + 1) % 3 == 0) {
            printf("\n");
        }
    }
}