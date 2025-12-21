/*

*******************************************
************** DOCUMENTATION **************
*******************************************

********** CALCULATION PROCESS **********
The engine stores future positions (futures) in a list (actually multiple lists).
These lists represent the board, miscellanceous info, movefrom, moveto, node parent index, number of children,
child indices, eval, and score.
The lists are O(1) to add at the end because they have a capacity nodeCap greater than numNodes.
The lists resize according to the formula in addEmptyFutureToList().

The evaluation process is a repetition of only one operation which contains multiple steps. (There is technically also another operation:
if we are past the evaluation time limit or the user types something in the case of the analysis board, stop evaluating.)
This operation is conducted as follows:
- Pop the first future index from the heap of futures to check next, call it P for parent.
- Find all moves, create new futures to be P's children corresponding to the resulting positions and evaluate them using the evalBoards, checks and attacks, and any other metrics.
- Add those futures to the futures list.
- Get P's score by summing eval differences from P up to the root.
- Get the scores of P's children as eval differences from P.
- Add those futures to the heap based on their scores.

To stop evaluating, we must first get the final evaluation of every node (changing only the internal nodes' evals).
This process gives an up-to-date eval for every single node in the tree and we can keep evaluating from that point at any time.
For every node X, depth-first:
- Update X's eval as the min (if black) or max (if white) of the evals of X's children.



Why this calculation process works:
First of all, assume no zugzwangs. So at least one move increases the eval calculated directly from the board (not considering children) if white or decreases if black.
The eval of a node at the END of the evaluation must be the maximum child node eval if white or minimum if black, for EVERY node in the tree.
So doing exactly that at the end ensures the result is accurate.



********** EVAL AND SCORE VISUALIZATION **********
white's turn -> max of result evals                                             18
black's turn -> min of result evals                         14                                    18
etc.                                           14           22        21                 28                 18
                                         11    14  12       22      21  20           13  20  28        18       13
                                       11  19             22  29        20                   28      24  18   20  13

                                                                                0
                                                            4                                     0
                                               4            12        11                 10                 0
                                         7     4   6        12      11  12           25  18  10        0        5
                                       7   15             12  19        12                   10      6   0    12  5


********** BOARD ENCODING **********
Each board is a char* of 64 chars representing the pieces at each board square.
Index meanings: 0 = a1, 1 = b1, 2 = c1, ..., 63 = h8.
Value meanings:
-1 = no piece, 0 = white pawn, 1 = white knight, 2 = white bishop,
3 = white rook, 4 = white queen, 5 = white king, 6 = black pawn,
7 = black knight, 8 = black bishop, 9 = black rook, 10 = black queen, 11 = black king

********** MISC ENCODING **********
Each miscellaneous descriptor is a char* of MISC_SIZE = 11 chars representing additional information
about any board state that is not determinable using just the board.
Index = description (values)
0 = white's kingside castling ability (0 or 1)
1 = white's queenside castling ability (0 or 1)
2 = black's kingside castling ability (0 or 1)
3 = black's queenside castling ability (0 or 1)
4 = en passant column (file) availability (-1 through 7)
    - This only indicates the file of a pawn that has just moved two squares.
5 = 50-move rule counter (0-100, number of turns (half-moves) since a pawn move or capture)
6 = white's king square (0-63) or last square king was on before captured
7 = black's king square (0-63) or last square king was on before captured
8 = movefrom square # (0-63)
9 = moveto square # (0-127)
10 = player whose turn it is in this position (player who will make moves to produce child nodes) (0 for white or 1 for black)

Be aware that if a castling ability is 1, the engine assumes the king and rook are
at their starting squares and will move whatever pieces are there. This is always
true in a full game of chess, but modifying the castling ability will produce bugs.

********** MOVE ENCODING **********
Every move is identified solely by the movefrom (source) square
and moveto (destination) square, which are chars from 0-63 (typically).
0 = a1, 1 = b1, 2 = c1, ..., 63 = h8.

To indicate castling, the movefrom and moveto squares are the source
and destination of the king that moves.
To indicate en passant, the movefrom and moveto squares are the source
and destination of the pawn that moves.
To indicate a pawn promotion, the movefrom square is the source of the
pawn that moves. The moveto square is a value between 64 and 127 that
indicates both the destination square and the piece that pawn promotes to.
- 64-71 = white knight
- 72-79 = white bishop
- 80-87 = white rook
- 88-95 = white queen
- 96-103 = black knight
- 104-111 = black bishop
- 112-119 = black rook
- 120-127 = black queen
The moveto value mod 8 is the column (file) number of the pawn's destination.


********** GAME PROCESSES **********

1-PLAYER:
    Setup the board.
    While the game is in play:
        If it is the player's turn:
            Receive and parse a player's move.
            If the player chose to quit, end the game.
        Else:
            Evaluate the current position.
            Choose a move based on possible moves and engine difficulty.
        Play the chosen move.
        Get the number of legal moves and whether the opponent is in check now.
        Based on that info, if checkmate or stalemate is present, end the game.

2-PLAYER:
    Setup the board.
    While the game is in play:
        Receive and parse a player's move.
        If the player chose to quit, end the game.
        Play the chosen move.
        Get the number of legal moves and whether the opponent is in check now.
        Based on that info, if checkmate or stalemate is present, end the game.

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <conio.h>
#include <windows.h>

// Pointer allocation macros

// This free wrapper crashes if a non-null pointer is unallocated, so all unallocated pointers must ALWAYS be null.
// free(p) frees iff non-null.
#define clear(p) free(p); p = NULL;

// Crash after a memory allocation failure.
void crash() {
    printf("Could not allocate memory.\n");
    exit(1);
}


// Maximum size of an input line on console before causing an error.
#define MAX_LINE_SIZE 1000

// Movefrom and moveto of the root node (should not matter)
#define DEFAULT_MOVEFROMTO -1

// Global settings
bool usePlusesOnEvalNumbers = 1;
bool unicodeEnabled = 0;
bool reverseWhiteBlackLetters = 0;
char asciiSpaceValues[] = { 32, 42, 46 }; // space, *, .
char asciiSpaceOption = 2; // which of the values is being used
bool showBoardCoordinates = 1;
bool evaluationPrintChoices = 1;
// Settings that affect the actual evaluation algorithm.
double evaluationTimeLimitMin = 1.0; // seconds
double evaluationTimeLimitMax = 1.0; // seconds
double evaluationTimeLimitAnalysis = 1.0; // seconds
int evaluationTimeMeasurementInterval = 20; // compute current time every X position examinations
int evaluationDepthLimit = 30; // 0 means do not add root's children to heap, etc.
int evaluationNodeLimit = 10000000; // Stop if the number of nodes in the tree is close (within 500) at any moment.


enum drawSettings {
    NO_DRAWS = 0,
    ASK = 1,
    FORCE = 2
};
char drawSetting = ASK;



bool keyPrev[256];
bool keyCurr[256];

char* lastLine;

#define MAX_MOVE_STRING_LENGTH 10
char* moveString;
int moveStringLength = 0;


// Analysis board
char* analysisBoard;
char* analysisMiscs;

// All previous board states in this game including the current one.
char** gameBoardHistory;
char** gameMiscsHistory;
int gameLength = 0; // Number of positions in this game (length of gameBoardHistory and gameMiscsHistory)

// Board state used only internally to find all semilegal and legal piece moves (and sometimes execute them).
int currentExaminingNode;
char* B; // Position pointers for short code, they change every time we resize the position array (and potentially change the pointer locations with realloc())
char* M;

// Calculation stats tracked during one calculation.
int calcNumNodesAddedTree = 0; // Number of total positions found.
int calcNumNodesAddedHeap = 0; // Number of positions queued (total found minus checkmates/stalemates).
int calcNumNodesRemovedHeap = 0; // Number of positions examined.
// Average number of moves in a position is calculable from the above three stats.

int calcNumStalematesFound = 0;
int calcNumWhiteWinsFound = 0;
int calcNumBlackWinsFound = 0;
int calcNumNormalsFound = 0;

int* calcNumNodesAddedTreeDepth; // Number of total positions found at each depth.
int* calcNumNodesAddedHeapDepth; // Number of positions queued (total found minus checkmates/stalemates) at each depth.
int* calcNumNodesRemovedHeapDepth; // Number of positions examined at each depth.



// Heap of indices in futures to evaluate next.
int* futuresHeapValue = NULL; // The list index of this future in the futures tree
double* futuresHeapScore = NULL; // The score of this future used for priority
int futuresHeapLength;
int futuresHeapCap;
double futuresHeapCapMultiplier = 1.5;
int futuresHeapCapAdder = 10;

// Futures (nodes) in tree.
int nodeCap = 0;
double nodeCapMultiplier = 1.5;
int nodeCapAdder = 10;
int numNodes = 0;
int* nodeParentIndex;
int* nodeNumChildren;
int* nodeChildStartIndex; // all children are at consecutive indices in the tree (node arrays)
char* nodeBoard;
char* nodeMiscs;
double* nodeEval;

// Sorted indices and evals of the children of node 0 (moves in this position), globalized for easy reference.
int* nodeIndexSorted;
double* nodeEvalSorted;

// An arbitrary value that should never be checked by the program.
#define UNDEFINED -1


#define MISC_SIZE 12
enum misc {
    wKINGSIDE_CASTLE = 0,
    wQUEENSIDE_CASTLE = 1,
    bKINGSIDE_CASTLE = 2,
    bQUEENSIDE_CASTLE = 3,
    EN_PASSANT_FILE = 4,
    FIFTY_MOVE_COUNTER = 5,
    wKING_SQUARE = 6,
    bKING_SQUARE = 7,
    SQUARE_FROM = 8,
    SQUARE_TO = 9,
    PLAYER_TURN = 10,
    GAME_STATE = 11
};

enum gameStates {
    NORMAL = 0,
    WHITE_WIN = 1,
    BLACK_WIN = 2,
    DRAW = 3
};

#define ROOT_SCORE 0.0
#define WHITE_WINS_EVAL 1e9 // The eval of a White checkmate position.
#define BLACK_WINS_EVAL -1e9 // The eval of a Black checkmate position.
#define DRAW_EVAL 0 // The eval of a stalemate position.
#define WHITE_WINS_EVAL_THRESHOLD 1e8 // The minimum eval to be considered a forced mate by White.
#define BLACK_WINS_EVAL_THRESHOLD -1e8 // The maximum eval to be considered a forced mate by Black.
#define EVAL_FORCED_MATE_INCREMENT 1000 // The difference in eval between a checkmate and mate-in-one, etc.


#define NUM_PIECES 12
enum pieces {
    EMPTY = -1,
    wPAWN = 0,
    wKNIGHT = 1,
    wBISHOP = 2,
    wROOK = 3,
    wQUEEN = 4,
    wKING = 5,
    bPAWN = 6,
    bKNIGHT = 7,
    bBISHOP = 8,
    bROOK = 9,
    bQUEEN = 10,
    bKING = 11
};

enum playerTurn {
    WHITE = 0,
    BLACK = 1
};

double** evalBoards;


char startingPieceCounts[NUM_PIECES] = { 8, 2, 2, 2, 1, 1, 8, 2, 2, 2, 1, 1 };
double piecePointValues[NUM_PIECES] = { 1.0, 3.0, 3.3, 5.0, 9.0, 0.0, -1.0, -3.0, -3.3, -5.0, -9.0, -0.0 };
double pieceEdgeContribution[NUM_PIECES] = { 0.05, 0.08, 0.07, 0.07, 0.15, 0.0, -0.05, -0.08, -0.07, -0.07, -0.15, -0.0 }; // How much moving a piece 1 square changes eval.

// First row is rank 1, etc.
char startingBoard[64] = {
    3, 1, 2, 4, 5, 2, 1, 3,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    6, 6, 6, 6, 6, 6, 6, 6,
    9, 7, 8, 10, 11, 8, 7, 9,
};

char startingMiscs[MISC_SIZE] = { 1, 1, 1, 1, -1, 0, 4, 60, UNDEFINED, UNDEFINED, WHITE, NORMAL };

// Data for playing against engine
char playerRole = BLACK;

// Difficulty range for engine.
#define DIFFICULTY_MIN 0
#define DIFFICULTY_MAX 9


unsigned long long randPrev = 0x940b19e3fd06b7a5;
unsigned long long randState = 0x1e964d81c33fa402;

// Seed the RNG.
void setSeed(unsigned long long seed) {
    randPrev = seed;
    randState = seed;
}

// Seed the RNG with a value based on the current time.
void seedRandom() {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    unsigned long long s = (unsigned long long)now.tv_sec;
    unsigned long long ns = (unsigned long long)now.tv_nsec;
    unsigned long long seed = s * 0xb619280e4fa733c5 + ns * 0x442c04f61ea63cb7;
    setSeed(seed);
}

// Get a random u64.
unsigned long long random() {
    randPrev = (randPrev * 0xa63e40147c582b49 + (randState += 0x51f84b2308a7d929)) * 0x681ac9427d5fe8b3;
    return randPrev;
}

// Clear the console window.
void clearConsole() {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    system("clear");
#endif

#if defined(_WIN32) || defined(_WIN64)
    //system("cls");
#endif
}

// Initialize the engine by configuring settings and allocating position memory.
void init(int n) {

    // We may change this to be more than the given setting if we want extra nodes.
    int limit = evaluationNodeLimit;

    // Allocate memory in the heap while making it empty.
    futuresHeapValue = (int*)realloc(futuresHeapValue, limit * 4);
    futuresHeapScore = (double*)realloc(futuresHeapScore, limit * 8);
    futuresHeapLength = 1;
    futuresHeapCap = limit;

    // Allocate memory in the tree while making it empty.
    nodeParentIndex = (int*)realloc(nodeParentIndex, limit * 4);
    nodeNumChildren = (int*)realloc(nodeNumChildren, limit * 4);
    nodeChildStartIndex = (int*)realloc(nodeChildStartIndex, limit * 4);
    nodeBoard = (char*)realloc(nodeBoard, limit * 64);
    nodeMiscs = (char*)realloc(nodeMiscs, limit * MISC_SIZE);
    nodeEval = (double*)realloc(nodeEval, limit * 8);
    numNodes = 0;
    nodeCap = limit;
}

// Add an empty future to the futures list to be filled immediately after.
void addEmptyFutureToList() {

    numNodes++;

    if (numNodes > nodeCap) {
        // Resize the futures list to the next appropriate size. This should only be needed if we don't allocate the max at the start.
        nodeCap = (int)((double)nodeCap * nodeCapMultiplier + (double)nodeCapAdder);

        size_t size = nodeCap;
        
        nodeParentIndex = (int*)realloc(nodeParentIndex, size * 4);
        nodeNumChildren = (int*)realloc(nodeNumChildren, size * 4);
        nodeChildStartIndex = (int*)realloc(nodeChildStartIndex, size * 4);
        nodeBoard = (char*)realloc(nodeBoard, size * 64);
        nodeMiscs = (char*)realloc(nodeMiscs, size * MISC_SIZE);
        nodeEval = (double*)realloc(nodeEval, size * 8);

        B = nodeBoard + currentExaminingNode * 64;
        M = nodeMiscs + currentExaminingNode * MISC_SIZE;
    }
}

// Setup the board to the starting game position given all the references of the position data.
void setupBoard() {
    for (int i = 0; i < gameLength; i++) {
        clear(gameBoardHistory[i]);
        clear(gameMiscsHistory[i]);
    }

    gameLength = 1;

    gameBoardHistory = (char**)realloc(gameBoardHistory, sizeof(char*));
    gameMiscsHistory = (char**)realloc(gameMiscsHistory, sizeof(char*));

    gameBoardHistory[0] = (char*)calloc(64, 1);
    gameMiscsHistory[0] = (char*)calloc(MISC_SIZE, 1);
    gameMiscsHistory[1] = 0;

    for (int i = 0; i < 64; i++) {
        gameBoardHistory[0][i] = startingBoard[i];
    }
    for (int i = 0; i < MISC_SIZE; i++) {
        gameMiscsHistory[0][i] = startingMiscs[i];
    }
}

// Given a full board state, compute its eval using the evalBoards.
double computeEval(char* b, char* m) {
    double o = 0.0f;

    for (char i = 0; i < 64; i++) {
        if (b[i] != EMPTY) {
            // TODO: DELETE THIS
            //if (b[i] < 0 || b[i] >= 12) {
            //    printf("%i\n", b[i]);
            //}
            o += evalBoards[b[i]][i];
        }
    }

    // TODO: Add other eval criteria.


    return o;
}

// Calculate the target s and ns for t seconds from now.
void calcTime(double t, unsigned int* s, unsigned int* ns) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);

    // Convert t into seconds and nanoseconds.
    *s = (unsigned int)t;
    *ns = (unsigned int)((double)(t - (double)((int)t)) * 1000000000.0);

    *s += (unsigned int)now.tv_sec;
    *ns += (unsigned int)now.tv_nsec;

    // Carry
    if (*ns >= 1000000000) {
        *ns -= 1000000000;
        (*s)++;
    }
}

// Return true if the piece at square x on the calculating board is white.
bool isWhite(char x) {
    char r = B[x];
    return r >= 0 && r <= 5;
}

// Return true if the piece at square x on the calculating board is black.
bool isBlack(char x) {
    char r = B[x];
    return r >= 6 && r <= 11;
}

// SQUARE_FROM, SQUARE_TO, and PLAYER_TURN in the given parameter m must be set. 
// Play a given move on the given board and update all OTHER miscs.
void playMove(char* b, char* m) {
    char moveFrom = m[SQUARE_FROM];
    char moveTo = m[SQUARE_TO];

    // Get the type of piece being promoted to or negative if no promotion.
    char promotion = (moveTo - 64) / 8;

    // Set moveTo to the true destination square.
    if (moveTo >= 96) {
        moveTo = moveTo % 8;
    }
    else if (moveTo >= 64) {
        moveTo = 56 + (moveTo % 8);
    }

    char rf = moveFrom / 8, cf = moveFrom % 8, rt = moveTo / 8, ct = moveTo % 8;
    char p = b[moveFrom];
    char q = b[moveTo];

    if (m[FIFTY_MOVE_COUNTER] < 100) m[FIFTY_MOVE_COUNTER]++;

    // If capturing, reset 50-move counter.
    bool capture = 0;
    if (q != EMPTY) {
        m[FIFTY_MOVE_COUNTER] = 0;
        capture = 1;
    }

    m[EN_PASSANT_FILE] = -1;

    // Make default move at beginning - it will be overridden by pawn promotions.
    b[moveTo] = p;
    b[moveFrom] = EMPTY;

    switch (p) {
    case wPAWN:
        m[FIFTY_MOVE_COUNTER] = 0; // 50-move rule
        if (rf == 1 && rt == 3) { // en passant availability
            m[EN_PASSANT_FILE] = ct;
        }
        else if (promotion > -1) { // white promotion
            b[moveTo] = promotion + 1;
        }
        else if (rf == 4 && !capture && cf != ct) { // white en passant
            b[moveTo - 8] = EMPTY;
        }
        break;
    case bPAWN:
        m[FIFTY_MOVE_COUNTER] = 0; // 50-move rule
        if (rf == 6 && rt == 4) { // en passant availability
            m[EN_PASSANT_FILE] = ct;
        }
        else if (promotion > -1) { // black promotion
            b[moveTo] = promotion + 3;
        }
        else if (rf == 3 && !capture && cf != ct) { // black en passant
            b[moveTo + 8] = EMPTY;
        }
        break;
    case wKING:
        m[wKINGSIDE_CASTLE] = 0;
        m[wQUEENSIDE_CASTLE] = 0;
        m[wKING_SQUARE] = moveTo;
        if (moveFrom == 4 && moveTo == 6) { // WK
            b[5] = wROOK; b[7] = EMPTY; m[wKINGSIDE_CASTLE] = 0;
        }
        else if (moveFrom == 4 && moveTo == 2) { // WQ
            b[3] = wROOK; b[0] = EMPTY; m[wQUEENSIDE_CASTLE] = 0;
        }
        break;
    case bKING:
        m[bKINGSIDE_CASTLE] = 0;
        m[bQUEENSIDE_CASTLE] = 0;
        m[bKING_SQUARE] = moveTo;
        if (moveFrom == 60 && moveTo == 62) { // BK
            b[61] = bROOK; b[63] = EMPTY; m[bKINGSIDE_CASTLE] = 0;
        }
        else if (moveFrom == 60 && moveTo == 58) { // BQ
            b[59] = bROOK; b[56] = EMPTY; m[bQUEENSIDE_CASTLE] = 0;
        }
        break;
    case wROOK:
        if (moveFrom == 7) {
            m[wKINGSIDE_CASTLE] = 0;
        }
        else if (moveFrom == 0) {
            m[wQUEENSIDE_CASTLE] = 0;
        }
        break;
    case bROOK:
        if (moveFrom == 63) {
            m[bKINGSIDE_CASTLE] = 0;
        }
        else if (moveFrom == 56) {
            m[bQUEENSIDE_CASTLE] = 0;
        }
        break;
    }


    if (b[moveTo] < -1 || b[moveTo] >= 12) {
        printf("Mid: %i %i %i\n", currentExaminingNode, moveTo, b[moveTo]);
    }
}

// Return whether the given king is being attacked on the board given the king's square.
bool kingNotInCheck(char* b, char x, bool isBlack) {
    char r = x / 8, c = x % 8;

    // Offset the attacker piece types by 6 if king is white (attackers are black).
    char z = isBlack ? 0 : 6;
    
    // Check pawn attacks to this king.
    if (isBlack) {
        if (r > 0 && c > 0 && b[x - 9] == z) return 0;
        if (r > 0 && c < 7 && b[x - 7] == z) return 0;
    }
    else {
        if (r < 7 && c > 0 && b[x + 7] == z) return 0;
        if (r < 7 && c < 7 && b[x + 9] == z) return 0;
    }

    // Check knight, king attacks to this king.
    if (c > 0) {
        if (b[x - 1] == z + 5) return 0;
    }
    if (c < 7) {
        if (b[x + 1] == z + 5) return 0;
    }
    if (r > 0) {
        if (c > 1) {
            if (b[x - 10] == z + 1) return 0;
        }
        if (c < 6) {
            if (b[x - 6] == z + 1) return 0;
        }
        if (c > 0) {
            if (b[x - 9] == z + 5) return 0;
        }
        if (c < 7) {
            if (b[x - 7] == z + 5) return 0;
        }
        if (b[x - 8] == z + 5) return 0;
    }
    if (r < 7) {
        if (c > 1) {
            if (b[x + 6] == z + 1) return 0;
        }
        if (c < 6) {
            if (b[x + 10] == z + 1) return 0;
        }
        if (c > 0) {
            if (b[x + 7] == z + 5) return 0;
        }
        if (c < 7) {
            if (b[x + 9] == z + 5) return 0;
        }
        if (b[x + 8] == z + 5) return 0;
    }
    if (r > 1) {
        if (c > 0) {
            if (b[x - 17] == z + 1) return 0;
        }
        if (c < 7) {
            if (b[x - 15] == z + 1) return 0;
        }
    }
    if (r < 6) {
        if (c > 0) {
            if (b[x + 15] == z + 1) return 0;
        }
        if (c < 7) {
            if (b[x + 17] == z + 1) return 0;
        }
    }

    // Check diagonal attacks to this king.
    char l = r < c ? r : c;
    l = x - 9 * l;
    for (char X = x - 9; X >= l; X -= 9) {
        char p = b[X];
        if (p == z + 2 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x - 7 * l;
    for (char X = x - 7; X >= l; X -= 7) {
        char p = b[X];
        if (p == z + 2 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }
    r = 7 - r;
    l = r < c ? r : c;
    l = x + 9 * l;
    for (char X = x + 9; X <= l; X += 9) {
        char p = b[X];
        if (p == z + 2 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x + 7 * l;
    for (char X = x + 7; X <= l; X += 7) {
        char p = b[X];
        if (p == z + 2 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }

    // Check orthogonal attacks to this king.
    for (char X = x - 8; X >= 0; X -= 8) {
        char p = b[X];
        if (p == z + 3 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }
    for (char X = x + 8; X < 64; X += 8) {
        char p = b[X];
        if (p == z + 3 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }
    l = (x / 8) * 8;
    for (char X = x - 1; X >= l; X--) {
        char p = b[X];
        if (p == z + 3 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }
    l += 8;
    for (char X = x + 1; X < l; X++) {
        char p = b[X];

        if (p == z + 3 || p == z + 4) return 0;
        if (p != EMPTY) break;
    }

    return 1;
}

// Execute an already known to be semilegal move while calculating, creating a new future position ONLY if not moving into check.
// This function is also called when finding all legal moves to determine the legal moves outside of a position evaluation and to determine if stalemate happens.
void move(char moveFrom, char moveTo) {

    // Add a node to the futures tree containing this new legal position.
    addEmptyFutureToList();
    
    // Only doing this so I don't have to pass parameters or set globals.
    char playerTurn = M[PLAYER_TURN];
    char newPlayerTurn = 1 - playerTurn;

    int otherKingSquare = playerTurn ? M[wKING_SQUARE] : M[bKING_SQUARE];

    int trueMoveto = moveTo;
    if (trueMoveto >= 128) trueMoveto %= 8;
    if (trueMoveto >= 64) trueMoveto = 56 + (trueMoveto % 8);

    int lastNode = numNodes - 1;

    // Find the new node position in the array.
    char* board = nodeBoard + lastNode * 64;
    char* miscs = nodeMiscs + lastNode * MISC_SIZE;
    
    for (int i = 0; i < 64; i++) {
        board[i] = B[i];
    }
    for (int i = 0; i < MISC_SIZE; i++) {
        miscs[i] = M[i];
    }

    double eval = 0.0;

    miscs[SQUARE_FROM] = moveFrom;
    miscs[SQUARE_TO] = moveTo;
    miscs[PLAYER_TURN] = newPlayerTurn;

    playMove(board, miscs);

    // If moving to other king, we define this to be a guaranteed checkmate.
    if (trueMoveto == otherKingSquare) {
        miscs[GAME_STATE] = playerTurn == BLACK ? BLACK_WIN : WHITE_WIN;
        eval = playerTurn == BLACK ? BLACK_WINS_EVAL : WHITE_WINS_EVAL;
    }
    else {
        // Evaluate what the position would be after moving.
        eval = computeEval(board, miscs);
    }

    // If moving into check, decrement the node count. Safe because new node will just contain junk values.
    int newKingSquare = playerTurn ? miscs[bKING_SQUARE] : miscs[wKING_SQUARE];
    if (!kingNotInCheck(board, newKingSquare, playerTurn)) {
        numNodes--;
        return;
    }

    calcNumNodesAddedTree++;

    nodeParentIndex[lastNode] = currentExaminingNode;
    nodeNumChildren[lastNode] = 0;
    nodeChildStartIndex[lastNode] = UNDEFINED;
    nodeEval[lastNode] = eval;

    // Add this new node to the parent's children in the futures tree.
    int numChildren = nodeNumChildren[currentExaminingNode] + 1;
    nodeNumChildren[currentExaminingNode] = numChildren;
}

// Make all semilegal moves for a white pawn.
void examineWhitePawn(char x) {
    char r = x / 8, c = x % 8;

    if (r == 6) {
        if (B[56 + c] == EMPTY) { // promoting move
            move(x, 64 + c);
            move(x, 72 + c);
            move(x, 80 + c);
            move(x, 88 + c);
        }
        if (c > 0) { // promoting capture left
            if (isBlack(55 + c)) {
                move(x, 63 + c);
                move(x, 71 + c);
                move(x, 79 + c);
                move(x, 87 + c);
            }
        }
        if (c < 7) { // promoting capture right
            if (isBlack(56 + c)) {
                move(x, 65 + c);
                move(x, 73 + c);
                move(x, 81 + c);
                move(x, 89 + c);
            }
        }
    }
    else if (r < 6) {
        if (B[x + 8] == EMPTY) { // move
            move(x, x + 8);
            if (r == 1 && B[x + 16] == EMPTY) { // move two squares
                move(x, x + 16);
            }
        }
        if (c > 0) { // capture or en passant left
            if (isBlack(x + 7) || (M[EN_PASSANT_FILE] == c - 1 && r == 4)) {
                move(x, x + 7);
            }
        }
        if (c < 7) { // capture or en passant right
            if (isBlack(x + 9) || (M[EN_PASSANT_FILE] == c + 1 && r == 4)) {
                move(x, x + 9);
            }
        }
    }
}

// Make all semilegal moves for a black pawn.
void examineBlackPawn(char x) {
    char r = x / 8, c = x % 8;

    if (r == 1) {
        if (B[0 + c] == EMPTY) { // promoting move
            move(x, 96 + c);
            move(x, 104 + c);
            move(x, 112 + c);
            move(x, 120 + c);
        }
        if (c > 0) { // promoting capture left
            if (isWhite(-1 + c)) {
                move(x, 95 + c);
                move(x, 103 + c);
                move(x, 111 + c);
                move(x, 119 + c);
            }
        }
        if (c < 7) { // promoting capture right
            if (isWhite(1 + c)) {
                move(x, 97 + c);
                move(x, 105 + c);
                move(x, 113 + c);
                move(x, 121 + c);
            }
        }
    }
    else if (r > 1) {
        if (B[x - 8] == EMPTY) { // move
            move(x, x - 8);
            if (r == 6 && B[x - 16] == EMPTY) { // move two squares
                move(x, x - 16);
            }
        }
        if (c > 0) { // capture or en passant left
            if (isWhite(x - 9) || (M[EN_PASSANT_FILE] == c - 1 && r == 3)) {
                move(x, x - 9);
            }
        }
        if (c < 7) { // capture or en passant right
            if (isWhite(x - 7) || (M[EN_PASSANT_FILE] == c + 1 && r == 3)) {
                move(x, x - 7);
            }
        }
    }
}

// Make all semilegal moves for a white knight.
void examineWhiteKnight(char x) {
    char r = x / 8, c = x % 8;

    if (r > 0) {
        if (c > 1) {
            if (!isWhite(x - 10)) {
                move(x, x - 10);
            }
        }
        if (c < 6) {
            if (!isWhite(x - 6)) {
                move(x, x - 6);
            }
        }
    }
    if (r < 7) {
        if (c > 1) {
            if (!isWhite(x + 6)) {
                move(x, x + 6);
            }
        }
        if (c < 6) {
            if (!isWhite(x + 10)) {
                move(x, x + 10);
            }
        }
    }
    if (r > 1) {
        if (c > 0) {
            if (!isWhite(x - 17)) {
                move(x, x - 17);
            }
        }
        if (c < 7) {
            if (!isWhite(x - 15)) {
                move(x, x - 15);
            }
        }
    }
    if (r < 6) {
        if (c > 0) {
            if (!isWhite(x + 15)) {
                move(x, x + 15);
            }
        }
        if (c < 7) {
            if (!isWhite(x + 17)) {
                move(x, x + 17);
            }
        }
    }
}

// Make all semilegal moves for a black knight.
void examineBlackKnight(char x) {
    char r = x / 8, c = x % 8;

    if (r > 0) {
        if (c > 1) {
            if (!isBlack(x - 10)) {
                move(x, x - 10);
            }
        }
        if (c < 6) {
            if (!isBlack(x - 6)) {
                move(x, x - 6);
            }
        }
    }
    if (r < 7) {
        if (c > 1) {
            if (!isBlack(x + 6)) {
                move(x, x + 6);
            }
        }
        if (c < 6) {
            if (!isBlack(x + 10)) {
                move(x, x + 10);
            }
        }
    }
    if (r > 1) {
        if (c > 0) {
            if (!isBlack(x - 17)) {
                move(x, x - 17);
            }
        }
        if (c < 7) {
            if (!isBlack(x - 15)) {
                move(x, x - 15);
            }
        }
    }
    if (r < 6) {
        if (c > 0) {
            if (!isBlack(x + 15)) {
                move(x, x + 15);
            }
        }
        if (c < 7) {
            if (!isBlack(x + 17)) {
                move(x, x + 17);
            }
        }
    }
}

// Make all semilegal moves for a white bishop.
void examineWhiteBishop(char x) {
    char r = x / 8, c = x % 8;

    char l = r < c ? r : c;
    l = x - 9 * l;
    for (char X = x - 9; X >= l; X -= 9) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x - 7 * l;
    for (char X = x - 7; X >= l; X -= 7) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
    r = 7 - r;
    l = r < c ? r : c;
    l = x + 9 * l;
    for (char X = x + 9; X <= l; X += 9) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x + 7 * l;
    for (char X = x + 7; X <= l; X += 7) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
}

// Make all semilegal moves for a black bishop.
void examineBlackBishop(char x) {
    char r = x / 8, c = x % 8;

    char l = r < c ? r : c;
    l = x - 9 * l;
    for (char X = x - 9; X >= l; X -= 9) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x - 7 * l;
    for (char X = x - 7; X >= l; X -= 7) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
    r = 7 - r;
    l = r < c ? r : c;
    l = x + 9 * l;
    for (char X = x + 9; X <= l; X += 9) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x + 7 * l;
    for (char X = x + 7; X <= l; X += 7) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
}

// Make all semilegal moves for a white rook.
void examineWhiteRook(char x) {
    char r = x / 8, c = x % 8;

    for (char X = x - 8; X >= 0; X -= 8) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
    for (char X = x + 8; X < 64; X += 8) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
    char l = r * 8;
    for (char X = x - 1; X >= l; X--) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
    l += 8;
    for (char X = x + 1; X < l; X++) {
        if (isWhite(X)) break;
        move(x, X);
        if (isBlack(X)) break;
    }
}

// Make all semilegal moves for a black rook.
void examineBlackRook(char x) {
    char r = x / 8, c = x % 8;

    for (char X = x - 8; X >= 0; X -= 8) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
    for (char X = x + 8; X < 64; X += 8) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
    char l = r * 8;
    for (char X = x - 1; X >= l; X--) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
    l += 8;
    for (char X = x + 1; X < l; X++) {
        if (isBlack(X)) break;
        move(x, X);
        if (isWhite(X)) break;
    }
}

// Make all semilegal moves for a white queen.
void examineWhiteQueen(char x) {
    examineWhiteBishop(x);
    examineWhiteRook(x);
}

// Make all semilegal moves for a black queen.
void examineBlackQueen(char x) {
    examineBlackBishop(x);
    examineBlackRook(x);
}

// Make all semilegal moves for a white king.
void examineWhiteKing(char x) {
    char r = x / 8, c = x % 8;

    if (r > 0) {
        if (!isWhite(x - 8)) {
            move(x, x - 8);
        }
        if (c > 0) {
            if (!isWhite(x - 9)) {
                move(x, x - 9);
            }
        }
        if (c < 7) {
            if (!isWhite(x - 7)) {
                move(x, x - 7);
            }
        }
    }
    if (r < 7) {
        if (!isWhite(x + 8)) {
            move(x, x + 8);
        }
        if (c > 0) {
            if (!isWhite(x + 7)) {
                move(x, x + 7);
            }
        }
        if (c < 7) {
            if (!isWhite(x + 9)) {
                move(x, x + 9);
            }
        }
    }
    if (c > 0) {
        if (!isWhite(x - 1)) {
            move(x, x - 1);
        }
    }
    if (c < 7) {
        if (!isWhite(x + 1)) {
            move(x, x + 1);
        }
    }

    // kingside castling
    if (M[wKINGSIDE_CASTLE] &&
        B[4] == wKING && B[5] == EMPTY && B[6] == EMPTY && B[7] == wROOK) {
        if (kingNotInCheck(B, 4, 0)) {
            B[4] = EMPTY;
            B[5] = wKING;
            if (kingNotInCheck(B, 5, 0)) {
                B[5] = EMPTY;
                B[6] = wKING;
                if (kingNotInCheck(B, 6, 0)) { // optionally check moving into check before executing
                    B[6] = EMPTY;
                    B[4] = wKING;
                    move(4, 6);
                }
            }
        }
    }

    // queenside castling
    if (M[wQUEENSIDE_CASTLE] &&
        B[0] == wROOK && B[1] == EMPTY && B[2] == EMPTY && B[3] == EMPTY && B[4] == wKING) {
        if (kingNotInCheck(B, 4, 0)) {
            B[4] = EMPTY;
            B[3] = wKING;
            if (kingNotInCheck(B, 3, 0)) {
                B[3] = EMPTY;
                B[2] = wKING;
                if (kingNotInCheck(B, 2, 0)) { // optionally check moving into check before executing
                    B[2] = EMPTY;
                    B[4] = wKING;
                    move(4, 2);
                }
            }
        }
    }
}

// Make all semilegal moves for a black king.
void examineBlackKing(char x) {
    char r = x / 8, c = x % 8;

    if (r > 0) {
        if (!isBlack(x - 8)) {
            move(x, x - 8);
        }
        if (c > 0) {
            if (!isBlack(x - 9)) {
                move(x, x - 9);
            }
        }
        if (c < 7) {
            if (!isBlack(x - 7)) {
                move(x, x - 7);
            }
        }
    }
    if (r < 7) {
        if (!isBlack(x + 8)) {
            move(x, x + 8);
        }
        if (c > 0) {
            if (!isBlack(x + 7)) {
                move(x, x + 7);
            }
        }
        if (c < 7) {
            if (!isBlack(x + 9)) {
                move(x, x + 9);
            }
        }
    }
    if (c > 0) {
        if (!isBlack(x - 1)) {
            move(x, x - 1);
        }
    }
    if (c < 7) {
        if (!isBlack(x + 1)) {
            move(x, x + 1);
        }
    }

    // kingside castling
    if (M[bKINGSIDE_CASTLE] &&
        B[4] == bKING && B[61] == EMPTY && B[62] == EMPTY && B[63] == bROOK) {
        if (kingNotInCheck(B, 60, 1)) {
            B[60] = EMPTY;
            B[61] = bKING;
            if (kingNotInCheck(B, 61, 1)) {
                B[61] = EMPTY;
                B[62] = bKING;
                if (kingNotInCheck(B, 62, 1)) { // optionally check moving into check before executing
                    B[62] = EMPTY;
                    B[60] = bKING;
                    move(60, 62);
                }
            }
        }
    }

    // queenside castling
    if (M[bQUEENSIDE_CASTLE] &&
        B[56] == bROOK && B[57] == EMPTY && B[58] == EMPTY && B[59] == EMPTY && B[60] == bKING) {
        if (kingNotInCheck(B, 60, 1)) {
            B[60] = EMPTY;
            B[59] = bKING;
            if (kingNotInCheck(B, 59, 1)) {
                B[59] = EMPTY;
                B[58] = bKING;
                if (kingNotInCheck(B, 58, 1)) { // optionally check moving into check before executing
                    B[58] = EMPTY;
                    B[60] = bKING;
                    move(60, 58);
                }
            }
        }
    }
}

// Find and execute all semilegal moves on the calculating position by making deep copies of the position.
void examineAllSemilegalMoves() {

    char playerTurn = M[PLAYER_TURN];

    if (playerTurn == WHITE) {
        for (char x = 0; x < 64; x++) {

            switch (B[x]) {
            case wPAWN:
                examineWhitePawn(x); break;
            case wKNIGHT:
                examineWhiteKnight(x); break;
            case wBISHOP:
                examineWhiteBishop(x); break;
            case wROOK:
                examineWhiteRook(x); break;
            case wQUEEN:
                examineWhiteQueen(x); break;
            case wKING:
                examineWhiteKing(x); break;
            }
        }
    }
    else {
        for (char x = 0; x < 64; x++) {

            switch (B[x]) {
            case bPAWN:
                examineBlackPawn(x); break;
            case bKNIGHT:
                examineBlackKnight(x); break;
            case bBISHOP:
                examineBlackBishop(x); break;
            case bROOK:
                examineBlackRook(x); break;
            case bQUEEN:
                examineBlackQueen(x); break;
            case bKING:
                examineBlackKing(x); break;
            }
        }
    }
}

// Pop and return the first (lowest score) future position index on the heap.
int getFirstFuture() {
    // Remove the minimum element at index 1 (since 0 is empty)
    int o = futuresHeapValue[1];
    calcNumNodesRemovedHeap++;
    futuresHeapLength--;
    futuresHeapValue[1] = futuresHeapValue[futuresHeapLength];
    futuresHeapScore[1] = futuresHeapScore[futuresHeapLength];

    // Reheap the heap.
    int i = 1;
    while (1) {

        int l = i * 2, r = i * 2 + 1;
        if (l >= futuresHeapLength) {
            break;
        }
        else if (r >= futuresHeapLength) {
            if (futuresHeapScore[i] > futuresHeapScore[l]) {
                double tempScore = futuresHeapScore[i];
                futuresHeapScore[i] = futuresHeapScore[l];
                futuresHeapScore[l] = tempScore;

                int tempValue = futuresHeapValue[i];
                futuresHeapValue[i] = futuresHeapValue[l];
                futuresHeapValue[l] = tempValue;
            }
            break;
        }
        else if (futuresHeapScore[i] > futuresHeapScore[l] || futuresHeapScore[i] > futuresHeapScore[r]) {
            // Find the minimum score of left and right children and swap with that one.
            if (futuresHeapScore[l] < futuresHeapScore[r]) {
                double tempScore = futuresHeapScore[i];
                futuresHeapScore[i] = futuresHeapScore[l];
                futuresHeapScore[l] = tempScore;

                int tempValue = futuresHeapValue[i];
                futuresHeapValue[i] = futuresHeapValue[l];
                futuresHeapValue[l] = tempValue;
                i = l;
            }
            else {
                double tempScore = futuresHeapScore[i];
                futuresHeapScore[i] = futuresHeapScore[r];
                futuresHeapScore[r] = tempScore;

                int tempValue = futuresHeapValue[i];
                futuresHeapValue[i] = futuresHeapValue[r];
                futuresHeapValue[r] = tempValue;
                i = r;
            }
        }
        else {
            break;
        }
    }

    return o;
}

// Add a future index to the heap. Resize it if necessary.
void addFutureHeap(int value, double score) {
    if (futuresHeapLength >= futuresHeapCap) {
        futuresHeapCap = (int)((double)futuresHeapCap * futuresHeapCapMultiplier + (double)futuresHeapCapAdder);
        futuresHeapValue = (int*)realloc(futuresHeapValue, futuresHeapCap * 4);
        futuresHeapScore = (double*)realloc(futuresHeapScore, futuresHeapCap * 8);
    }

    futuresHeapScore[futuresHeapLength] = score;
    futuresHeapValue[futuresHeapLength] = value;

    // Reheap.
    int i = futuresHeapLength;
    while (i > 1) {
        int p = i / 2;
        if (futuresHeapScore[i] < futuresHeapScore[p]) {
            double tempScore = futuresHeapScore[i];
            futuresHeapScore[i] = futuresHeapScore[p];
            futuresHeapScore[p] = tempScore;

            int tempValue = futuresHeapValue[i];
            futuresHeapValue[i] = futuresHeapValue[p];
            futuresHeapValue[p] = tempValue;
        }
        else {
            break;
        }
        i = p;
    }

    futuresHeapLength++;

    calcNumNodesAddedHeap++;
}

// If e is the eval of a checkmate, return the eval of a mate in one, etc.
double evalForcedMateDelay(double e) {
    if (e >= WHITE_WINS_EVAL_THRESHOLD) {
        return e - EVAL_FORCED_MATE_INCREMENT;
    }
    if (e <= BLACK_WINS_EVAL_THRESHOLD) {
        return e + EVAL_FORCED_MATE_INCREMENT;
    }
    return e;
}

// Backtrack up the tree, keeping the eval of every node in the tree perfectly up-to-date.
void evalBacktrack() {
    double oldEval;

    // Now update the parents' evals to keep the eval of every node in the tree perfectly up-to-date.
    for (int node = currentExaminingNode; node != 0;) {

        int parent = nodeParentIndex[node];
        char* parentMiscs = nodeMiscs + parent * MISC_SIZE;

        char turn = parentMiscs[PLAYER_TURN];
        if (turn == BLACK) {

            // Set the parent's eval to be the best (minimum considering it's Black's turn) of the child evals.
            oldEval = nodeEval[parent];
            nodeEval[parent] = evalForcedMateDelay(nodeEval[nodeChildStartIndex[parent]]);
            for (int i = 1; i < nodeNumChildren[parent]; i++) {
                double childEval = evalForcedMateDelay(nodeEval[nodeChildStartIndex[parent] + i]);
                if (childEval < nodeEval[parent]) nodeEval[parent] = childEval;
            }

            // If the parent's eval did not change, there is no reason to keep going.
            if (oldEval == nodeEval[parent]) {
                break;
            }
        }
        else {

            // Set the parent's eval to be the best (maximum considering it's White's turn) of the child evals.
            oldEval = nodeEval[parent];
            nodeEval[parent] = evalForcedMateDelay(nodeEval[nodeChildStartIndex[parent]]);
            for (int i = 1; i < nodeNumChildren[parent]; i++) {
                double childEval = evalForcedMateDelay(nodeEval[nodeChildStartIndex[parent] + i]);
                if (childEval > nodeEval[parent]) nodeEval[parent] = childEval;
            }

            // If the parent's eval did not change, there is no reason to keep going.
            if (oldEval == nodeEval[parent]) {
                break;
            }
        }

        node = parent;
    }
}

// Examine the highest-priority future position, creating new nodes and evaluating them.
void examineNextPosition() {

    // Shallow copy the position since it is not being edited other than deep copied to a different location.
    currentExaminingNode = getFirstFuture();
    B = nodeBoard + currentExaminingNode * 64;
    M = nodeMiscs + currentExaminingNode * MISC_SIZE;

    bool playerTurn = M[PLAYER_TURN];

    // Examine all moves, creating a child node in only the tree for every fully-legal move.
    examineAllSemilegalMoves();

    // If there are no legal moves, mark this node as checkmate or stalemate.
    if (nodeNumChildren[currentExaminingNode] == 0) {
        char kingSquare = playerTurn == BLACK ? M[bKING_SQUARE] : M[wKING_SQUARE];

        if (kingNotInCheck(B, kingSquare, playerTurn)) {
            M[GAME_STATE] = DRAW;
            nodeEval[currentExaminingNode] = 0.0;
        }
        else if (playerTurn == BLACK) {
            M[GAME_STATE] = WHITE_WIN;
            nodeEval[currentExaminingNode] = WHITE_WINS_EVAL;
        }
        else {
            M[GAME_STATE] = BLACK_WIN;
            nodeEval[currentExaminingNode] = BLACK_WINS_EVAL;
        }

        evalBacktrack();
        return;
    }

    // Everything after this line within this method relies on there being at least 1 child (legal move) or is pointless otherwise.

    nodeChildStartIndex[currentExaminingNode] = numNodes - nodeNumChildren[currentExaminingNode];

    // Set the currentExaminingNode eval to be the best of the resulting evals.
    if (playerTurn == BLACK) {
        nodeEval[currentExaminingNode] = evalForcedMateDelay(nodeEval[nodeChildStartIndex[currentExaminingNode]]);
        for (int i = 1; i < nodeNumChildren[currentExaminingNode]; i++) {
            double childEval = evalForcedMateDelay(nodeEval[nodeChildStartIndex[currentExaminingNode] + i]);
            if (childEval < nodeEval[currentExaminingNode]) nodeEval[currentExaminingNode] = childEval;
        }
    }
    else {
        nodeEval[currentExaminingNode] = evalForcedMateDelay(nodeEval[nodeChildStartIndex[currentExaminingNode]]);
        for (int i = 1; i < nodeNumChildren[currentExaminingNode]; i++) {
            double childEval = evalForcedMateDelay(nodeEval[nodeChildStartIndex[currentExaminingNode] + i]);
            if (childEval > nodeEval[currentExaminingNode]) nodeEval[currentExaminingNode] = childEval;
        }
    }

    evalBacktrack();

    // Move up the tree from currentExaminingNode to the root, summing eval differences to get the parentScore.
    double parentScore = 0.0;
    int depth = 0; // Root has depth 0, etc.
    for (int node = currentExaminingNode; node != 0;) {

        int parent = nodeParentIndex[node];
        char* parentMiscs = nodeMiscs + parent * MISC_SIZE;

        /*
        // Add the eval difference between this node and its parent to parentScore.
        if (parentMiscs[PLAYER_TURN] == BLACK) {
            parentScore += nodeEval[node] - nodeEval[parent]; // Black move increasing eval is bad, so increase score.
        }
        else {
            parentScore += nodeEval[parent] - nodeEval[node]; // White move decreasing eval is bad, so increase score.
        }

        */

        double diff = nodeEval[node] - nodeEval[parent];
        if (-diff > diff) diff = -diff;
        parentScore += diff;

        depth++;
        node = parent;
    }

    // Add extra score points for each depth layer to bias toward lower depths.
    // Examining root = no change, etc.
    parentScore += 4.0 * (double)depth;

    // For each child node (resulting position), find its score (both from parent and then from root by adding parentScore).
    for (int i = 0; i < nodeNumChildren[currentExaminingNode]; i++) {
        
        int child = nodeChildStartIndex[currentExaminingNode] + i;
        char* childMiscs = nodeMiscs + child * MISC_SIZE;

        // If we checkmated or stalemated (either legally or by capturing the king), do not examine.
        if (childMiscs[GAME_STATE] != NORMAL) {
            continue;
        }
        // If max depth is reached, do not examine. (I wish there was a way to stash these nodes to be able to evaluate them when increasing max depth.)
        if (depth >= evaluationDepthLimit) {
            continue;
        }

        double childScore = 0.0;
        /*
        if (playerTurn == BLACK) {
            childScore = nodeEval[child] - nodeEval[currentExaminingNode]; // Black move increasing eval is bad, so increase score.
        }
        else {
            childScore = nodeEval[currentExaminingNode] - nodeEval[child]; // White move decreasing eval is bad, so increase score.
        }
        */
        double diff = nodeEval[child] - nodeEval[currentExaminingNode];
        if (-diff > diff) diff = -diff;
        childScore = diff;

        addFutureHeap(child, parentScore + childScore);
    }
}

// Reset the futures heap to the length 2 (one node) state from any length and capacity.
void clearHeapHeavy() {
    clear(futuresHeapValue);
    clear(futuresHeapScore);

    // Make it have one node.
    futuresHeapValue = (int*)realloc(futuresHeapValue, 8);
    futuresHeapScore = (double*)realloc(futuresHeapScore, 16);
    futuresHeapValue[1] = 0;
    futuresHeapScore[1] = ROOT_SCORE;
    futuresHeapLength = 2;
    futuresHeapCap = 2;
}

// Reset the futures heap to the length 2 (one node) state from any length and capacity, without freeing any memory.
void clearHeapLight() {
    
    // Assume the capacity is at least 2.
    // Make it have one node.
    futuresHeapValue[1] = 0;
    futuresHeapScore[1] = ROOT_SCORE;
    futuresHeapLength = 2;
}

// Reset the calc statistics.
void resetCalcStats() {
    calcNumWhiteWinsFound = 0;
    calcNumBlackWinsFound = 0;
    calcNumStalematesFound = 0;
    calcNumNormalsFound = 0;

    calcNumNodesAddedTree = 0;
    calcNumNodesAddedHeap = 0;
    calcNumNodesRemovedHeap = 0;

    size_t size = evaluationDepthLimit * 4;
    calcNumNodesAddedTreeDepth = (int*)realloc(calcNumNodesAddedTreeDepth, size);
    calcNumNodesAddedHeapDepth = (int*)realloc(calcNumNodesAddedHeapDepth, size);
    calcNumNodesRemovedHeapDepth = (int*)realloc(calcNumNodesRemovedHeapDepth, size);

    for (int i = 0; i < evaluationDepthLimit; i++) {
        calcNumNodesAddedTreeDepth[i] = 0;
        calcNumNodesAddedHeapDepth[i] = 0;
        calcNumNodesRemovedHeapDepth[i] = 0;
    }
}

// Empty the tree and heap of nodes. This should not need to be called since we don't need to free junk nodes; we can reuse them.
// Make the given position the only position in the analysis.
void clearNodesHeavy(char* b, char* m) {

    nodeParentIndex = (int*)realloc(nodeParentIndex, 4);
    nodeNumChildren = (int*)realloc(nodeNumChildren, 4);
    nodeChildStartIndex = (int*)realloc(nodeChildStartIndex, 4);
    nodeBoard = (char*)realloc(nodeBoard, 64);
    nodeMiscs = (char*)realloc(nodeMiscs, MISC_SIZE);
    nodeEval = (double*)realloc(nodeEval, 8);

    nodeCap = 1;
    numNodes = 1;
    currentExaminingNode = 0;

    B = nodeBoard + currentExaminingNode * 64;
    M = nodeMiscs + currentExaminingNode * MISC_SIZE;

    // Empty the node heap.
    clearHeapHeavy();

    // Reset the calc statistics.
    resetCalcStats();

    calcNumNodesAddedTree = 1;
    calcNumNodesAddedHeap = 1;
}

// Reset the tree and heap of nodes without actually freeing any memory.
// Make the given position the only position in the analysis.
void clearNodesLight(char* b, char* m) {

    // If the capacity is 0, make it 1.
    if (nodeCap == 0) {
        nodeParentIndex = (int*)realloc(nodeParentIndex, 4);
        nodeNumChildren = (int*)realloc(nodeNumChildren, 4);
        nodeChildStartIndex = (int*)realloc(nodeChildStartIndex, 4);
        nodeBoard = (char*)realloc(nodeBoard, 64);
        nodeMiscs = (char*)realloc(nodeMiscs, MISC_SIZE);
        nodeEval = (double*)realloc(nodeEval, 8);
        nodeCap = 1;
    }

    numNodes = 1;
    currentExaminingNode = 0;

    B = nodeBoard + currentExaminingNode * 64;
    M = nodeMiscs + currentExaminingNode * MISC_SIZE;

    // Empty the node heap.
    clearHeapLight();

    // Reset the calc statistics.
    resetCalcStats();

    calcNumNodesAddedTree = 1;
    calcNumNodesAddedHeap = 1;
}

// Clear the tree and initialize the first future position to examine, making deep copies of the given parameters.
void seedExamine(char* b, char* m) {
    // Make this the only position without freeing any nodes.
    clearNodesLight(b, m);

    // Deep copy b and m so deallocating them does not deallocate anything else.
    int lastTurn = gameLength - 1;
    nodeParentIndex[0] = UNDEFINED;
    nodeNumChildren[0] = 0;
    nodeChildStartIndex[0] = UNDEFINED;

    for (int i = 0; i < 64; i++) {
        nodeBoard[i] = b[i];
    }
    for (int i = 0; i < MISC_SIZE; i++) {
        nodeMiscs[i] = m[i];
    }

    // Get the eval of the new position.
    nodeEval[0] = computeEval(b, m);
}

// Return true if a white pawn move follows all white pawn rules.
bool isValidWhitePawnMove(char* b, char* m, char f, char t) {
    char rf = f / 8, cf = f % 8, rt = t / 8, ct = t % 8;

    if (rf < 6) {
        if (cf == ct) { // moving forward
            if (b[f + 8] == -1) {
                if (t == f + 8) return 1;
                if (rf == 1 && b[f + 16] == -1) { // two squares
                    if (t == f + 16) return 1;
                }
            }
        }
        else if (cf < 7 && t == f + 9) { // capturing right
            if (b[t] >= 6 && b[t] <= 11) {
                return 1;
            }
            else if (b[t] == -1 && m[4] == ct && rf == 4) {
                return 1; // en passant
            }
        }
        else if (cf > 0 && t == f + 7) { // capturing left
            if (b[t] >= 6 && b[t] <= 11) {
                return 1;
            }
            else if (b[t] == -1 && m[4] == ct && rf == 4) {
                return 1; // en passant
            }
        }
    }
    else if (rf == 6) { // promoting
        if (cf == ct) { // moving forward
            if (b[f + 8] == -1) {
                if (t == cf + 64 || t == cf + 72 || t == cf + 80 || t == cf + 88) return 1;
            }
        }
        else if (cf < 7 && (t == cf + 65 || t == cf + 73 || t == cf + 81 || t == cf + 89)) { // capturing right
            if (b[f + 9] >= 6 && b[f + 9] <= 11) {
                return 1;
            }
        }
        else if (cf > 0 && (t == cf + 63 || t == cf + 71 || t == cf + 79 || t == cf + 87)) { // capturing left
            if (b[f + 7] >= 6 && b[f + 7] <= 11) {
                return 1;
            }
        }
    }

    return 0;
}

// Return true if a black pawn move follows all black pawn rules.
bool isValidBlackPawnMove(char* b, char* m, char f, char t) {
    char rf = f / 8, cf = f % 8, rt = t / 8, ct = t % 8;

    if (rf > 1) {
        if (cf == ct) { // moving forward
            if (b[f - 8] == -1) {
                if (t == f - 8) return 1;
                if (rf == 6 && b[f - 16] == -1) { // two squares
                    if (t == f - 16) return 1;
                }
            }
        }
        else if (cf < 7 && t == f - 7) { // capturing right
            if (b[t] >= 0 && b[t] <= 5) {
                return 1;
            }
            else if (b[t] == -1 && m[4] == ct && rf == 3) {
                return 1; // en passant
            }
        }
        else if (cf > 0 && t == f - 9) { // capturing left
            if (b[t] >= 0 && b[t] <= 5) {
                return 1;
            }
            else if (b[t] == -1 && m[4] == ct && rf == 3) {
                return 1; // en passant
            }
        }
    }
    else if (rf == 1) { // promoting
        if (cf == ct) { // moving forward
            if (b[f - 8] == -1) {
                if (t == cf + 96 || t == cf + 104 || t == cf + 112 || t == cf + 120) return 1;
            }
        }
        else if (cf < 7 && (t == cf + 97 || t == cf + 105 || t == cf + 113 || t == cf + 121)) { // capturing right
            if (b[f - 7] >= 0 && b[f - 7] <= 5) {
                return 1;
            }
        }
        else if (cf > 0 && (t == cf + 95 || t == cf + 103 || t == cf + 111 || t == cf + 119)) { // capturing left
            if (b[f - 9] >= 0 && b[f - 9] <= 5) {
                return 1;
            }
        }
    }

    return 0;
}

// Return true if a knight move follows all knight rules.
bool isValidKnightMove(char* b, char* m, char f, char t) {
    char rf = f / 8, cf = f % 8, rt = t / 8, ct = t % 8;

    if (rf + 1 == rt) {
        return cf + 2 == ct || cf - 2 == ct;
    }
    if (rf - 1 == rt) {
        return cf + 2 == ct || cf - 2 == ct;
    }
    if (rf + 2 == rt) {
        return cf + 1 == ct || cf - 1 == ct;
    }
    if (rf - 2 == rt) {
        return cf + 1 == ct || cf - 1 == ct;
    }
    return 0;
}

// Return true if a bishop move follows all bishop rules.
bool isValidBishopMove(char* b, char* m, char f, char t) {
    char rf = f / 8, cf = f % 8, rt = t / 8, ct = t % 8;

    if (rf - cf == rt - ct) {
        if (f < t) {
            for (int x = f + 9; x < t; x += 9) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
        else {
            for (int x = f - 9; x > t; x -= 9) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
    }
    else if (rf + cf == rt + ct) {
        if (f < t) {
            for (int x = f + 7; x < t; x += 7) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
        else {
            for (int x = f - 7; x > t; x -= 7) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
    }

    return 0;
}

// Return true if a rook move follows all rook rules.
bool isValidRookMove(char* b, char* m, char f, char t) {
    char rf = f / 8, cf = f % 8, rt = t / 8, ct = t % 8;

    if (rf == rt) {
        if (cf < ct) {
            for (int x = f + 1; x < t; x++) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
        else {
            for (int x = f - 1; x > t; x--) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
    }
    else if (cf == ct) {
        if (rf < rt) {
            for (int x = f + 8; x < t; x += 8) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
        else {
            for (int x = f - 8; x > t; x -= 8) {
                if (b[x] != -1) return 0;
            }
            return 1;
        }
    }

    return 0;
}

// Return true if a queen move follows all queen rules.
bool isValidQueenMove(char* b, char* m, char f, char t) {
    return isValidBishopMove(b, m, f, t) || isValidRookMove(b, m, f, t);
}

// Return true if a white kingside castle follows all castle rules.
bool isValidWKMove(char* b, char* m, char f, char t) {
    if (m[wKINGSIDE_CASTLE] && f == 4 && t == 6 && b[5] == EMPTY && b[6] == EMPTY) {
        // no need to check king and rook positions since moving them turns off the misc

        if (kingNotInCheck(b, 4, 0)) {
            b[4] = -1;
            b[5] = 5;
            if (kingNotInCheck(b, 5, 0)) {
                b[5] = -1;
                b[6] = 5;
                if (kingNotInCheck(b, 6, 0)) {
                    b[4] = 5;
                    b[5] = -1;
                    b[6] = -1;
                    return 1;
                }
            }
        }

        b[4] = 5;
        b[5] = -1;
        b[6] = -1;
    }
    return 0;
}

// Return true if a white queenside castle follows all castle rules.
bool isValidWQMove(char* b, char* m, char f, char t) {
    if (m[wQUEENSIDE_CASTLE] && f == 4 && t == 2 && b[3] == EMPTY && b[2] == EMPTY) {
        // no need to check king and rook positions since moving them turns off the misc

        if (kingNotInCheck(b, 4, 0)) {
            b[4] = -1;
            b[3] = 5;
            if (kingNotInCheck(b, 3, 0)) {
                b[3] = -1;
                b[2] = 5;
                if (kingNotInCheck(b, 2, 0)) {
                    b[4] = 5;
                    b[3] = -1;
                    b[2] = -1;
                    return 1;
                }
            }
        }

        b[4] = 5;
        b[3] = -1;
        b[2] = -1;
    }
    return 0;
}

// Return true if a black kingside castle follows all castle rules.
bool isValidBKMove(char* b, char* m, char f, char t) {
    if (m[bKINGSIDE_CASTLE] && f == 60 && t == 62 && b[61] == EMPTY && b[62] == EMPTY) {
        // no need to check king and rook positions since moving them turns off the misc
        if (kingNotInCheck(b, 60, 1)) {
            b[60] = -1;
            b[61] = 11;
            if (kingNotInCheck(b, 61, 1)) {
                b[61] = -1;
                b[62] = 11;
                if (kingNotInCheck(b, 62, 1)) {
                    b[60] = 11;
                    b[61] = -1;
                    b[62] = -1;
                    return 1;
                }
            }
        }

        b[60] = 11;
        b[61] = -1;
        b[62] = -1;
    }
    return 0;
}

// Return true if a black queenside castle follows all castle rules.
bool isValidBQMove(char* b, char* m, char f, char t) {
    if (m[bQUEENSIDE_CASTLE] && f == 60 && t == 58 && b[59] == EMPTY && b[58] == EMPTY) {
        // no need to check king and rook positions since moving them turns off the misc

        if (kingNotInCheck(b, 60, 1)) {
            b[60] = -1;
            b[59] = 11;
            if (kingNotInCheck(b, 59, 1)) {
                b[59] = -1;
                b[58] = 11;
                if (kingNotInCheck(b, 58, 1)) {
                    b[60] = 11;
                    b[59] = -1;
                    b[58] = -1;
                }
                return 1;
            }
        }

        b[60] = 11;
        b[59] = -1;
        b[58] = -1;
    }
    return 0;
}

bool isValidKingMove(char* b, char* m, char f, char t) {
    char rf = f / 8, cf = f % 8, rt = t / 8, ct = t % 8;
    char rd = rt - rf, cd = ct - cf;

    return rd >= -1 && rd <= 1 && cd >= -1 && cd <= 1;
}


// Return true if a move follows the piece moving rules.
bool isSemilegalMove(char* b, char* m, char moveFrom, char moveTo) {
    char p = b[moveFrom];
    char q = b[moveTo];

    switch (p) {
    case 0:
        return isValidWhitePawnMove(b, m, moveFrom, moveTo);
    case 6:
        return isValidBlackPawnMove(b, m, moveFrom, moveTo);
    case 1:
    case 7:
        return isValidKnightMove(b, m, moveFrom, moveTo);
    case 2:
    case 8:
        return isValidBishopMove(b, m, moveFrom, moveTo);
    case 3:
    case 9:
        return isValidRookMove(b, m, moveFrom, moveTo);
    case 4:
    case 10:
        return isValidQueenMove(b, m, moveFrom, moveTo);
    case 5:
        return isValidKingMove(b, m, moveFrom, moveTo) || isValidWKMove(b, m, moveFrom, moveTo) || isValidWQMove(b, m, moveFrom, moveTo);
    case 11:
        return isValidKingMove(b, m, moveFrom, moveTo) || isValidBKMove(b, m, moveFrom, moveTo) || isValidBQMove(b, m, moveFrom, moveTo);
    }

    // moved piece is not a piece
    return 0;
}

// Return true if the given move on the given board follows the piece moving rules and does not move into check.
// Replace b and m with the new position if playIfLegal and the move is fully legal.
bool isLegalMove(char* b, char* m, char moveFrom, char moveTo, bool playIfLegal) {

    char playerTurn = m[PLAYER_TURN];
    if (moveFrom < 0 || moveFrom >= 64) {
        return 0;
    }

    if (moveTo < 0) {
        return 0;
    }

    char p = b[moveFrom];
    char q = -1;
    if (moveTo >= 96) {
        q = b[moveTo % 8];
    }else if(moveTo >= 64){
        q = b[56 + (moveTo % 8)];
    }else{
        q = b[moveTo];
    }

    if (moveFrom == moveTo) { // piece moved to square it moved from
        return 0;
    }

    if (p < 6 && playerTurn == BLACK) { // moved piece is white while playing black
        return 0;
    }

    if (p > 5 && playerTurn == WHITE) { // moved piece is black while playing white
        return 0;
    }

    if (q >= 6 && q <= 11 && playerTurn == BLACK) { // destination is black while playing black
        return 0;
    }

    if (q >= 0 && q <= 5 && playerTurn == WHITE) { // destination is white while playing white
        return 0;
    }

    // Check the piece moving rules.
    if (!isSemilegalMove(b, m, moveFrom, moveTo)) {
        return 0;
    }

    // Simulate moving any pieces involved in this move by creating a new state so we can go back.
    char* newB = (char*)calloc(64, 1);
    for (int i = 0; i < 64; i++) {
        newB[i] = b[i];
    }
    char* newM = (char*)calloc(MISC_SIZE, 1);
    for (int i = 0; i < MISC_SIZE; i++) {
        newM[i] = m[i];
    }

    char newPlayerTurn = 1 - playerTurn;
    newM[SQUARE_FROM] = moveFrom;
    newM[SQUARE_TO] = moveTo;
    newM[PLAYER_TURN] = newPlayerTurn;

    playMove(newB, newM);

    char kingSquare = newM[playerTurn == BLACK ? bKING_SQUARE : wKING_SQUARE];

    bool notInCheck = kingNotInCheck(newB, kingSquare, playerTurn);

    if (playIfLegal && notInCheck) {
        b = newB;
        m = newM;
    }
    else {
        // Free the new board and miscs pointers since we're not using the board state after this check.
        clear(newB);
        clear(newM);
    }

    return notInCheck;
}

// Return true if the given state has occurred at least twice previously in the game history.
bool checkThreefoldRepetition() {

    char count = 0;

    // Check every second game state (all previous states with same player's turn as now) for equality.
    for (int i = gameLength - 3; i >= 0; i -= 2) {

        // Check all board squares for equality.
        bool unequal = 0;
        for (int j = 0; j < 64; j++) {
            if (gameBoardHistory[gameLength - 1][j] != gameBoardHistory[i][j]) {
                unequal = 1; break;
            }
        }
        if (!unequal) {
            // Check only castling and en passant states.
            for (int j = 0; j < 5; j++) {
                if (gameMiscsHistory[gameLength - 1][j] != gameMiscsHistory[i][j]) {
                    unequal = 1; break;
                }
            }

            if (!unequal) {
                count++;
                if (count >= 2) return 1;
            }
        }
    }
    return 0;
}

// Setup the console to check if a key has been pressed.
void startEvaluationInterruptDetector() {
    // Clear the past key presses
    while (_kbhit()) {}
}

// Return true if the user has typed anything since the start of the evaluation.
bool checkEvaluationInterruptDetector() {
    // Check if at least one key has been pressed since last interrupt check
    bool result = _kbhit();

    // Clear the other key presses
    while (_kbhit()) {}

    return result;
}

// Fill the evalBoards with zeroes.
void fillEvalBoards0s() {

}

// Fill the evalBoards with default values.
void setupEvalBoards() {
    if (evalBoards == NULL) {
        evalBoards = (double**)calloc(NUM_PIECES, sizeof(double*));
        for (int i = 0; i < NUM_PIECES; i++) {
            evalBoards[i] = (double*)calloc(64, 8);
        }
    }

    for (int i = 0; i < NUM_PIECES; i++) {
        for (int j = 0; j < 64; j++) {
            double rowScore = i < 6 ? j / 8 : 7 - (j / 8);
            double colScore = j % 8 < 4 ? j % 8 : 7 - (j % 8);

            double placementScore = (rowScore + colScore - 3) * pieceEdgeContribution[i];

            evalBoards[i][j] = piecePointValues[i] + placementScore;
        }
    }
}

void setupAnalysisBoard() {
    if (analysisBoard == NULL) {
        analysisBoard = (char*)calloc(64, 1);
        analysisMiscs = (char*)calloc(MISC_SIZE, 1);
    }
}

void randomizeEvalBoards() {

}

// Prepare to evaluate a position.
void setupEvaluation(char* b, char* m) {

    setupEvalBoards();
    seedExamine(b, m);
}

// Continue evaluating a position until reaching a time given the global evaluation settings.
char evaluatePositionTimed(double t) {

    unsigned int s = 0;
    unsigned int ns = 0;
    calcTime(t, &s, &ns);

    int i = 0;
    for (;; i++) {

        // Checking if there are no more futures to evaluate.
        if (futuresHeapLength == 1) {
            return 1;
        }
        if (numNodes >= evaluationNodeLimit - 500) {
            return 1;
        }

        // Checking if exceeding the time limit for this evaluation period.
        if (i % evaluationTimeMeasurementInterval == 0) {
            struct timespec now;
            timespec_get(&now, TIME_UTC);

            if ((unsigned int)now.tv_sec > s || ((unsigned int)now.tv_sec == s && (unsigned int)now.tv_nsec >= ns)) {
                return 0;
            }
        }

        examineNextPosition();
    }

    return 0;
}

// Read a string from console.
void getLine() {

    while (1) {
        for (int i = 0; i < MAX_LINE_SIZE; i++) {
            lastLine[i] = 0;
        }

        char* r = fgets(lastLine, MAX_LINE_SIZE, stdin);

        if (r == NULL) {
            printf("Enter a valid string of characters with length 0-%i: ", MAX_LINE_SIZE);
        }
        else {
            return;
        }
    }
}

bool isAlpha(char c) {
    return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z';
}

bool isNumeric(char c) {
    return c >= '0' && c <= '9';
}

// Read and return a char from console.
// Return \n if given a blank line.
char getChar() {
    getLine();
    return lastLine[0];
}

// Read and return a non-negative integer from console.
int getNumeric() {
    bool invalid = 1;
    int x = 0;
    while (invalid) {
        invalid = 0;
        getLine();
        int i = 0;
        x = 0;
        while (lastLine[i] != '\n') {
            char c = lastLine[i];
            if (c == ' ') continue;

            if (isNumeric(c)) {
                if (x >= 200000000) {
                    printf("Input number must be less than 2000000000: ");
                    exit(1);
                }
                x *= 10;
                x += lastLine[i] - '0';
                i++;
                if (i >= MAX_LINE_SIZE) {
                    printf("Error: line reading overflow.\n\n");
                    exit(1);
                }
            }
            else {
                printf("Input must be a non-negative integer: ");
                invalid = 1;
                break;
            }

            c = lastLine[i];
        }
    }

    return x;
}

bool isPiece(char c) {
    switch (c) {
    case 'P':
    case 'N':
    case 'B':
    case 'R':
    case 'Q':
    case 'K':
        return 1;
    }
    return 0;
}

bool isAH(char c) {
    return c >= 'a' && c <= 'h';
}

bool is18(char c) {
    return c >= '1' && c <= '8';
}

// Read and set a move string from console.
void getMoveString() {
    clear(moveString);
    moveString = (char*)calloc(MAX_MOVE_STRING_LENGTH, 1);
    moveStringLength = 0;

    bool invalid = 1;
    while (invalid) {
        invalid = 0;
        getLine();
        moveStringLength = 0;
        for (int i = 0; lastLine[i] != '\n'; i++) {
            char c = lastLine[i];
            if (c == ' ') continue;
            if (c == '-') continue;
            if (c == 'x') continue;

            if (c == '0' || is18(c) || isAH(c) || isPiece(c)) {
                if (i >= MAX_LINE_SIZE) {
                    printf("Error: line reading overflow.\n\n");
                    exit(1);
                }

                if (moveStringLength >= MAX_MOVE_STRING_LENGTH) {
                    printf("Error: move string cannot exceed the maximum length of %i.\n\n", MAX_MOVE_STRING_LENGTH);
                    exit(1);
                }
                moveString[moveStringLength] = c;
                moveStringLength++;
            }
            else {
                printf("Input must contain only valid letters and numbers, not %c: ", c);
                invalid = 1;
                break;
            }
        }
    }

    // Null-terminate the move string.
    for (int i = moveStringLength; i < MAX_MOVE_STRING_LENGTH; i++) {
        moveString[i] = '\0';
    }
}

void updateKeys() {
    for (int i = 0; i < 256; i++) {
        keyPrev[i] = keyCurr[i];
        keyCurr[i] = 0;
    }

    while (_kbhit()) {
        int ch = _getch();
        printf("%c", ch);
        keyCurr[ch] = 1;
    }
}

// Return the type (0-11) of a piece character or -1 if invalid.
char pieceCharToType(char c, bool isBlackMove) {
    char blackAddon = 6 * isBlackMove;

    switch (c) {
    case 'P':
        return blackAddon;
    case 'N':
        return blackAddon + 1;
    case 'B':
        return blackAddon + 2;
    case 'R':
        return blackAddon + 3;
    case 'Q':
        return blackAddon + 4;
    case 'K':
        return blackAddon + 5;
    }

    return -1;
}

// Get the first possible movefrom square of the piece moving to a given board square.
// Row and col restrict the movefrom square. They can specify a value or can be -1 for any value.
// Returns -127 if no possible movefrom square.
char getPieceMoving(char* b, char* m, char piece, char t, char row, char col, bool isBlackMove) {
    char type = pieceCharToType(piece, isBlackMove);
    if (type == -1) {
        return -127;
    }

    char rowStart = 0, rowEnd = 7, colStart = 0, colEnd = 7;
    if (row > -1) {
        rowStart = row;
        rowEnd = row;
    }
    if (col > -1) {
        colStart = col;
        colEnd = col;
    }

    // Check all possible rows and cols that the user could be specifying to move from.
    for (int i = rowStart; i <= rowEnd; i++) {
        for (int j = colStart; j <= colEnd; j++) {
            char f = i * 8 + j;
            if (b[f] != type) continue;

            // Check if the given piece can move from f to t.
            if (isLegalMove(b, m, f, t, 0)) {
                return f;
            }
        }
    }

    return -127;
}

// Get the square for a square index in human-readable format.
char* getSquareHuman(char x) {

    if (x < 0) {
        char* o = (char*)calloc(3, 1);
        o[0] = '?';
        o[1] = '?';
        o[2] = '\0';
        return o;
    }

    if (x >= 64) {
        char* o = (char*)calloc(4, 1);
        o[0] = 'a' + (x % 8);
        o[1] = x >= 96 ? '1' : '8';
        char promotionRow = (x % 32) / 8;
        switch (promotionRow) {
        case 0:
            o[2] = 'N'; break;
        case 1:
            o[2] = 'B'; break;
        case 2:
            o[2] = 'R'; break;
        case 3:
            o[2] = 'Q'; break;
        }
        o[3] = '\0';
        return o;
    }

    char* o = (char*)calloc(3, 1);
    o[0] = 'a' + (x % 8);
    o[1] = '1' + (x / 8);
    o[2] = '\0';
    return o;
}

// Get a string for a move in human-readable format.
char* getMoveHuman(int i) {

    int index = nodeIndexSorted[i];
    char* miscs = nodeMiscs + index * MISC_SIZE;
    char f = miscs[SQUARE_FROM];
    char t = miscs[SQUARE_TO];
    char piece = nodeBoard[f];

    char* o;

    if (piece == wKING && f == 4 && t == 6 || piece == bKING && f == 60 && t == 62) {
        o = (char*)calloc(4, 1);
        o[0] = '0';
        o[1] = '-';
        o[2] = '0';
        o[3] = '\0';
        return o;
    }
    if (piece == wKING && f == 4 && t == 2 || piece == bKING && f == 60 && t == 58) {
        o = (char*)calloc(6, 1);
        o[0] = '0';
        o[1] = '-';
        o[2] = '0';
        o[3] = '-';
        o[4] = '0';
        o[5] = '\0';
        return o;
    }

    int l = 5, s = 0;
    if (piece != wPAWN && piece != bPAWN) {
        l++;
        s++;
    }

    if (t >= 64) {
        // If the move is a promotion, add the promotion piece letter at the end.
        l++;
        o = (char*)calloc(l, 1);

        switch ((t % 32) / 8) {
        case 0:
            o[l - 2] = 'N'; break;
        case 1:
            o[l - 2] = 'B'; break;
        case 2:
            o[l - 2] = 'R'; break;
        case 3:
            o[l - 2] = 'Q'; break;
        }

        // Set the destination to the true destination of the promotion.
        if (t >= 96) {
            t %= 8;
        }
        else {
            t = 56 + (t % 8);
        }
    }
    else {
        o = (char*)calloc(l, 1);
    }

    // Indicate the moving piece if it is not a pawn.
    switch (piece) {
    case EMPTY:
        o[0] = '?'; break;
    case wKNIGHT:
    case bKNIGHT:
        o[0] = 'N'; break;
    case wBISHOP:
    case bBISHOP:
        o[0] = 'B'; break;
    case wROOK:
    case bROOK:
        o[0] = 'R'; break;
    case wQUEEN:
    case bQUEEN:
        o[0] = 'Q'; break;
    case wKING:
    case bKING:
        o[0] = 'K'; break;
    }

    // Indicate the square we are moving from.
    if (f < 0 || f >= 64) {
        o[s] = '?';
        o[s + 1] = '?';
    }
    else {
        o[s] = 'a' + (f % 8);
        o[s + 1] = '1' + (f / 8);
    }

    // Indicate the square we are moving to.
    if (t < 0 || t >= 64) {
        o[s + 2] = '?';
        o[s + 3] = '?';
    }
    else {
        o[s + 2] = 'a' + (t % 8);
        o[s + 3] = '1' + (t / 8);
    }

    o[l - 1] = '\0';
    return o;
}

// Return the UTF-8 encoding for a Unicode value.
int getUTF8(char* utf8, unsigned int c) {
    if (c <= 0x7F) {
        utf8[0] = c;
        return 1;
    }
    if (c <= 0x7FF) {
        utf8[0] = 0xC0 | (c >> 6);
        utf8[1] = 0x80 | (c & 0x3F);
        return 2;
    }
    if (c <= 0xFFFF) {
        if (c >= 0xD800 && c <= 0xDFFF) return 0;
        utf8[0] = 0xE0 | (c >> 12);
        utf8[1] = 0x80 | ((c >> 6) & 0x3F);
        utf8[2] = 0x80 | (c & 0x3F);
        return 3;
    }
    if (c <= 0x10FFFF) {
        utf8[0] = 0xF0 | (c >> 18);
        utf8[1] = 0x80 | ((c >> 12) & 0x3F);
        utf8[2] = 0x80 | ((c >> 6) & 0x3F);
        utf8[3] = 0x80 | (c & 0x3F);
        return 4;
    }
    return 0;
}

// Get the encoded promotion square given the promotion column and the type being promoted to.
char getPromotionSquareCode(char col, char type) {
    if (type >= 7 && type <= 11) {
        return col + 8 * (type + 5);
    }
    else if (type >= 1 && type <= 5) {
        return col + 8 * (type + 7);
    }
    else {
        return -128;
    }
}

// Parse a user-entered string containing a move and set movefrom and moveto.
// Return whether the move is fully legal.
bool parseMove(char* b, char* m, char* s, int l, char playerTurn) {
    char* f = &(m[SQUARE_FROM]);
    char* t = &(m[SQUARE_TO]);
    *f = -128; *t = -128;

    switch (l) {
    case 2:

        // Pawn move (e4)
        if (isAH(s[0]) && is18(s[1])) {
            *t = (s[1] - '1') * 8 + s[0] - 'a';
            if (playerTurn) {
                if (*t >= 56) {
                    printf("No black pawn can move to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
                else if (b[*t + 8] == 6) {
                    *f = *t + 8;
                }
                else if (*t / 8 == 4 && b[*t + 16] == 6) {
                    *f = *t + 16;
                }
                else {
                    printf("No black pawn can move to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
            }
            else {
                if (*t < 8) {
                    printf("No white pawn can move to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
                else if (b[*t - 8] == 0) {
                    *f = *t - 8;
                }
                else if (*t / 8 == 3 && b[*t - 16] == 0) {
                    *f = *t - 16;
                }
                else {
                    printf("No white pawn can move to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
            }
        }

        // Kingside (00)
        if (s[0] == '0' && s[1] == '0') {
            *f = 4 + playerTurn * 56;
            *t = 6 + playerTurn * 56;
        }

        break;
    case 3:

        // Piece move (Ne4)
        if (isPiece(s[0]) && isAH(s[1]) && is18(s[2])) {
            *t = (s[2] - '1') * 8 + s[1] - 'a';
            *f = getPieceMoving(b, m, s[0], *t, -1, -1, playerTurn);
        }

        // Queenside (000)
        if (s[0] == '0' && s[1] == '0' && s[2] == '0') {
            *f = 4 + playerTurn * 56;
            *t = 2 + playerTurn * 56;
        }

        // Pawn capture (de4)
        if (isAH(s[0]) && isAH(s[1]) && is18(s[2])) {
            *t = (s[2] - '1') * 8 + s[1] - 'a';
            if (playerTurn) {
                *f = (s[2] - '0') * 8 + s[0] - 'a';
                if (*t >= 56 || *t < 8) {
                    printf("No black pawn can capture to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
                else if (b[*f] != bPAWN) {
                    printf("No black pawn can capture to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
            }
            else {
                *f = (s[2] - '2') * 8 + s[0] - 'a';
                if (*t >= 56 || *t < 8) {
                    printf("No white pawn can capture to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
                else if (b[*f] != wPAWN) {
                    printf("No white pawn can capture to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
            }
        }

        // Pawn move promotion (e8Q)
        if (isAH(s[0]) && is18(s[1]) && isPiece(s[2])) {
            *t = getPromotionSquareCode(s[0] - 'a', pieceCharToType(s[2], playerTurn));
            if (s[2] == 'P' || s[2] == 'K') {
                printf("No black pawn can promote to %c: ", s[2]);
                *f = -126;
            }
            else if (playerTurn) {
                *f = 8 + s[0] - 'a';
                if (s[1] != '1') {
                    printf("No black pawn can promote to %s%c: ", getSquareHuman(*t), s[2]);
                    *f = -126;
                }
                else if (b[*f] == bPAWN) {

                }
                else {
                    printf("No black pawn can promote to %s%c: ", getSquareHuman(*t), s[2]);
                    *f = -126;
                }
            }
            else {
                *f = 48 + s[0] - 'a';
                if (s[1] != '8') {
                    printf("No white pawn can promote to %s%c: ", getSquareHuman(*t), s[2]);
                    *f = -126;
                }
                else if (b[*f] == wPAWN) {

                }
                else {
                    printf("No white pawn can promote to %s%c: ", getSquareHuman(*t), s[2]);
                    *f = -126;
                }
            }
        }

        break;
    case 4:

        // From-to (c3e4)
        if (isAH(s[0]) && is18(s[1]) && isAH(s[2]) && is18(s[3])) {
            *t = (s[3] - '1') * 8 + s[2] - 'a';
            *f = (s[1] - '1') * 8 + s[0] - 'a';
        }

        // Piece move with row (N3e4)
        if (isPiece(s[0]) && is18(s[1]) && isAH(s[2]) && is18(s[3])) {
            *t = (s[3] - '1') * 8 + s[2] - 'a';
            *f = getPieceMoving(b, m, s[0], *t, s[1] - '1', -1, playerTurn);
        }

        // Piece move with column (Nce4)
        if (isPiece(s[0]) && isAH(s[1]) && isAH(s[2]) && is18(s[3])) {
            *t = (s[3] - '1') * 8 + s[2] - 'a';
            *f = getPieceMoving(b, m, s[0], *t, -1, s[1] - 'a', playerTurn);
        }

        // Pawn capture promotion (de8Q)
        if (isAH(s[0]) && isAH(s[1]) && is18(s[2]) && isPiece(s[3])) {
            *t = (s[2] - '1') * 8 + s[1] - 'a';
            if (s[3] == 'P' || s[3] == 'K') {
                printf("No black pawn can promote to %c: ", s[3]);
                *f = -126;
            }
            else if (playerTurn) {
                *f = (s[2] - '0') * 8 + s[0] - 'a';
                if (s[2] != '1') {
                    printf("No black pawn can promote to the %cth rank: ", s[2]);
                    *f = -126;
                }
                else if (b[*f] == bPAWN) {

                }
                else {
                    printf("No black pawn can capture to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
            }
            else {
                *f = (s[2] - '2') * 8 + s[0] - 'a';
                if (s[2] != '8') {
                    printf("No white pawn can promote to the %cth rank: ", s[2]);
                    *f = -126;
                }
                else if (b[*f] == wPAWN) {

                }
                else {
                    printf("No white pawn can capture to %s: ", getSquareHuman(*t));
                    *f = -126;
                }
            }
            *t = getPromotionSquareCode(s[1] - 'a', pieceCharToType(s[3], playerTurn));
        }

        break;
    case 5:

        // Piece move with both (Nc3e4)
        if (isPiece(s[0]) && isAH(s[1]) && is18(s[2]) && isAH(s[3]) && is18(s[4])) {
            *t = (s[4] - '1') * 8 + s[3] - 'a';
            *f = getPieceMoving(b, m, s[0], *t, s[2] - '1', s[1] - 'a', playerTurn);
        }

        // From-to pawn promotion (d7e8Q)
        if (isAH(s[0]) && is18(s[1]) && isAH(s[2]) && is18(s[3]) && isPiece(s[4])) {
            *f = (s[1] - '1') * 8 + s[0] - 'a';
            *t = getPromotionSquareCode(s[2] - 'a', pieceCharToType(s[4], playerTurn));
        }

        break;
    }

    switch (*f) {
    case -128:
        printf("Move is formatted incorrectly: ");
        return 0;
    case -127:
        printf("No %c piece can move in the specified way: ", s[0]);
        return 0;
    case -126: // Miscellaneous problem
        return 0;
    default:
        if (isLegalMove(b, m, *f, *t, 1)) {
            return 1;
        }
        else {
            printf("Move from %s to %s is illegal: ", getSquareHuman(*f), getSquareHuman(*t));
            return 0;
        }
    }
    return 0;
}

// Return the char used to display a piece.
char pieceTypeToChar(char x) {
    bool r = reverseWhiteBlackLetters;
    char c[] = { 'P', 'N', 'B', 'R', 'Q', 'K' };
    char l[] = { 'p', 'n', 'b', 'r', 'q', 'k' };
    if (x >= 0 && x <= 5) {
        return r ? l[x] : c[x];
    }
    if (x >= 6 && x <= 11) {
        return r ? c[x - 6] : l[x - 6];
    }
    return asciiSpaceValues[asciiSpaceOption];
}

// Gets a move from the user and records the movefrom, moveto, and new playerTurn in the given parameter m.
// Repeats until the move is fully legal on the given parameters.
bool getMove(char* b, char* m) {
    char playerTurn = m[PLAYER_TURN];

    printf("Enter a move for ");
    playerTurn == BLACK ? printf("Black: ") : printf("White: ");

    //printf("Debug Miscs: ");
    //for (int i = 0; i < MISC_SIZE; i++) {
    //    printf("%i ", m[i]);
    //}
    //printf("\n");

    while (1) {
        getMoveString();

        if (moveStringLength == 0) {
            return 0;
        }

        bool legal = parseMove(b, m, moveString, moveStringLength, playerTurn);
        if (legal) {
            m[PLAYER_TURN] = 1 - playerTurn;
            return 1;
        }
    }
    return 1;
}

// Get the Unicode value for a piece type.
unsigned int getUnicodeValue(char* b, char x) {
    switch (b[x]) {
    case -1:
        if ((x + (x / 8)) % 2) {
            return 0x2588;
        }
        else {
            return 32;
        }
    case 0: return 0x265f;
    case 1: return 0x265e;
    case 2: return 0x265d;
    case 3: return 0x265c;
    case 4: return 0x265b;
    case 5: return 0x265a;
    case 6: return 0x2659;
    case 7: return 0x2658;
    case 8: return 0x2657;
    case 9: return 0x2656;
    case 10: return 0x2655;
    case 11: return 0x2654;
    }

    return 32;
}

// Draw a Unicode character.
void drawUnicode(unsigned int u) {
    char utf8[4] = { 0, 0, 0, 0 };
    int len = getUTF8(utf8, u);
    if (len > 0) {
        printf("%s", utf8);
    }
    else {
        printf(" ");
    }
}

// Draw the given board on screen.
void drawBoard(char* b, bool playerTurn) {
    if (unicodeEnabled) {
        drawUnicode(0x250f);
        for (int i = 0; i < 16; i++) {
            drawUnicode(0x2501);
        }
        drawUnicode(0x2513);
        printf("\n");

        for (int y = 0; y < 8; y++) {
            drawUnicode(0x2503);
            for (int x = 0; x < 8; x++) {
                char square = playerTurn ? y * 8 + (7 - x) : (7 - y) * 8 + x;
                unsigned int u = getUnicodeValue(b, square);
                drawUnicode(u);
                if (u == 0x2588) {
                    drawUnicode(0x2588);
                }
                else {
                    printf(" ");
                }
            }
            drawUnicode(0x2503);
            if (showBoardCoordinates) {
                char c[] = " ";
                if (playerTurn) {
                    c[0] = '1' + y;
                }
                else {
                    c[0] = '8' - y;
                }
                printf(c);
            }
            printf("\n");
        }

        drawUnicode(0x2517);
        for (int i = 0; i < 16; i++) {
            drawUnicode(0x2501);
        }
        drawUnicode(0x251b);
        printf("\n");
    }
    else {
        printf("-------------------\n");
        for (int y = 0; y < 8; y++) {
            printf("| ");
            for (int x = 0; x < 8; x++) {
                char square = playerTurn ? y * 8 + (7 - x) : (7 - y) * 8 + x;
                printf("%c ", pieceTypeToChar(b[square]));
            }
            printf("|");
            if (showBoardCoordinates) {
                char c[] = " ";
                if (playerTurn) {
                    c[0] = '1' + y;
                }
                else {
                    c[0] = '8' - y;
                }
                printf(c);
            }
            printf("\n");
        }
        printf("-------------------\n");
    }

    if (showBoardCoordinates) {
        if (playerTurn) {
            printf("  H G F E D C B A\n");
        }
        else {
            printf("  A B C D E F G H \n");
        }
    }
    else {
        printf("\n");
    }
    printf("\n");
}

// Copy and sort the choices of moves from node 0.
void sortChoices() {

    int numChoices = nodeNumChildren[0];
    nodeIndexSorted = (int*)calloc(numChoices, 4);
    nodeEvalSorted = (double*)calloc(numChoices, 8);

    for (int i = 0; i < numChoices; i++) {
        nodeIndexSorted[i] = 1 + i;
        nodeEvalSorted[i] = nodeEval[1 + i];
    }

    bool playerTurn = nodeMiscs[PLAYER_TURN];

    // Sort the choices using insertion sort.
    for (int i = 1; i < numChoices; i++) {

        double x = nodeEvalSorted[i];
        int y = nodeIndexSorted[i];
        int j = i - 1;

        while (j >= 0 && ((playerTurn) && nodeEvalSorted[j] > x || (!playerTurn) && nodeEvalSorted[j] < x)) {
            nodeEvalSorted[j + 1] = nodeEvalSorted[j];
            nodeIndexSorted[j + 1] = nodeIndexSorted[j];
            j--;
        }

        nodeEvalSorted[j + 1] = x;
        nodeIndexSorted[j + 1] = y;
    }
}

// Choose a move using the evals and difficulty (from DIFFICULTY_MIN to DIFFICULTY_MAX).
// Return the node index of the move.
// Return -1 if no moves found.
int chooseMove(int difficulty) {

    int numChoices = nodeNumChildren[0];

    if (numChoices <= 0) return -1;

    sortChoices();

    if (evaluationPrintChoices) {
        if (numChoices > 0) {
            printf("%i choices with best eval (current position eval) %f:\n", numChoices, nodeEvalSorted[0]);
            for (int i = 0; i < numChoices; i++) {
                printf(getMoveHuman(i));
                printf("\t%f\n", nodeEvalSorted[i]);
            }
        }
        else {
            printf("No move choices found.\n");
        }
    }

    // Get the number of good moves to choose from depending on the engine difficulty.
    int numActualChoices = numChoices;
    if (DIFFICULTY_MAX + 1 - difficulty < numActualChoices) {
        numActualChoices = DIFFICULTY_MAX + 1;
    }

    // Get the 0-indexed choice.
    int choice = random() % numActualChoices;
    return nodeIndexSorted[0];

    // Return the node index of that choice.
    return nodeIndexSorted[choice];
}

// SQUARE_FROM, SQUARE_TO, and PLAYER_TURN must be set.
// Current turn (last index in histories) must be at least 1, game length must be at least 2.
// Return whether to end the game.
bool playAndCheckEndOfGame() {

    playMove(gameBoardHistory[gameLength - 1], gameMiscsHistory[gameLength - 1]);

    char newPlayerTurn = gameMiscsHistory[gameLength - 1][PLAYER_TURN];

    // Determine if the new position is checkmate or stalemate using the examine method from evaluation.
    // Reuse the examine position code just to check if there is a legal move.
    setupEvaluation(gameBoardHistory[gameLength - 1], gameMiscsHistory[gameLength - 1]);
    examineAllSemilegalMoves();

    // If there are no legal moves, end the game as either checkmate or stalemate.
    if (nodeNumChildren[0] == 0) {

        // Find out whether the king of the player whose turn it is after the move is in check.
        int kingSquare = newPlayerTurn ? gameMiscsHistory[gameLength - 1][bKING_SQUARE] : gameMiscsHistory[gameLength - 1][wKING_SQUARE];

        if (kingNotInCheck(gameBoardHistory[gameLength - 1], kingSquare, newPlayerTurn)) {
            drawBoard(gameBoardHistory[gameLength - 1], newPlayerTurn);
            printf("Stalemate!\n\n");
        }
        else {
            drawBoard(gameBoardHistory[gameLength - 1], newPlayerTurn);
            printf("Checkmate!\n");
        }

        return 1;
    }

    return 0;
}

int minimumSufficientPieceCounts[NUM_PIECES] = { 1, 2, 2, 1, 1, 0, 1, 2, 2, 1, 1, 0 };

// Return 1 if neither player has the material to checkmate.
bool checkInsufficientMatingMaterial() {
    int* c = (int*)calloc(NUM_PIECES, 4);
    char* board = gameBoardHistory[gameLength - 1];

    for (int i = 0; i < 64; i++) {
        c[board[i]]++;
    }

    for (int i = 0; i < NUM_PIECES; i++) {
        if (c[i] >= minimumSufficientPieceCounts[i]) return 0;
    }
    return 1;
}

// Check the game position for threefold repetition, 50-move rule, and insufficient mating material draws.
// Return 1 if ending the game in a draw.
bool checkDraws() {
    if (checkThreefoldRepetition()) {
        if (drawSetting == FORCE) {
            return 1;
        }
        else {
            printf("Threefold repetition. Would you like to claim a draw? (y/n)\n");
            getLine();
            return lastLine[0] != '\0' && lastLine[0] != '\n' && lastLine[0] != 'n' && lastLine[0] != 'N';
        }
    }

    if (gameMiscsHistory[gameLength - 1][FIFTY_MOVE_COUNTER] >= 100) {
        if (drawSetting == FORCE) {
            return 1;
        }
        else {
            printf("Fifty-move rule. Would you like to claim a draw? (y/n)\n");
            getLine();
            return lastLine[0] != '\0' && lastLine[0] != '\n' && lastLine[0] != 'n' && lastLine[0] != 'N';
        }
    }

    if (checkInsufficientMatingMaterial()) {
        if (drawSetting == FORCE) {
            return 1;
        }
        else {
            printf("Insufficient mating material. Would you like to claim a draw? (y/n)\n");
            getLine();
            return lastLine[0] != '\0' && lastLine[0] != '\n' && lastLine[0] != 'n' && lastLine[0] != 'N';
        }
    }

    return 0;
}

// Set the xth square in the FEN code order to piece on the board. Return the board square.
char setFENBoard(char* b, int x, char piece) {
    char square = (7 - (x / 8)) * 8 + (x % 8);
    b[square] = piece;
    return square;
}

// Get input and parse the FEN code stored in lastLine and return 1 if valid. Must handle empty line case (returns 0) outside this function.
// Parameters must be the allocated board and miscs. They get cleared and replaced with the FEN data.
bool parseFEN(char* b, char* m) {
    getLine();

    if (lastLine[0] == '\n' || lastLine[0] == '\0') {
        return 0;
    }

    int l = -1;
    for (int i = 1; i < MAX_LINE_SIZE; i++) {
        if (lastLine[i] == '\n' || lastLine[i] == '\0') {
            l = i;
            break;
        }
    }
    if (l == -1) {
        printf("FEN code must be a valid string with length less than %i.\n", MAX_LINE_SIZE);
        return 0;
    }
    else if (l < 15) {
        printf("FEN code must be at least 15 characters long.\n");
        return 0;
    }
    else if (l > 99) {
        printf("FEN code must be at most 99 characters long.\n");
        return 0;
    }

    int numWhiteKings = 0;
    int numBlackKings = 0;

    // Set the default board and miscs data in case they do not change.
    for (int i = 0; i < 64; i++) {
        b[i] = EMPTY;
    }
    m[EN_PASSANT_FILE] = -1;
    m[FIFTY_MOVE_COUNTER] = 0;
    m[SQUARE_FROM] = UNDEFINED;
    m[SQUARE_TO] = UNDEFINED;
    m[GAME_STATE] = NORMAL; // TODO: DECIDE WHAT TO MAKE THIS IF THERE IS EVIDENTLY A CHECKMATE/STALEMATE

    // Parse the piece locations on the board.
    int x = 0;
    int pos = 0;
    for (;; pos++) {
        if (x >= 64) break;

        switch (lastLine[pos]) {

        case '\n':
        case '\0':
            printf("FEN code ended early at board square %i.\n", x);
            return 0;

        case 'P':
            setFENBoard(b, x++, wPAWN);
            break;
        case 'N':
            setFENBoard(b, x++, wKNIGHT);
            break;
        case 'B':
            setFENBoard(b, x++, wBISHOP);
            break;
        case 'R':
            setFENBoard(b, x++, wROOK);
            break;
        case 'Q':
            setFENBoard(b, x++, wQUEEN);
            break;
        case 'K':
            m[wKING_SQUARE] = setFENBoard(b, x++, wKING);
            numWhiteKings++;
            break;

        case 'p':
            setFENBoard(b, x++, bPAWN);
            break;
        case 'n':
            setFENBoard(b, x++, bKNIGHT);
            break;
        case 'b':
            setFENBoard(b, x++, bBISHOP);
            break;
        case 'r':
            setFENBoard(b, x++, bROOK);
            break;
        case 'q':
            setFENBoard(b, x++, bQUEEN);
            break;
        case 'k':
            m[bKING_SQUARE] = setFENBoard(b, x++, bKING);
            numBlackKings++;
            break;

        default:
            // Numbers indicate that many empty consecutive board spaces.
            if (lastLine[pos] >= '0' && lastLine[pos] <= '8') {
                int c = lastLine[pos] - '0';
                x += c;
            }
        }
    }

    if (numWhiteKings != 1) {
        printf("Number of white kings (K) in FEN code must be 1 and is %i.\n", numWhiteKings);
        return 0;
    }
    if (numBlackKings != 1) {
        printf("Number of black kings (k) in FEN code must be 1 and is %i.\n", numBlackKings);
        return 0;
    }

    // Set the player turn.
    while (1) {
        if (lastLine[pos] == '\n' || lastLine[pos] == '\0') {
            printf("FEN code ended early at player turn indicator.\n");
            return 0;
        }
        else if (lastLine[pos] == 'w' || lastLine[pos] == 'W') {
            m[PLAYER_TURN] = WHITE;
            break;
        }
        else if (lastLine[pos] == 'b' || lastLine[pos] == 'B') {
            m[PLAYER_TURN] = BLACK;
            break;
        }
        pos++;
    }


    // Assume we can castle if kings and rooks are in the right positions.
    if (b[4] == wKING) {
        if (b[0] == wROOK) m[wQUEENSIDE_CASTLE] = 1;
        if (b[7] == wROOK) m[wKINGSIDE_CASTLE] = 1;
    }
    if (b[60] == bKING) {
        if (b[56] == bROOK) m[bQUEENSIDE_CASTLE] = 1;
        if (b[63] == bROOK) m[bKINGSIDE_CASTLE] = 1;
    }

    return 1;
}

// Get a valid FEN code from the user and return 0 if the user enters a blank line.
bool getFEN(char* b, char* m) {
    while (!parseFEN(b, m)) {
        if (lastLine[0] == '\n' || lastLine[0] == '\0') return 0;
        printf("Type a valid FEN code: ");
    }

    return 1;
}

// Plays a game between the player and engine.
void play1Player() {
    clearConsole();

    setupBoard();

    printf("Enter a starting FEN code or a blank line for the default starting position: ");
    if (!getFEN(gameBoardHistory[0], gameMiscsHistory[0])) {
        setupBoard();
    }

    printf("Enter engine difficulty (%i-%i): ", DIFFICULTY_MIN, DIFFICULTY_MAX);
    int difficulty = getNumeric();
    while (difficulty < DIFFICULTY_MIN || difficulty > DIFFICULTY_MAX) {
        printf("Difficulty must be between %i and %i, inclusive: ", DIFFICULTY_MIN, DIFFICULTY_MAX);
        difficulty = getNumeric();
    }

    printf("Choose white, black, or random (w/b/any): ");
    switch (getChar()) {
    case '\n':
        return;
    case 'w':
    case 'W':
        playerRole = 0;
        break;
    case 'b':
    case 'B':
        playerRole = 1;
        break;
    default:
        playerRole = random() % 2;
        break;
    }

    while (1) {

        clearConsole();

        drawBoard(gameBoardHistory[gameLength - 1], gameMiscsHistory[gameLength - 1][PLAYER_TURN]);

        // Allocate space for this position.
        gameLength++;
        gameBoardHistory = (char**)realloc(gameBoardHistory, gameLength * sizeof(char*));
        gameMiscsHistory = (char**)realloc(gameMiscsHistory, gameLength * sizeof(char*));
        gameBoardHistory[gameLength - 1] = (char*)calloc(64, 1);
        gameMiscsHistory[gameLength - 1] = (char*)calloc(MISC_SIZE, 1);

        // Copy the previous board and miscs to this board and miscs.
        for (int i = 0; i < 64; i++) {
            gameBoardHistory[gameLength - 1][i] = gameBoardHistory[gameLength - 2][i];
        }
        for (int i = 0; i < MISC_SIZE; i++) {
            gameMiscsHistory[gameLength - 1][i] = gameMiscsHistory[gameLength - 2][i];
        }

        // Get the move to play next and store it in gameMiscsHistory[gameLength - 1].
        if (playerRole == gameMiscsHistory[gameLength - 2][PLAYER_TURN]) {
            // Player plays.
            bool play = getMove(gameBoardHistory[gameLength - 1], gameMiscsHistory[gameLength - 1]);
            if (!play) return;
        }
        else {
            // Engine plays.
            double t = evaluationTimeLimitMin + ((double)random() / (double)ULLONG_MAX) * (evaluationTimeLimitMax - evaluationTimeLimitMin);

            setupEvaluation(gameBoardHistory[gameLength - 1], gameMiscsHistory[gameLength - 1]);
            evaluatePositionTimed(t);

            int child = chooseMove(difficulty);
            if (child == -1) {
                printf("Engine could not find a move. Ending the game.\n");
                break;
            }

            char* childMiscs = nodeMiscs + child * MISC_SIZE;
            int moveFrom = childMiscs[SQUARE_FROM];
            int moveTo = childMiscs[SQUARE_TO];

            gameMiscsHistory[gameLength - 1][SQUARE_FROM] = moveFrom;
            gameMiscsHistory[gameLength - 1][SQUARE_TO] = moveTo;
            gameMiscsHistory[gameLength - 1][PLAYER_TURN] = 1 - gameMiscsHistory[gameLength - 2][PLAYER_TURN];
        }

        if (playAndCheckEndOfGame()) break;

        if (drawSetting == FORCE || drawSetting == ASK) {
            if (checkDraws()) break;
        }
    }
}

// Plays a game between two players.
void play2Player() {
    clearConsole();

    setupBoard();

    printf("Enter a starting FEN code or a blank line for the default starting position: ");
    if (!getFEN(gameBoardHistory[0], gameMiscsHistory[0])) {
        setupBoard();
    }

    while (1) {

        clearConsole();
        drawBoard(gameBoardHistory[gameLength - 1], gameMiscsHistory[gameLength - 1][PLAYER_TURN]);

        // Allocate space for this position.
        gameLength++;
        gameBoardHistory = (char**)realloc(gameBoardHistory, gameLength * sizeof(char*));
        gameMiscsHistory = (char**)realloc(gameMiscsHistory, gameLength * sizeof(char*));

        gameBoardHistory[gameLength - 1] = (char*)calloc(64, 1);
        gameMiscsHistory[gameLength - 1] = (char*)calloc(MISC_SIZE, 1);

        bool playerTurn = gameMiscsHistory[gameLength - 1][PLAYER_TURN];

        // Copy the previous board and miscs to this board and miscs.
        for (int i = 0; i < 64; i++) {
            gameBoardHistory[gameLength - 1][i] = gameBoardHistory[gameLength - 2][i];
        }
        for (int i = 0; i < MISC_SIZE; i++) {
            gameMiscsHistory[gameLength - 1][i] = gameMiscsHistory[gameLength - 2][i];
        }

        // Either player plays.
        bool play = getMove(gameBoardHistory[gameLength - 1], gameMiscsHistory[gameLength - 1]);
        if (!play) return;

        if (playAndCheckEndOfGame()) break;

        if (drawSetting == FORCE || drawSetting == ASK) {
            if (checkDraws()) break;
        }
    }
}

// Analyze a position typed by the user.
void analyzePosition() {
    printf("Enter a position FEN code to analyze: ");

    if (!getFEN(analysisBoard, analysisMiscs)) {
        return;
    }

    bool playerTurn = analysisMiscs[PLAYER_TURN];
    drawBoard(analysisBoard, playerTurn);

    printf("Analyzing for %f seconds...\n\n", evaluationTimeLimitAnalysis);

    setupEvaluation(analysisBoard, analysisMiscs);
    evaluatePositionTimed(evaluationTimeLimitAnalysis);
    sortChoices();

    // Print the choices and their evals.

    int numChoices = nodeNumChildren[0];
    printf("Analyzed for max %f seconds and found %i moves for ", evaluationTimeLimitAnalysis, numChoices);
    playerTurn ? printf("Black") : printf("White");
    printf(" with %i nodes (%i queued).\n", numNodes, futuresHeapLength - 1);

    printf("# nodes added to tree / added to heap / removed from heap: %i/%i/%i\n", calcNumNodesAddedTree, calcNumNodesAddedHeap, calcNumNodesRemovedHeap);

    for (int i = 0; i < numChoices; i++) {
        printf(getMoveHuman(i));

        printf("\t");
        if (usePlusesOnEvalNumbers && nodeEvalSorted[i] > 0.0) {
            printf("+");
        }
        printf("%.3f\n", nodeEvalSorted[i]);
        int index = nodeIndexSorted[i];
        int nc = nodeNumChildren[index];
        int k = nodeChildStartIndex[index];
        for (int j = 0; j < nc; j++) {
            int child = k + j;
            char* childMiscs = nodeMiscs + child * MISC_SIZE;
            printf("   %i to %i: %i -> %i %f\n", index, child, childMiscs[SQUARE_FROM], childMiscs[SQUARE_TO], nodeEval[child]);
        }
    }
    printf("\n");

}

// Main driver menu.
bool menu() {
    printf("Enter a blank line at any time to return to this menu.\n");
    printf("Play 1 player (1), 2 players (2), train engine (t), analyze a position (p), or settings (s), or anything else to exit: ");

    switch (getChar()) {
    case '1':
        play1Player();
        break;
    case '2':
        play2Player();
        break;
    case 't':
    case 'T':
        //train();
        break;
    case 'p':
    case 'P':
        analyzePosition();
        break;
    case 's':
    case 'S':
        //settings();
        break;
    case '\n':
        return 0;
    }

    return 1;
}

void resetConsoleBuffer() {
    if (lastLine == NULL) lastLine = (char*)calloc(MAX_LINE_SIZE, 1);

    for (int i = 0; i < MAX_LINE_SIZE; i++) {
        lastLine[i] = 0;
    }
}

// Run the user interface application.
void runUI() {


    // Loop the menu screen if the user returns to the menu at any time.
    while (menu()) {}
}

/*
// Read an underscore-terminated integer from in and set pos to the index after the underscore.
int readInt(char* in, int* pos) {
    int x = 0;

    bool neg = 0;
    if (in[*pos] == '-') {
        neg = 1;
        (*pos)++;
    }

    // Read this number.
    while (in[*pos] != '_') {
        x *= 10;
        x += in[*pos] - '0';
        (*pos)++;
    }

    (*pos)++;

    if (neg) x *= -1;

    return x;
}*/

// Read a position code and allocate and set the analysisBoard and analysisMiscs to the position.
void readPosition(char* in) {

    setupAnalysisBoard();

    // Fill the analysisBoard and analysisMiscs with chars from input.
    for (int i = 0; i < 64; i++) {
        analysisBoard[i] = in[i];
    }
    for (int i = 0; i < MISC_SIZE; i++) {
        analysisMiscs[i] = in[i + 64];
    }
}

// Run the setup for analysis operation after init has been called.
void runSetupAnalysis(char* in, int d1, int d2, int d3) {

    // Set settings based on the details.
    evaluationTimeMeasurementInterval = d1;
    evaluationDepthLimit = d2;
    evaluationNodeLimit = d3;

    readPosition(in);
    setupEvaluation(analysisBoard, analysisMiscs);
}

// Run the analyze for timeLimitMS ms operation after runSetupAnalysis has been called.
char runAnalysis(int timeLimitMS) {

    evaluationTimeLimitAnalysis = (double)timeLimitMS / 1000.0;

    // Analyze the position.
    char result = evaluatePositionTimed(evaluationTimeLimitAnalysis);

    // Copy and sort the movefroms, movetos, and evals of node 0's children.
    sortChoices();

    return result;
}

int getNumChoices() {
    return nodeNumChildren[0];
}

void* getFrom() {
    int numChoices = nodeNumChildren[0];
    char* o = calloc(numChoices, 1);
    for (int i = 0; i < numChoices; i++) {
        char* childMiscs = nodeMiscs + nodeIndexSorted[i] * MISC_SIZE;
        o[i] = childMiscs[SQUARE_FROM];
    }
    return (void*)o;
}

void* getTo() {
    int numChoices = nodeNumChildren[0];
    char* o = calloc(numChoices, 1);
    for (int i = 0; i < numChoices; i++) {
        char* childMiscs = nodeMiscs + nodeIndexSorted[i] * MISC_SIZE;
        o[i] = childMiscs[SQUARE_TO];
    }
    return (void*)o;
}

char** getString() {
    int numChoices = nodeNumChildren[0];
    char** o = calloc(numChoices, sizeof(char*));
    for (int i = 0; i < numChoices; i++) {
        o[i] = getMoveHuman(i);
    }
    return (void*)o;
}

double* getEval() {
    int numChoices = nodeNumChildren[0];
    double* o = calloc(numChoices, 8);
    for (int i = 0; i < numChoices; i++) {
        o[i] = nodeEvalSorted[i];
    }
    return o;
}

int getNumNodesAddedTree() {
    return calcNumNodesAddedTree;
}

int getNumNodesAddedHeap() {
    return calcNumNodesAddedHeap;
}

int getNumNodesRemovedHeap() {
    return calcNumNodesRemovedHeap;
}

// Run the legality operation.
// Return a 1 or 0 depending on whether the given move is legal on the given position.
char runLegality(char* in, char f, char t) {

    readPosition(in);

    int pos = 0;

    // Return if the move is legal.
    bool o = isLegalMove(analysisBoard, analysisMiscs, f, t, 0);

    if (o) return '1';
    return '0';
}

// Run the check operation.
// Return a 1 or 0 depending on whether the given king is in check on the given position.
char runCheck(char* in, char king) {

    readPosition(in);

    // Return if the king is not in check.
    bool o = 0;
    if (king == '1') {
        o = kingNotInCheck(analysisBoard, analysisMiscs[bKING_SQUARE], 1) ? '1' : '0';
    }
    else {
        o = kingNotInCheck(analysisBoard, analysisMiscs[wKING_SQUARE], 0) ? '1' : '0';
    }

    if (o) return '1';
    return '0';
}

int main(int argc, char* argv[]) {

    init(10000000);
    
    // These calls before analysis are only needed for this application, not other applications that use this one.
    setupAnalysisBoard();
    setupEvalBoards();
    resetConsoleBuffer();

    runUI();
    return 0;
}