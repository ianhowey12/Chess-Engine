#define DEBUG 0
#define PERFORMANCE 0
#define PRINT_PATHS 0
#define CHECK_FOR_CHECK 1

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <windows.h>
#include <thread>
#include <atomic>

using namespace std;

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

#define MAX_FILE_SIZE 1000000

// Movefrom and moveto of the root node (should not matter)
#define DEFAULT_MOVEFROMTO -1

// Global settings
bool unicodeEnabled = 0;
bool reverseWhiteBlackLetters = 0;
bool useAsterisk = 0;
bool showBoardCoordinates = 1;
bool useCapitalCoordinates = 1;
bool evaluationPrintChoices = 0;
bool usePlusesOnEvalNumbers = 1;
bool printAnalysisResponses = 0;
// Settings that affect the actual evaluation algorithm.
int evaluationTimeLimitMin = 1000; // ms
int evaluationTimeLimitMax = 1000; // ms
int evaluationTimeLimitAnalysis = 1000; // ms
int evaluationDepthLimit = 20; // Number of tree levels allowed. Nodes may be created at depth [0, evaluationDepthLimit) with the root having depth 0. evaluationDepthLimit must be 1 to create the root.

bool initComplete = 0;
bool setupComplete = 0;

enum drawSettings {
    NO_DRAWS = 0,
    ASK = 1,
    FORCE = 2
};
char drawSetting = ASK;



bool keyPrev[256];
bool keyCurr[256];

char* inLine;
int inLinePos = 0; // only used for reading commands from other applications, not typed user input

char* outLine;
int outLinePos = 0; // only used for writing commands to other applications, not printing for user to read

char* fileLine;
int fileLinePos = 0; // only used for reading and writing files

#define MAX_MOVE_STRING_LENGTH 10
char* moveString;
int moveStringLength = 0;

typedef short eval;


// Resizing info.
double nodeCapMultiplier = 1.5;
int nodeCapAdder = 10;

// Node sizing info.
#define MISC_SIZE 12
#define LEGAL_MOVES_UPPER_BOUND 350 // Must be >= the max # legal moves possible in any position.

// All information about a position node.
typedef struct {
    char moveFrom;
    char moveTo;
    short numChildren;
    int childStartIndex; // position in global array nodes, made an int so resizing does not change this location
    eval e;
    short cost; // the minimum cost of any path from this node to a leaf in its subtree
} node;


// The data source for the node tree.
atomic<int> numNodes;
atomic<int> nodeCap; // doesn't need to be modified by a random thread during evaluation unless resizing, which will break the multithreading and must be done after pausing all threads
node* nodes;
atomic<bool> nodeCapReached;

// The moves from the root which will be sorted by eval.
node* sortedMoves[LEGAL_MOVES_UPPER_BOUND]; // Length is stored in root as number of children.

// A position, including a board and some miscellaneous data.
typedef struct {
    char board[64];
    char wKINGSIDE_CASTLE;
    char wQUEENSIDE_CASTLE;
    char bKINGSIDE_CASTLE;
    char bQUEENSIDE_CASTLE;
    char EN_PASSANT_FILE;
    char FIFTY_MOVE_COUNTER;
    char wKING_SQUARE;
    char bKING_SQUARE;
    char SQUARE_FROM;
    char SQUARE_TO;
    char PLAYER_TURN;
    char GAME_STATE;
} position;

// Move for playing and undoing moves.
typedef struct {
    char f;
    char tt;
    char promotion;
    char mover;
    char captured;
    char enPassantSquare;
} M;

// An arbitrary value that should never be checked by the program.
#define UNDEFINED -1

// Search algorithm data.
#define MAX_DEPTH 100

int mainPathIndices[MAX_DEPTH];
int mainPathLength = 0;

// Information only accessed by one thread.
typedef struct {

    thread thr;

    // Allow this path to be accessed by other threads for backtracking.
    atomic<bool> hasPath;
    // The node indices in the path found by this thread.
    int nodePathIndices[MAX_DEPTH];
    int nodePathLength;

    // Calculating position to play and undo moves on.
    position pos;

    // All legal children of a position found and recorded before setting the examined node's child start.
    char childFroms[LEGAL_MOVES_UPPER_BOUND];
    char childTos[LEGAL_MOVES_UPPER_BOUND];
    eval childEvals[LEGAL_MOVES_UPPER_BOUND];
    eval bestChildEval;
    int childPoolLength;

    // Temporary pieces and their cumulative values for both players used to evaluate all moves from a position.
    eval whiteValue;
    eval blackValue;
    eval whiteValueChange;
    eval blackValueChange;
    int numEvalPieces;
    char evalPieces[64]; // There will never be more pieces on the board than board squares, so 64 is enough.
    char evalSquares[64];

} T;

T* threads;
int numThreads;
atomic<int> numThreadsAlive;

// Evaluation control data.
unsigned long long endTime = 0;
unsigned long long nodeAddLimit = 0;
unsigned long long nodeExamineLimit = 0;


// Combined stats from all threads.
atomic<int> calcNumNodesAdded;
atomic<int> calcNumNodesExamined;
// Average number of moves in a position is calculable from the above three stats.
atomic<int> calcNumStalematesFound;
atomic<int> calcNumWhiteWinsFound;
atomic<int> calcNumBlackWinsFound;
atomic<int> calcNumNormalsFound;

atomic<int> calcNumNodesAddedDepth[MAX_DEPTH];
atomic<int> calcNumNodesExaminedDepth[MAX_DEPTH];


// Search algorithm data accessed by multiple threads.
atomic<bool> treeLock;
atomic<bool> killThreads;

enum gameStates {
    NORMAL = 0,
    WHITE_WIN = 1,
    BLACK_WIN = 2,
    DRAW = 3
};

#define ROOT_SCORE 1.0
#define WHITE_WINS_EVAL 32767 // The eval of a White checkmate position.
#define BLACK_WINS_EVAL -32768 // The eval of a Black checkmate position.
#define DRAW_EVAL 0 // The eval of a stalemate position.
#define WHITE_WINS_EVAL_THRESHOLD 30000 // The minimum eval to be considered a forced mate by White.
#define BLACK_WINS_EVAL_THRESHOLD -30000 // The maximum eval to be considered a forced mate by Black.
#define EVAL_FORCED_MATE_INCREMENT 1 // The difference in eval between a checkmate and mate-in-one, etc.
#define TEMPO_EVAL 25

#define MAX_COST 32767


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


typedef struct {
    // Data read and written to files.
    eval weight[12][64][12][64];
} W;

// Basic data used to fill the point tables.
// King value is high only because king weights need to vary more than other pieces.
char startingPieceCounts[NUM_PIECES] = { 8, 2, 2, 2, 1, 1, 8, 2, 2, 2, 1, 1 };
eval piecePointValues[NUM_PIECES] = { 100, 300, 320, 500, 900, 100, -100, -300, -320, -500, -900, -100 };
#define TOTAL_MATERIAL 6940 // The sum of White's piece points at the beginning of the game.

// All weight tables that must be stored.
W* currentWeights; // A pointer to the table accessed by the engine. It gets set when changing the table to evaluate with.
W bestWeights; // The best table from training and the one always used when not training.
W* trainingWeights; // The tables used only for training.


position startingPos = {
    {
        3, 1, 2, 4, 5, 2, 1, 3,
        0, 0, 0, 0, 0, 0, 0, 0,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1,
        6, 6, 6, 6, 6, 6, 6, 6,
        9, 7, 8, 10, 11, 8, 7, 9
    },
    1, 1, 1, 1, -1, 0, 4, 60, UNDEFINED, UNDEFINED, WHITE, NORMAL
};

// The root position of an evaluation, used to get to the leaves.
position rootPosition;


// Data for playing against engine.
char playerRole = BLACK;
#define DIFFICULTY_MIN 0
#define DIFFICULTY_MAX 9

// Analysis position.
position analysisPos;

// All previous board states in this game including the current one.
position* history;
int gameLength = 0; // Number of positions in this game (length of history)



// Return true if the piece at square x on the calculating board is white.
#define ifWhite(x) char iw = b[x]; if(iw >= 0 && iw <= 5)

// Return true if the piece at square x on the calculating board is black.
#define ifBlack(x) char ib = b[x]; if(ib >= 6 && ib <= 11)

// Return true if the piece at square x on the calculating board is empty.
#define ifEmpty(x) if(b[x] == EMPTY)

// Return true if the piece at square x on the calculating board is not white.
#define ifNonWhite(x) char inw = b[x]; if(inw < 0 || inw > 5)

// Return true if the piece at square x on the calculating board is not black.
#define ifNonBlack(x) char inb = b[x]; if(inb < 6 || inb > 11)

// Return true if the piece at square x on the calculating board is not empty.
#define ifNonEmpty(x) if(b[x] != EMPTY)

#define mv(y) examineMove(t, x, y)

unsigned long long getTime() {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    unsigned long long t = ((unsigned long long)now.tv_sec % (unsigned long long)1000000000) * (unsigned long long)1000000000 + (unsigned long long)now.tv_nsec;
    return t;
}

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
    randState += 0x51f84b2308a7d929;
    randPrev = (randPrev * 0xa63e40147c582b49 + (randState >> 1)) * 0x681ac9427d5fe8b3;
    return randPrev;
}

// Get a random i64 in the given range, inclusive.
long long randomRange(long long l, long long r) {
    unsigned long long range = (r - l) + 1;
    unsigned long long x = random() % range;
    return l + (long long)x;
}

// Clear the console window.
void clearConsole() {
    #if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        system("clear");
    #endif

    #if defined(_WIN32) || defined(_WIN64)
        system("cls");
    #endif
}

// Get the Unicode value for a piece type.
char* getUnicodeValue(char* b, char x) {
    switch (b[x]) {
        case -1:
            if ((x + (x / 8)) % 2) {
                return (char*)"\u2588\u2588";
            }
            else {
                return (char*)"\u0020\u0020";
            }
        case 0: return (char*)"\u265f\u0020";
        case 1: return (char*)"\u265e\u0020";
        case 2: return (char*)"\u265d\u0020";
        case 3: return (char*)"\u265c\u0020";
        case 4: return (char*)"\u265b\u0020";
        case 5: return (char*)"\u265a\u0020";
        case 6: return (char*)"\u2659\u0020";
        case 7: return (char*)"\u2658\u0020";
        case 8: return (char*)"\u2657\u0020";
        case 9: return (char*)"\u2656\u0020";
        case 10: return (char*)"\u2655\u0020";
        case 11: return (char*)"\u2654\u0020";
    }

    return (char*)"\u0020\u0020";
}

// Write the UTF-8 encoded string of a Unicode character.
void writeUnicode(unsigned int u, char* s, int* p) {

    if (u <= 0x7F) {
        s[*p] = u;
        (*p)++;
    }
    if (u <= 0x7FF) {
        s[*p] = 0xC0 | (u >> 6);
        (*p)++;
        s[*p] = 0x80 | (u & 0x3F);
        (*p)++;
    }
    if (u <= 0xFFFF) {
        if (u >= 0xD800 && u <= 0xDFFF) return;
        s[*p] = 0xE0 | (u >> 12);
        (*p)++;
        s[*p] = 0x80 | ((u >> 6) & 0x3F);
        (*p)++;
        s[*p] = 0x80 | (u & 0x3F);
        (*p)++;
    }
    if (u <= 0x10FFFF) {
        s[*p] = 0xF0 | (u >> 18);
        (*p)++;
        s[*p] = 0x80 | ((u >> 12) & 0x3F);
        (*p)++;
        s[*p] = 0x80 | ((u >> 6) & 0x3F);
        (*p)++;
        s[*p] = 0x80 | (u & 0x3F);
        (*p)++;
    }
}

void append(char* s, int* p, char* t) {
    for (int i = 0;; i++) {
        if (t[i] == '\0') return;
        s[*p] = t[i];
        (*p)++;
    }
}


void appendChar(char* s, int* p, char t) {
    s[*p] = t;
    (*p)++;
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
    return useAsterisk ? '*' : '.';
}

// Draw the given board on screen.
void drawBoard(char* b, bool playerTurn) {
    char* s = (char*)calloc(1000, 1);
    int P = 0;
    int* p = &P;

    if (unicodeEnabled) {
        append(s, p, (char*)"\u250f");
        for (int i = 0; i < 16; i++) {
            append(s, p, (char*)"\u2501");
        }
        append(s, p, (char*)"\u2513");
        append(s, p, (char*)"\u000a");

        for (int y = 0; y < 8; y++) {
            append(s, p, (char*)"\u2503");
            for (int x = 0; x < 8; x++) {
                char square = playerTurn ? y * 8 + (7 - x) : (7 - y) * 8 + x;
                append(s, p, getUnicodeValue(b, square));
                //if (u == 0x2588) {
                //    writeUnicode(0x2588, s, p);
                //}
                //else {
                    //writeUnicode(0x0020, s, p);
                //}
            }
            append(s, p, (char*)"\u2503");
            if (showBoardCoordinates) {
                if (playerTurn) {
                    appendChar(s, p, '1' + y);
                }
                else {
                    appendChar(s, p, '8' - y);
                }
            }
            append(s, p, (char*)"\u000a");
        }

        append(s, p, (char*)"\u2517");
        for (int i = 0; i < 16; i++) {
            append(s, p, (char*)"\u2501");
        }
        append(s, p, (char*)"\u251b");
        append(s, p, (char*)"\u000a");
    }
    else {
        append(s, p, (char*)"-------------------\n");
        for (int y = 0; y < 8; y++) {
            append(s, p, (char*)"| ");
            for (int x = 0; x < 8; x++) {
                char square = playerTurn ? y * 8 + (7 - x) : (7 - y) * 8 + x;
                s[P] = pieceTypeToChar(b[square]);
                P++;
                s[P] = ' ';
                P++;
            }
            append(s, p, (char*)"|");
            if (showBoardCoordinates) {
                char c[] = " ";
                if (playerTurn) {
                    s[P] = '1' + y;
                    P++;
                }
                else {
                    s[P] = '8' - y;
                    P++;
                }
            }
            s[P] = '\n';
            P++;
        }
        append(s, p, (char*)"-------------------\n");
    }

    printf(s);

    if (showBoardCoordinates) {
        if (useCapitalCoordinates) {
            if (playerTurn) {
                printf("  H G F E D C B A\n\n");
            }
            else {
                printf("  A B C D E F G H \n\n");
            }
        }
        else {
            if (playerTurn) {
                printf("  h g f e d c b a\n\n");
            }
            else {
                printf("  a b c d e f g h \n\n");
            }
        }
    }
    else {
        printf("\n\n");
    }
}

// Setup the board to the starting game position given all the references of the position data.
void setupBoard() {
    gameLength = 1;
    history = (position*)realloc(history, sizeof(position));
    history[0] = startingPos;
}

// Play a given move on the given position and update all data except from, to, and player turn.
void playMoveUpdating(position* pos) {
    char from = pos->SQUARE_FROM;
    char to = pos->SQUARE_TO;
    char* b = pos->board;

    // Get the type of piece being promoted to or negative if no promotion.
    char promotion = (to - 64) / 8;

    // Set moveTo to the true destination square.
    if (to >= 96) {
        to = to % 8;
    }
    else if (to >= 64) {
        to = 56 + (to % 8);
    }

    char rf = from / 8, cf = from % 8, rt = to / 8, ct = to % 8;
    char p = b[from];
    char q = b[to];

    if (pos->FIFTY_MOVE_COUNTER < 100) (pos->FIFTY_MOVE_COUNTER)++;

    // If capturing, reset 50-move counter.
    bool capture = 0;
    if (q != EMPTY) {
        pos->FIFTY_MOVE_COUNTER = 0;
        capture = 1;
    }

    pos->EN_PASSANT_FILE = -1;

    // Make default move at beginning - it will be overridden by pawn promotions.
    b[to] = p;
    b[from] = EMPTY;

    switch (p) {
    case wPAWN:
        pos->FIFTY_MOVE_COUNTER = 0; // 50-move rule
        if (rf == 1 && rt == 3) { // en passant availability
            pos->EN_PASSANT_FILE = ct;
        }
        else if (promotion > -1) { // white promotion
            b[to] = promotion + 1;
        }
        else if (rf == 4 && !capture && cf != ct) { // white en passant
            b[to - 8] = EMPTY;
        }
        break;
    case bPAWN:
        pos->FIFTY_MOVE_COUNTER = 0; // 50-move rule
        if (rf == 6 && rt == 4) { // en passant availability
            pos->EN_PASSANT_FILE = ct;
        }
        else if (promotion > -1) { // black promotion
            b[to] = promotion + 3;
        }
        else if (rf == 3 && !capture && cf != ct) { // black en passant
            b[to + 8] = EMPTY;
        }
        break;
    case wKING:
        pos->wKINGSIDE_CASTLE = 0;
        pos->wQUEENSIDE_CASTLE = 0;
        pos->wKING_SQUARE = to;
        if (from == 4 && to == 6) { // WK
            b[5] = wROOK; b[7] = EMPTY;
        }
        else if (from == 4 && to == 2) { // WQ
            b[3] = wROOK; b[0] = EMPTY;
        }
        break;
    case bKING:
        pos->bKINGSIDE_CASTLE = 0;
        pos->bQUEENSIDE_CASTLE = 0;
        pos->bKING_SQUARE = to;
        if (from == 60 && to == 62) { // BK
            b[61] = bROOK; b[63] = EMPTY;
        }
        else if (from == 60 && to == 58) { // BQ
            b[59] = bROOK; b[56] = EMPTY;
        }
        break;
    case wROOK:
        if (from == 7) {
            pos->wKINGSIDE_CASTLE = 0;
        }
        else if (from == 0) {
            pos->wQUEENSIDE_CASTLE = 0;
        }
        break;
    case bROOK:
        if (from == 63) {
            pos->bKINGSIDE_CASTLE = 0;
        }
        else if (from == 56) {
            pos->bQUEENSIDE_CASTLE = 0;
        }
        break;
    }
}

// Play a given move on the given board without updating miscs.
// Return the en passant square or -1.
char playMove(char* b, M* move) {
    char eps = -1;

    // Set moveTo to the true destination square.
    char from = move->f;
    char tt = move->tt;

    char p = b[from];
    char q = b[tt];

    // Make default move at beginning - it will be overridden by pawn promotions.
    b[tt] = p;
    b[from] = EMPTY;

    switch (p) {
    case wPAWN:
        if (move->promotion > -1) { // white promotion
            b[tt] = move->promotion;
        }
        else if (from % 8 != tt % 8 && q == EMPTY) { // white en passant
            b[tt - 8] = EMPTY;
            eps = tt - 8;
        }
        break;
    case bPAWN:
        if (move->promotion > -1) { // black promotion
            b[tt] = move->promotion;
        }
        else if (from % 8 != tt % 8 && q == EMPTY) { // black en passant
            b[tt + 8] = EMPTY;
            eps = tt + 8;
        }
        break;
    case wKING:
        if (from == 4 && tt == 6) { // WK
            b[5] = wROOK; b[7] = EMPTY;
        }
        else if (from == 4 && tt == 2) { // WQ
            b[3] = wROOK; b[0] = EMPTY;
        }
        break;
    case bKING:
        if (from == 60 && tt == 62) { // BK
            b[61] = bROOK; b[63] = EMPTY;
        }
        else if (from == 60 && tt == 58) { // BQ
            b[59] = bROOK; b[56] = EMPTY;
        }
        break;
    }

    return eps;
}

// Undo a move on this thread's calculating board.
inline void undoMove(char* b, M* m) {

    b[m->f] = m->mover;
    b[m->tt] = m->captured;
    
    // Undo an en passant move.
    if (m->enPassantSquare > -1) {
        b[m->enPassantSquare] = m->mover == wPAWN ? bPAWN : wPAWN;
    }
    else {
        // Undo a castling move.
        if (m->f == 4 && m->mover == wKING) {
            if (m->tt == 6) {
                b[5] = EMPTY;
                b[7] = wROOK;
            } else if(m->tt == 2) {
                b[3] = EMPTY;
                b[0] = wROOK;
            }
        }else if (m->f == 60 && m->mover == bKING) {
            if (m->tt == 62) {
                b[61] = EMPTY;
                b[63] = bROOK;
            }
            else if (m->tt == 58) {
                b[59] = EMPTY;
                b[56] = bROOK;
            }
        }
    }
}

// Return whether the given king is being attacked on the board given the king's square.
bool kingNotInCheck(char* b, char x) {

    #if DEBUG
        if (b[x] != wKING && b[x] != bKING) {
            printf("DEBUG: b[%i] is %i instead of a king.\n", x, b[x]);
        }
    #endif

    char r = x / 8, c = x % 8;
    bool isBlack = b[x] == bKING;

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

// Get the total value of all of White's pieces and the total value of all of Black's pieces.
// This function is called before finding all moves in a position and evaluating them.
inline void evalFullBoard(T* t) {
    position* pos = &(t->pos);
    char* b = pos->board;
    W* w = currentWeights;
    int np = t->numEvalPieces;

    t->whiteValue = 0;
    t->blackValue = 0;

    // Iterate over all pieces on the board.
    for (int i = 0; i < np; i++) {

        char aPiece = t->evalPieces[i];
        char aSquare = t->evalSquares[i];

        if (aPiece == -1) continue;

        // Iterate over all pieces on the board.
        for (int j = 0; j < np; j++) {

            char bPiece = t->evalPieces[j];
            char bSquare = t->evalSquares[j];

            eval x = w->weight[bPiece][bSquare][aPiece][aSquare];

            // Add the weight of a affecting b.
            if (bPiece >= wPAWN && bPiece <= wKING) {
                t->whiteValue += x;
            }
            else {
                t->blackValue += x;
            }
        }
    }
}

// Get the total value of all of White's pieces and the total value of all of Black's pieces.
// This function is called to debug and check if the optimized move eval algorithm works correctly.
inline void evalFullBoardDebug(char* b, eval* whiteValue, eval* blackValue) {
    W* w = currentWeights;

    // Iterate over all squares on the board.
    for (int i = 0; i < 64; i++) {
        char x = b[i];
        if (x == EMPTY) continue;

        // Iterate over all squares on the board.
        for (int j = 0; j < 64; j++) {
            char y = b[j];
            if (y == EMPTY) continue;

            // Add the weight of x affecting y.
            if (y >= wPAWN && y <= wKING) {
                *whiteValue += w->weight[y][j][x][i];
            }
            else {
                *blackValue += w->weight[y][j][x][i];
            }
        }
    }
}

// Compute the eval of a position given the values of White's and Black's pieces.
inline eval finalEval(short whiteValue, short blackValue) {
    double sum = (double)whiteValue + (double)blackValue; // Example: 15.0 + -12.0 = 3.0
    double diff = (double)whiteValue - (double)blackValue; // Example: 15.0 - -12.0 = 27.0
    if (diff < 1) diff = 1;
    double o = TOTAL_MATERIAL * 2.0 * sum / diff; // Example: 3940 * 3.0 / 27.0 = 437.777777778 units.
    // We use TOTAL_MATERIAL * 2.0 above so that when all pieces are on the board, the diff is approximately TOTAL_MATERIAL * 2.0 so the sum of piece values equals the actual eval.
    if (o >= WHITE_WINS_EVAL_THRESHOLD - 1) {
        o = WHITE_WINS_EVAL_THRESHOLD - 1;
    }
    else if (o <= BLACK_WINS_EVAL_THRESHOLD + 1) {
        o = BLACK_WINS_EVAL_THRESHOLD + 1;
    }

    return o;
}

/*
Return the difference in eval between this position and what this position would be after making this move.
We already have a list of pieces and locations before making the move.
First, we list the pieces and locations of piece births and deaths that happen during the move.
Next, we calculate t->whiteValueChange and t->blackValueChange from the three lists above and currentWeights.
Finally, we compute and return what the eval would be after adding those value changes to both players' values.
 */
inline eval computeEvalMove(T* t, char moveFrom, char trueMoveto, char promotion) {
    position* pos = &(t->pos);
    char* b = pos->board;
    char x = b[moveFrom];
    char y = b[trueMoveto];

    char birthPieces[2]; // At most 2 piece births and deaths occur in any move, including castling, promotions, and en passants.
    char birthSquares[2];
    int numBirthPieces = 0;
    char deathPieces[2];
    char deathSquares[2];
    int numDeathPieces = 0;

    // Account for the captured piece.
    if (y != EMPTY) {
        #if DEBUG
        if (trueMoveto > 63 || trueMoveto < 0) {
            printf("DEBUG: While evaluating a move, trueMoveto was %i, outside [%i, %i].\n", trueMoveto, 0, 63);
        }
        if (y > 11 || y < -1) {
            printf("DEBUG: While evaluating a move, trueMoveto piece was %i, outside [%i, %i].\n", y, -1, 11);
        }
        #endif

        // Record a piece death for the piece being captured.
        deathPieces[numDeathPieces] = y;
        deathSquares[numDeathPieces] = trueMoveto;
        numDeathPieces++;
    }
    
    #if DEBUG
    if (moveFrom > 63 || moveFrom < 0) {
        printf("DEBUG: While evaluating a move, moveFrom was %i, outside [%i, %i].\n", moveFrom, 0, 63);
    }
    if (x > 11 || x < -1) {
        printf("DEBUG: While evaluating a move, moveFrom piece was %i, outside [%i, %i].\n", x, -1, 11);
    }
    #endif

    // Record a piece death for moving from.
    deathPieces[numDeathPieces] = x;
    deathSquares[numDeathPieces] = moveFrom;
    numDeathPieces++;
    
    // Record a piece birth for moving to using the piece on the trueMoveto square after the move.
    if (promotion == -1) {
        birthPieces[numBirthPieces] = x;
        birthSquares[numBirthPieces] = trueMoveto;
        numBirthPieces++;
    }
    else {
        birthPieces[numBirthPieces] = promotion;
        birthSquares[numBirthPieces] = trueMoveto;
        numBirthPieces++;
    }

    if (x == wKING) {
        // Account for castling: record a piece birth and a piece death for the rook move.
        int d = trueMoveto - moveFrom;
        if (d == 2) {
            birthPieces[numBirthPieces] = wROOK;
            birthSquares[numBirthPieces] = 5;
            numBirthPieces++;
            deathPieces[numDeathPieces] = wROOK;
            deathSquares[numDeathPieces] = 7;
            numDeathPieces++;
        }
        else if (d == -2) {
            birthPieces[numBirthPieces] = wROOK;
            birthSquares[numBirthPieces] = 3;
            numBirthPieces++;
            deathPieces[numDeathPieces] = wROOK;
            deathSquares[numDeathPieces] = 0;
            numDeathPieces++;
        }
    } else if (x == bKING) {
        // Account for castling: record a piece birth and a piece death for the rook move.
        int d = trueMoveto - moveFrom;
        if (d == 2) {
            birthPieces[numBirthPieces] = bROOK;
            birthSquares[numBirthPieces] = 61;
            numBirthPieces++;
            deathPieces[numDeathPieces] = bROOK;
            deathSquares[numDeathPieces] = 63;
            numDeathPieces++;
        }
        else if (d == -2) {
            birthPieces[numBirthPieces] = bROOK;
            birthSquares[numBirthPieces] = 59;
            numBirthPieces++;
            deathPieces[numDeathPieces] = bROOK;
            deathSquares[numDeathPieces] = 56;
            numDeathPieces++;
        }
    } else if (x == wPAWN) {
        // Account for en passant: record a piece death for the pawn being captured.
        if (trueMoveto % 8 != moveFrom % 8 && y == EMPTY) {
            deathPieces[numDeathPieces] = bPAWN;
            deathSquares[numDeathPieces] = trueMoveto - 8;
            numDeathPieces++;
        }
    } else if (x == bPAWN) {
        // Account for en passant: record a piece death for the pawn being captured.
        if (trueMoveto % 8 != moveFrom % 8 && y == EMPTY) {
            deathPieces[numDeathPieces] = wPAWN;
            deathSquares[numDeathPieces] = trueMoveto + 8;
            numDeathPieces++;
        }
    }

    // Determine t->whiteValueChange and t->blackValueChange resulting from this move.
    int numPreviousPieces = t->numEvalPieces;
    W* w = currentWeights;
    t->whiteValueChange = 0;
    t->blackValueChange = 0;

    // For each birth, we add previous, then subtract deaths and add births to have the same effect as adding next.
    for (int i = 0; i < numBirthPieces; i++) {
        char aPiece = birthPieces[i];
        char aSquare = birthSquares[i];
        eval* weight = (eval*)w->weight + (aPiece * 64 + aSquare) * NUM_PIECES * 64;

        // Add the weight of previous affecting this previous piece.
        for (int j = 0; j < numPreviousPieces; j++) {
            char bPiece = t->evalPieces[j];
            char bSquare = t->evalSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange += x;
            }
            else {
                t->blackValueChange += x;
            }
        }

        // Subtract the weight of deaths affecting this previous piece.
        for (int j = 0; j < numDeathPieces; j++) {
            char bPiece = deathPieces[j];
            char bSquare = deathSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange -= x;
            }
            else {
                t->blackValueChange -= x;
            }
        }

        // Add the weight of births affecting this previous piece.
        for (int j = 0; j < numBirthPieces; j++) {
            char bPiece = birthPieces[j];
            char bSquare = birthSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange += x;
            }
            else {
                t->blackValueChange += x;
            }
        }
    }

    // For each death, we subtract previous, then add deaths and subtract births to cancel out what we did for each previous (as deaths are included in previous).
    for (int i = 0; i < numDeathPieces; i++) {
        char aPiece = deathPieces[i];
        char aSquare = deathSquares[i];
        eval* weight = (eval*)w->weight + (aPiece * 64 + aSquare) * NUM_PIECES * 64;

        // Subtract the weight of previous affecting this previous piece.
        for (int j = 0; j < numPreviousPieces; j++) {
            char bPiece = t->evalPieces[j];
            char bSquare = t->evalSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange -= x;
            }
            else {
                t->blackValueChange -= x;
            }
        }

        // Add the weight of deaths affecting this previous piece.
        for (int j = 0; j < numDeathPieces; j++) {
            char bPiece = deathPieces[j];
            char bSquare = deathSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange += x;
            }
            else {
                t->blackValueChange += x;
            }
        }

        // Subtract the weight of births affecting this previous piece.
        for (int j = 0; j < numBirthPieces; j++) {
            char bPiece = birthPieces[j];
            char bSquare = birthSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange -= x;
            }
            else {
                t->blackValueChange -= x;
            }
        }
    }

    // For each previous, we subtract deaths and add births.
    for (int i = 0; i < numPreviousPieces; i++) {
        char aPiece = t->evalPieces[i];
        char aSquare = t->evalSquares[i];
        eval* weight = (eval*)w->weight + (aPiece * 64 + aSquare) * NUM_PIECES * 64;

        // Subtract the weight of deaths affecting this previous piece.
        for (int j = 0; j < numDeathPieces; j++) {
            char bPiece = deathPieces[j];
            char bSquare = deathSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange -= x;
            }
            else {
                t->blackValueChange -= x;
            }
        }

        // Add the weight of births affecting this previous piece.
        for (int j = 0; j < numBirthPieces; j++) {
            char bPiece = birthPieces[j];
            char bSquare = birthSquares[j];
            eval x = *(weight + bPiece * 64 + bSquare);

            if (aPiece >= wPAWN && aPiece <= wKING) {
                t->whiteValueChange += x;
            }
            else {
                t->blackValueChange += x;
            }
        }
    }

    eval whiteNew = t->whiteValue + t->whiteValueChange;
    eval blackNew = t->blackValue + t->blackValueChange;

#if DEBUG
    // Record the board state.
    char debugBoard[64];
    for (int i = 0; i < 64; i++) {
        debugBoard[i] = b[i];
    }

    // Play this move.
    M debugMove;
    debugMove.captured = 0; 
    debugMove.f = moveFrom;
    debugMove.tt = trueMoveto;
    debugMove.promotion = promotion;
    debugMove.mover = x;
    debugMove.captured = y;
    debugMove.enPassantSquare = playMove(b, &debugMove);
    
    // Check to make sure the full board values after this move match the previous values plus changes.
    eval whiteValue = 0;
    eval blackValue = 0;
    evalFullBoardDebug(b, &whiteValue, &blackValue);
    if (whiteValue != whiteNew || blackValue != blackNew) {
        printf("DEBUG: White value was %i but was calculated as %i + %i = %i.\n", whiteValue, t->whiteValue, t->whiteValueChange, whiteNew);
        printf("DEBUG: Black value was %i but was calculated as %i + %i = %i.\n", blackValue, t->blackValue, t->blackValueChange, blackNew);
        drawBoard(b, WHITE);
        exit(1);
    }

    // Undo this move.
    undoMove(b, &debugMove);

    // Check to make sure the board state matches the original state.
    for (int i = 0; i < 64; i++) {
        if (b[i] != debugBoard[i]) {
            printf("DEBUG: Full board eval: B[%i] is %i and old B[%i] is %i.\n", i, b[i], i, debugBoard[i]);
        }
    }
#endif

    // Compute the new eval.
    return finalEval(whiteNew, blackNew);
}

// Execute an already known to be semilegal move while calculating, adding a new move to the childPool.
void examineMove(T* t, char moveFrom, char moveTo) {
    position* pos = &(t->pos);
    char* b = pos->board;

    // Determine the player making the move.
    char playerTurn = b[moveFrom] >= bPAWN && b[moveFrom] <= bKING ? BLACK : WHITE;

    char trueMoveto = moveTo;
    char promotion = -1;
    if (trueMoveto >= 96) {
        trueMoveto %= 8;
        promotion = (moveTo / 8) - 5;
    } else if (trueMoveto >= 64) {
        trueMoveto = 56 + (trueMoveto % 8);
        promotion = (moveTo / 8) - 7;
    }

    #if CHECK_FOR_CHECK

        // Create the move.
        M move;
        move.f = moveFrom;
        move.tt = trueMoveto;
        move.promotion = promotion;
        move.mover = b[moveFrom];
        move.captured = b[trueMoveto];

        // Play the move.
        move.enPassantSquare = playMove(b, &move);
        
        // Get the new king square.
        char kingSquare = 0;
        
        if (playerTurn == BLACK) {
            if (b[trueMoveto] == bKING) {
                kingSquare = trueMoveto;
            }
            else {
                kingSquare = pos->bKING_SQUARE;
            }
        }else{
            if (b[trueMoveto] == wKING) {
                kingSquare = trueMoveto;
            }
            else {
                kingSquare = pos->wKING_SQUARE;
            }
        }

        // Make sure we have not just moved into check.
        bool inCheck = !kingNotInCheck(b, kingSquare);
        undoMove(b, &move);
        if(inCheck) return;

    #endif
    
    eval e = 0;

    // If moving to other king, we define this to be a guaranteed checkmate.
    if(playerTurn == BLACK) {
        if (b[trueMoveto] == wKING) {
            e = BLACK_WINS_EVAL;
        }
        else {
            // Evaluate what the position would be after moving.
            // If Black is making the moves, add TEMPO_EVAL to compensate and account for White now having the tempo.
            e = computeEvalMove(t, moveFrom, trueMoveto, promotion) + TEMPO_EVAL;
        }

        if (e < t->bestChildEval) t->bestChildEval = e;
    }
    else {
        if (b[trueMoveto] == bKING) {
            e = WHITE_WINS_EVAL;
        }
        else {
            // Evaluate what the position would be after moving.
            // If White is making the moves, subtract TEMPO_EVAL to compensate and account for Black now having the tempo.
            e = computeEvalMove(t, moveFrom, trueMoveto, promotion) - TEMPO_EVAL;
        }

        if (e > t->bestChildEval) t->bestChildEval = e;
    }

    // Add this move.
    int l = t->childPoolLength;
    (t->childFroms)[l] = moveFrom;
    (t->childTos)[l] = moveTo;
    (t->childEvals)[l] = e;
    (t->childPoolLength)++;
}

// Make all legal moves for a white pawn.
inline void examineWhitePawn(T* t, char x, char epf) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    if (r == 6) {
        ifEmpty(56 + c) { // promoting move
            mv(64 + c);
            mv(72 + c);
            mv(80 + c);
            mv(88 + c);
        }
        if (c > 0) { // promoting capture left
            ifBlack(55 + c) {
                mv(63 + c);
                mv(71 + c);
                mv(79 + c);
                mv(87 + c);
            }
        }
        if (c < 7) { // promoting capture right
            ifBlack(56 + c) {
                mv(65 + c);
                mv(73 + c);
                mv(81 + c);
                mv(89 + c);
            }
        }
    }
    else if (r < 6) {
        ifEmpty(x + 8) { // move
            mv(x + 8);
            if (r == 1) { // move two squares
                ifEmpty(x + 16) mv(x + 16);
            }
        }
        if (c > 0) { // capture or en passant left
            ifBlack(x + 7) {
                mv(x + 7);
            } else if(epf == c - 1 && r == 4) {
                mv(x + 7);
            }
        }
        if (c < 7) { // capture or en passant right
            ifBlack(x + 9) {
                mv(x + 9);
            } else if (epf == c + 1 && r == 4) {
                mv(x + 9);
            }
        }
    }
}

// Make all legal moves for a black pawn.
inline void examineBlackPawn(T* t, char x, char epf) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;
    
    if (r == 1) {
        ifEmpty(0 + c) { // promoting move
            mv(96 + c);
            mv(104 + c);
            mv(112 + c);
            mv(120 + c);
        }
        if (c > 0) { // promoting capture left
            ifWhite(-1 + c) {
                mv(95 + c);
                mv(103 + c);
                mv(111 + c);
                mv(119 + c);
            }
        }
        if (c < 7) { // promoting capture right
            ifWhite(1 + c) {
                mv(97 + c);
                mv(105 + c);
                mv(113 + c);
                mv(121 + c);
            }
        }
    }
    else if (r > 1) {
        ifEmpty(x - 8) { // move
            mv(x - 8);
            if (r == 6) { // move two squares
                ifEmpty(x - 16) mv(x - 16);
            }
        }
        if (c > 0) { // capture or en passant left
            ifWhite(x - 9) {
                mv(x - 9);
            } else if (epf == c - 1 && r == 3) {
                mv(x - 9);
            }
        }
        if (c < 7) { // capture or en passant right
            ifWhite(x - 7) {
                mv(x - 7);
            } else if (epf == c + 1 && r == 3) {
                mv(x - 7);
            }
        }
    }
}

// Make all legal moves for a white knight.
inline void examineWhiteKnight(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    if (r > 0) {
        if (c > 1) {
            ifNonWhite(x - 10) {
                mv(x - 10);
            }
        }
        if (c < 6) {
            ifNonWhite(x - 6) {
                mv(x - 6);
            }
        }
    }
    if (r < 7) {
        if (c > 1) {
            ifNonWhite(x + 6) {
                mv(x + 6);
            }
        }
        if (c < 6) {
            ifNonWhite(x + 10) {
                mv(x + 10);
            }
        }
    }
    if (r > 1) {
        if (c > 0) {
            ifNonWhite(x - 17) {
                mv(x - 17);
            }
        }
        if (c < 7) {
            ifNonWhite(x - 15) {
                mv(x - 15);
            }
        }
    }
    if (r < 6) {
        if (c > 0) {
            ifNonWhite(x + 15) {
                mv(x + 15);
            }
        }
        if (c < 7) {
            ifNonWhite(x + 17) {
                mv(x + 17);
            }
        }
    }
}

// Make all legal moves for a black knight.
inline void examineBlackKnight(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    if (r > 0) {
        if (c > 1) {
            ifNonBlack(x - 10) {
                mv(x - 10);
            }
        }
        if (c < 6) {
            ifNonBlack(x - 6) {
                mv(x - 6);
            }
        }
    }
    if (r < 7) {
        if (c > 1) {
            ifNonBlack(x + 6) {
                mv(x + 6);
            }
        }
        if (c < 6) {
            ifNonBlack(x + 10) {
                mv(x + 10);
            }
        }
    }
    if (r > 1) {
        if (c > 0) {
            ifNonBlack(x - 17) {
                mv(x - 17);
            }
        }
        if (c < 7) {
            ifNonBlack(x - 15) {
                mv(x - 15);
            }
        }
    }
    if (r < 6) {
        if (c > 0) {
            ifNonBlack(x + 15) {
                mv(x + 15);
            }
        }
        if (c < 7) {
            ifNonBlack(x + 17) {
                mv(x + 17);
            }
        }
    }
}

// Make all legal moves for a white bishop.
inline void examineWhiteBishop(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    char l = r < c ? r : c;
    l = x - 9 * l;
    for (char X = x - 9; X >= l; X -= 9) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x - 7 * l;
    for (char X = x - 7; X >= l; X -= 7) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
    r = 7 - r;
    l = r < c ? r : c;
    l = x + 9 * l;
    for (char X = x + 9; X <= l; X += 9) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x + 7 * l;
    for (char X = x + 7; X <= l; X += 7) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
}

// Make all legal moves for a black bishop.
inline void examineBlackBishop(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    char l = r < c ? r : c;
    l = x - 9 * l;
    for (char X = x - 9; X >= l; X -= 9) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x - 7 * l;
    for (char X = x - 7; X >= l; X -= 7) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
    r = 7 - r;
    l = r < c ? r : c;
    l = x + 9 * l;
    for (char X = x + 9; X <= l; X += 9) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
    c = 7 - c;
    l = r < c ? r : c;
    l = x + 7 * l;
    for (char X = x + 7; X <= l; X += 7) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
}

// Make all legal moves for a white rook.
inline void examineWhiteRook(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    for (char X = x - 8; X >= 0; X -= 8) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
    for (char X = x + 8; X < 64; X += 8) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
    char l = r * 8;
    for (char X = x - 1; X >= l; X--) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
    l += 8;
    for (char X = x + 1; X < l; X++) {
        ifWhite(X) break;
        mv(X);
        ifBlack(X) break;
    }
}

// Make all legal moves for a black rook.
inline void examineBlackRook(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    for (char X = x - 8; X >= 0; X -= 8) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
    for (char X = x + 8; X < 64; X += 8) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
    char l = r * 8;
    for (char X = x - 1; X >= l; X--) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
    l += 8;
    for (char X = x + 1; X < l; X++) {
        ifBlack(X) break;
        mv(X);
        ifWhite(X) break;
    }
}

// Make all legal moves for a white queen.
inline void examineWhiteQueen(T* t, char x) {
    examineWhiteBishop(t, x);
    examineWhiteRook(t, x);
}

// Make all legal moves for a black queen.
inline void examineBlackQueen(T* t, char x) {
    examineBlackBishop(t, x);
    examineBlackRook(t, x);
}

// Make all legal moves for a white king.
inline void examineWhiteKing(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    if (r > 0) {
        ifNonWhite(x - 8) {
            mv(x - 8);
        }
        if (c > 0) {
            ifNonWhite(x - 9) {
                mv(x - 9);
            }
        }
        if (c < 7) {
            ifNonWhite(x - 7) {
                mv(x - 7);
            }
        }
    }
    if (r < 7) {
        ifNonWhite(x + 8) {
            mv(x + 8);
        }
        if (c > 0) {
            ifNonWhite(x + 7) {
                mv(x + 7);
            }
        }
        if (c < 7) {
            ifNonWhite(x + 9) {
                mv(x + 9);
            }
        }
    }
    if (c > 0) {
        ifNonWhite(x - 1) {
            mv(x - 1);
        }
    }
    if (c < 7) {
        ifNonWhite(x + 1) {
            mv(x + 1);
        }
    }
}

// Make all legal kingside castle moves for a white king.
inline void examineWK(T* t, char x) {
    position* pos = &(t->pos);
    char* b = pos->board;

    if (x == 4 && b[5] == EMPTY && b[6] == EMPTY && b[7] == wROOK) {
        if (kingNotInCheck(b, 4)) {
            bool flag = 0;
            b[4] = EMPTY;
            b[5] = wKING;
            if (kingNotInCheck(b, 5)) {
                b[5] = EMPTY;
                b[6] = wKING;
                flag = kingNotInCheck(b, 6); // optionally check moving into check before executing
            }
            b[4] = wKING;
            b[5] = EMPTY;
            b[6] = EMPTY;
            if (flag) mv(6);
        }
    }
}

// Make all legal queenside castle moves for a white king.
inline void examineWQ(T* t, char x) {
    position* pos = &(t->pos);
    char* b = pos->board;

    if (x == 4 && b[3] == EMPTY && b[2] == EMPTY && b[1] == EMPTY && b[0] == wROOK) {
        if (kingNotInCheck(b, 4)) {
            bool flag = 0;
            b[4] = EMPTY;
            b[3] = wKING;
            if (kingNotInCheck(b, 3)) {
                b[3] = EMPTY;
                b[2] = wKING;
                flag = kingNotInCheck(b, 2); // optionally check moving into check before executing
            }
            b[4] = wKING;
            b[3] = EMPTY;
            b[2] = EMPTY;
            if (flag) mv(2);
        }
    }
}

// Make all legal moves for a black king.
inline void examineBlackKing(T* t, char x) {
    char r = x / 8, c = x % 8;
    position* pos = &(t->pos);
    char* b = pos->board;

    if (r > 0) {
        ifNonBlack(x - 8) {
            mv(x - 8);
        }
        if (c > 0) {
            ifNonBlack(x - 9) {
                mv(x - 9);
            }
        }
        if (c < 7) {
            ifNonBlack(x - 7) {
                mv(x - 7);
            }
        }
    }
    if (r < 7) {
        ifNonBlack(x + 8) {
            mv(x + 8);
        }
        if (c > 0) {
            ifNonBlack(x + 7) {
                mv(x + 7);
            }
        }
        if (c < 7) {
            ifNonBlack(x + 9) {
                mv(x + 9);
            }
        }
    }
    if (c > 0) {
        ifNonBlack(x - 1) {
            mv(x - 1);
        }
    }
    if (c < 7) {
        ifNonBlack(x + 1) {
            mv(x + 1);
        }
    }
}

// Make all legal kingside castle moves for a black king.
inline void examineBK(T* t, char x) {
    position* pos = &(t->pos);
    char* b = pos->board;

    if (x == 60 && b[61] == EMPTY && b[62] == EMPTY && b[63] == bROOK) {
        if (kingNotInCheck(b, 60)) {
            bool flag = 0;
            b[60] = EMPTY;
            b[61] = bKING;
            if (kingNotInCheck(b, 61)) {
                b[61] = EMPTY;
                b[62] = bKING;
                flag = kingNotInCheck(b, 62); // optionally check moving into check before executing
            }
            b[60] = bKING;
            b[61] = EMPTY;
            b[62] = EMPTY;
            if(flag) mv(62);
        }
    }
}

// Make all legal queenside castle moves for a black king.
inline void examineBQ(T* t, char x) {
    position* pos = &(t->pos);
    char* b = pos->board;

    if (x == 60 && b[59] == EMPTY && b[58] == EMPTY && b[57] == EMPTY && b[56] == bROOK) {
        if (kingNotInCheck(b, 60)) {
            bool flag = 0;
            b[60] = EMPTY;
            b[59] = bKING;
            if (kingNotInCheck(b, 59)) {
                b[59] = EMPTY;
                b[58] = bKING;
                flag = kingNotInCheck(b, 58); // optionally check moving into check before executing
            }
            b[60] = bKING;
            b[59] = EMPTY;
            b[58] = EMPTY;
            if (flag) mv(58);
        }
    }
}

// If e is the eval of a checkmate, return the eval of a mate in one, etc.
inline eval evalForcedMateDelay(eval e) {
    if (e >= WHITE_WINS_EVAL_THRESHOLD) {
        return e - EVAL_FORCED_MATE_INCREMENT;
    }
    if (e <= BLACK_WINS_EVAL_THRESHOLD) {
        return e + EVAL_FORCED_MATE_INCREMENT;
    }
    return e;
}

// Make all legal moves for all pieces that can move.
inline void examinePieces(T* t) {
    position* pos = &(t->pos);
    char* b = pos->board;
    char playerTurn = pos->PLAYER_TURN;

    // Clear the childPool so we can find all children.
    t->childPoolLength = 0;
    t->bestChildEval = playerTurn == BLACK ? WHITE_WINS_EVAL : BLACK_WINS_EVAL;

    // Setup the evaluation data.
    t->numEvalPieces = 0;
    for (char x = 0; x < 64; x++) {
        char y = b[x];
        if (y != EMPTY) {
            t->evalPieces[t->numEvalPieces] = y;
            t->evalSquares[t->numEvalPieces++] = x;
        }
    }

    // Calculate the full board t->whiteValue and t->blackValue.
    evalFullBoard(t);

    if (playerTurn == WHITE) {
        for (char x = 0; x < 64; x++) {
            char y = b[x];
            if (y != EMPTY) {
                if (y == wPAWN) {
                    examineWhitePawn(t, x, pos->EN_PASSANT_FILE); continue;
                }
                else if (y == wKNIGHT) {
                    examineWhiteKnight(t, x); continue;
                }
                else if (y == wBISHOP) {
                    examineWhiteBishop(t, x); continue;
                }
                else if (y == wROOK) {
                    examineWhiteRook(t, x); continue;
                }
                else if (y == wQUEEN) {
                    examineWhiteQueen(t, x); continue;
                }
                else if (y == wKING) {
                    examineWhiteKing(t, x);
                    if (pos->wKINGSIDE_CASTLE) examineWK(t, x);
                    if (pos->wQUEENSIDE_CASTLE) examineWQ(t, x);
                    continue;
                }
            }
        }
    }
    else {
        for (char x = 0; x < 64; x++) {
            char y = b[x];
            if (y != EMPTY) {
                if (y == bPAWN) {
                    examineBlackPawn(t, x, pos->EN_PASSANT_FILE); continue;
                }
                else if (y == bKNIGHT) {
                    examineBlackKnight(t, x); continue;
                }
                else if (y == bBISHOP) {
                    examineBlackBishop(t, x); continue;
                }
                else if (y == bROOK) {
                    examineBlackRook(t, x); continue;
                }
                else if (y == bQUEEN) {
                    examineBlackQueen(t, x); continue;
                }
                else if (y == bKING) {
                    examineBlackKing(t, x);
                    if (pos->bKINGSIDE_CASTLE) examineBK(t, x);
                    if (pos->bQUEENSIDE_CASTLE) examineBQ(t, x);
                    continue;
                }
            }
        }
    }
}

// Called after reaching a leaf node.
// Find, evaluate, and store all moves from the given position and node.
inline void examineAllSemilegalMoves(T* t, node* n, int depth) {
    position* pos = &(t->pos);
    char* b = pos->board;
    char playerTurn = pos->PLAYER_TURN;

    W* w = currentWeights;

    examinePieces(t);

    char* froms = t->childFroms;
    char* tos = t->childTos;
    eval* evals = t->childEvals;
    eval bestEval = t->bestChildEval;

    // If there are no legal moves, mark this node as checkmate or stalemate.
    if (t->childPoolLength == 0) {
        char kingSquare = playerTurn == BLACK ? pos->bKING_SQUARE : pos->wKING_SQUARE;

        if (kingNotInCheck(b, kingSquare)) {
            n->e = DRAW_EVAL;
            calcNumStalematesFound.fetch_add(1);
        }
        else if (playerTurn == BLACK) {
            n->e = WHITE_WINS_EVAL;
            calcNumWhiteWinsFound.fetch_add(1);
        }
        else {
            n->e = BLACK_WINS_EVAL;
            calcNumBlackWinsFound.fetch_add(1);
        }

        return;
    }

    // Handle checkmates by capturing the king that were just found by calling this node a checkmate.
    if (bestEval >= WHITE_WINS_EVAL_THRESHOLD) {
        n->e = WHITE_WINS_EVAL;
        calcNumWhiteWinsFound.fetch_add(1);
    }
    else if (bestEval <= BLACK_WINS_EVAL_THRESHOLD) {
        n->e = BLACK_WINS_EVAL;
        calcNumBlackWinsFound.fetch_add(1);
    }
    else {
        n->e = bestEval;
        calcNumNormalsFound.fetch_add(1);
    }
}

// Get the square for a square index in human-readable format.
char* squareToString(char x) {

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

// Get a string for a root move in human-readable format.
char* moveToString(int i, position* pos) {
    node* n = sortedMoves[i];
    char* b = pos->board;
    char f = n->moveFrom;
    char t = n->moveTo;
    char p = b[f];

    char* o;

    if (p == wKING && f == 4 && t == 6 || p == bKING && f == 60 && t == 62) {
        o = (char*)calloc(4, 1);
        o[0] = '0';
        o[1] = '-';
        o[2] = '0';
        o[3] = '\0';
        return o;
    }
    if (p == wKING && f == 4 && t == 2 || p == bKING && f == 60 && t == 58) {
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
    if (p != wPAWN && p != bPAWN) {
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
    switch (p) {
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

void printEval(eval e) {
    int x = (int)e;

    if (x >= WHITE_WINS_EVAL_THRESHOLD) {
        printf("+M%i", WHITE_WINS_EVAL - x);
    }
    else if (x <= BLACK_WINS_EVAL_THRESHOLD) {
        printf("-M%i", x - BLACK_WINS_EVAL);
    }
    else if (x > 0) {
        if (usePlusesOnEvalNumbers) {
            printf("+");
        }
        printf("%i", x / 100);
        printf(".%i%i", (x / 10) % 10, x % 10);
    }
    else {
        printf("-");
        printf("%i", -x / 100);
        printf(".%i%i", (-x / 10) % 10, -x % 10);
    }
}

void printTreeHelper(node* n, char turn, int depth, int depthLimit, int maxMoves) {
    if (depth > depthLimit) return;

    for (int i = 0; i < depth; i++) {
        printf("\t");
    }
    int nc = n->numChildren;
    
    printf(squareToString(n->moveFrom));
    printf(squareToString(n->moveTo));
    printf(": ");
    printEval(n->e);
    printf(" / %i [%i]\n", n->cost, nc);

    node* children[LEGAL_MOVES_UPPER_BOUND];
    for (int i = 0; i < nc; i++) {
        children[i] = nodes + n->childStartIndex + i;
    }

    // Sort the moves by eval.
    for (int i = 0; i < nc; i++) {
        for (int j = i + 1; j < nc; j++) {
            int d = (int)children[i]->e - (int)children[j]->e;
            if ((turn == BLACK && d > 0) || (turn == WHITE && d < 0)) {
                node* temp = children[i];
                children[i] = children[j];
                children[j] = temp;
            }
        }
    }

    if (nc > maxMoves) nc = maxMoves;
    for (int i = 0; i < nc; i++) {
        printTreeHelper(children[i], 1 - turn, depth + 1, depthLimit, maxMoves);
    }
}

void printTree(node* n, int depthLimit, int maxMoves) {
    printTreeHelper(n, rootPosition.PLAYER_TURN, 0, depthLimit, maxMoves);
}

// Update node costs of a path.
inline void updateCosts(int* nodePath, int start) {
    char rootTurn = rootPosition.PLAYER_TURN;

    for (int i = start; i >= 0; i--) {
        int index = nodePath[i];
        node* curr = nodes + index;
        char turn = (rootTurn + i) % 2;
        int turnMultiplier = turn == BLACK ? -1 : 1;

        // Set this node's cost to the maximum of child cost plus eval loss plus a depth loss.
        int nc = curr->numChildren;

        int minCost = MAX_COST; // Cost will be maximum if there are no moves, preventing this node from being examined again.
        for (int j = 0; j < nc; j++) {
            node* c = nodes + curr->childStartIndex + j;
            // If it is White's turn, add how much the eval decreases from curr to c.
            // If it is Black's turn, add how much the eval increases from curr to c.
            int cost = (int)c->cost + ((int)curr->e - (int)c->e) * turnMultiplier;
            if (cost < minCost) {
                minCost = cost;
            }
        }

        int depthCost = 2048 >> i;
        minCost += depthCost;

        curr->cost = minCost >= MAX_COST ? MAX_COST : minCost;
    }
}

// Backtrack up the tree, keeping the eval of every node in the tree perfectly up-to-date.
// Also update node costs.
inline void evalBacktrack(int* nodePath, int nodePathLength) {
    char rootTurn = rootPosition.PLAYER_TURN;

    // Update evals of the changing nodes in nodePath.
    for (int i = nodePathLength - 2; i >= 0; i--) {
        int index = nodePath[i];
        node* curr = nodes + index;
        eval oldEval = curr->e;

        char turn = (rootTurn + i) % 2;
        if (turn == BLACK) {

            // Set the parent's eval to be the best (minimum considering it's Black's turn) of the child evals.
            int nc = curr->numChildren;
            node* c = nodes + curr->childStartIndex;
            eval e = evalForcedMateDelay(c->e);
            for (int j = 1; j < nc; j++) {
                c++;
                eval childEval = evalForcedMateDelay(c->e);
                if (childEval < e) e = childEval;
            }

            // If the parent's eval did not change, there is no reason to keep going with eval or score changes.
            if (oldEval == e) {
                break;
            }

            curr->e = e;
        }
        else {

            // Set the parent's eval to be the best (maximum considering it's White's turn) of the child evals.
            int nc = curr->numChildren;
            node* c = nodes + curr->childStartIndex;
            eval e = evalForcedMateDelay(c->e);
            for (int j = 1; j < nc; j++) {
                c++;
                eval childEval = evalForcedMateDelay(c->e);
                if (childEval > e) e = childEval;
            }

            // If the parent's eval did not change, there is no reason to keep going.
            if (oldEval == e) {
                break;
            }

            curr->e = e;
        }
    }

    // Update costs of all nodes in the path.
    updateCosts(nodePath, nodePathLength - 1);
}

// Examine the given leaf node with the given thread.
// Create a new child node for each move.
// Update evals and other data.
void examineLeaf(T* t) {
    calcNumNodesExamined.fetch_add(1);
    int* path = t->nodePathIndices;

    // Get the root position in t's pos field.
    position* pos = &(t->pos);
    *pos = rootPosition;

    // Make the moves in the path to get to the position at the end of the path.
    int pathLength = t->nodePathLength;
    for (int i = 1; i < pathLength; i++) {
        node* x = nodes + path[i];
        pos->SQUARE_FROM = x->moveFrom;
        pos->SQUARE_TO = x->moveTo;
        playMoveUpdating(pos);
    }

    pos->PLAYER_TURN = (rootPosition.PLAYER_TURN + pathLength + 1) % 2;

    node* leaf = nodes + path[pathLength - 1];
    examineAllSemilegalMoves(t, leaf, pathLength);

    // If this leaf is at the maximum depth allowed, return without creating child nodes.
    if (pathLength >= evaluationDepthLimit) return;

    // Increment the number of nodes.
    int nc = t->childPoolLength;
    int start = numNodes.fetch_add(nc);
    if (start + nc > nodeCap.load()) {
        numNodes.fetch_add(-nc);
        nodeCapReached.store(1);
        return;
    }
    calcNumNodesAdded.fetch_add(nc);

    // Create the child nodes.
    leaf->numChildren = nc;
    leaf->childStartIndex = start;

    for (int i = 0; i < nc; i++) {
        node* newN = nodes + start + i;

        // Set the new node data.
        newN->moveFrom = t->childFroms[i];
        newN->moveTo = t->childTos[i];
        newN->numChildren = 0;
        newN->childStartIndex = UNDEFINED;
        newN->e = t->childEvals[i];
        newN->cost = 0;
    }
}

// Find the best leaf to examine and record its path in the main path or a thread's path.
void findBestLeaf(int* pathIndices, int* pathLength, int index) {
    char rootTurn = rootPosition.PLAYER_TURN;
    char turn = (rootTurn + *pathLength) % 2;
    int turnMultiplier = turn == BLACK ? -1 : 1;

    pathIndices[(*pathLength)++] = index;
    node* curr = nodes + index;

    int nc = curr->numChildren;
    if (nc == 0) {
        return;
    }

    // Expand upon the current path by finding the minimum cost among children.
    int minCost = MAX_COST, minCostIndex = 0;
    for (int i = 0; i < nc; i++) {
        node* c = nodes + curr->childStartIndex + i;
        
        // If it is White's turn, add how much the eval decreases from curr to c.
        // If it is Black's turn, add how much the eval increases from curr to c.
        int cost = (int)c->cost + ((int)curr->e - (int)c->e) * turnMultiplier;
        if (cost < minCost) {
            minCost = cost;
            minCostIndex = i;
        }
    }

    findBestLeaf(pathIndices, pathLength, curr->childStartIndex + minCostIndex);
}

// Copy and sort the choices of moves from the root node. Root must be created (nodes != 0) before calling this.
void getSortedChoices() {
    int numChoices = nodes->numChildren;

    int c = nodes->childStartIndex; // should be 1
    for (int i = 0; i < numChoices; i++) {
        sortedMoves[i] = nodes + c + i;
    }

    bool playerTurn = rootPosition.PLAYER_TURN;

    // Sort the choices using insertion sort.
    for (int i = 1; i < numChoices; i++) {

        node* n = sortedMoves[i];
        eval e = n->e;
        int j = i - 1;

        eval je = sortedMoves[j]->e;
        while (((playerTurn) && je > e || (!playerTurn) && je < e)) {
            sortedMoves[j + 1] = sortedMoves[j];
            j--;
            if (j < 0) break;
            je = sortedMoves[j]->e;
        }

        sortedMoves[j + 1] = n;
    }
}

// The search cycle for any thread, including the main thread.
void searchCycle(T* t) {

    unsigned long long s1 = getTime();

    // Claim the tree.
    bool locked = 1;
    while (locked) {
        if (killThreads.load()) {
            numThreadsAlive.fetch_add(-1);
            return;
        }

        locked = treeLock.exchange(1);
    }
#if PERFORMANCE
    unsigned long long t1 = getTime() - s1;
    printf("TIMER 1: %lli\n", t1);
    unsigned long long s2 = getTime();
    printf("NODES: %i %i\n", calcNumNodesAdded.load());
#endif

    // Find all paths waiting to be backtracked.
    for (int i = 0; i < numThreads; i++) {
        T* ti = threads + i;
        if (ti->hasPath.load()) {

            // Backtrack this path non-atomically since paths are not accessed by their threads unless that thread owns the lock.
            // This also sets this path leaf's true cost to enable it to be examined again.
            evalBacktrack(ti->nodePathIndices, ti->nodePathLength);

            ti->hasPath.store(0);
        }
    }

    // If there are no leaves available to be claimed (minimum cost is MAX_COST), unclaim the tree.
    // Then go to the next iteration.
    // We just backtracked every node, but more backtrackable nodes might come.
    // If the tree is complete, all threads will just do this.
    if (nodes->cost == MAX_COST) {
        treeLock.store(0);
        return;
    }

    // Find the new best leaf.
    // If the best leaf has MAX_COST cost, then all nodes have MAX_COST cost and we would stop before doing this.
    t->nodePathLength = 0;
    findBestLeaf(t->nodePathIndices, &(t->nodePathLength), 0);

#if PRINT_PATHS
    printf("(%i) Root cost %i, Path length %i: ", calcNumNodesExamined.load(), nodes->cost, t->nodePathLength);
    for (int i = 1; i < t->nodePathLength; i++) {
        node* pathNode = nodes + t->nodePathIndices[i];
        printf(squareToString(pathNode->moveFrom));
        printf(squareToString(pathNode->moveTo));
        printf(" ");
    }
    printf("\n");
#endif

    // Set its cost to maximum and update costs.
    (nodes + t->nodePathIndices[t->nodePathLength - 1])->cost = MAX_COST;
    updateCosts(t->nodePathIndices, t->nodePathLength - 2);

    // Unclaim the tree.
    treeLock.store(0);

    // Examine the leaf.
    examineLeaf(t);

    // Tell other threads to backtrack this leaf.
    t->hasPath.store(1);
#if PERFORMANCE
    unsigned long long t2 = getTime() - s2;
    printf("TIMER 2: %lli\n", t2);
    unsigned long long total = t1 + t2;
    printf("TIMER ALL: %lli\n", total);
#endif
}

// Reset the statistics for this thread.
void resetCalcStats() {

    calcNumWhiteWinsFound.store(0);
    calcNumBlackWinsFound.store(0);
    calcNumStalematesFound.store(0);
    calcNumNormalsFound.store(0);

    calcNumNodesAdded.store(0);
    calcNumNodesExamined.store(0);
    
    int size = evaluationDepthLimit * 4;

    for (int i = 0; i < evaluationDepthLimit; i++) {
        calcNumNodesAddedDepth[i].store(0);
        calcNumNodesExaminedDepth[i].store(0);
    }
}

// Return true if a white pawn move follows all white pawn rules.
bool isValidWhitePawnMove(char* b, char f, char t, char epf) {
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
            else if (b[t] == -1 && epf == ct && rf == 4) {
                return 1; // en passant
            }
        }
        else if (cf > 0 && t == f + 7) { // capturing left
            if (b[t] >= 6 && b[t] <= 11) {
                return 1;
            }
            else if (b[t] == -1 && epf == ct && rf == 4) {
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
bool isValidBlackPawnMove(char* b, char f, char t, char epf) {
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
            else if (b[t] == -1 && epf == ct && rf == 3) {
                return 1; // en passant
            }
        }
        else if (cf > 0 && t == f - 9) { // capturing left
            if (b[t] >= 0 && b[t] <= 5) {
                return 1;
            }
            else if (b[t] == -1 && epf == ct && rf == 3) {
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
bool isValidKnightMove(char f, char t) {
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
bool isValidBishopMove(char* b, char f, char t) {
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
bool isValidRookMove(char* b, char f, char t) {
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
bool isValidQueenMove(char* b, char f, char t) {
    return isValidBishopMove(b, f, t) || isValidRookMove(b, f, t);
}

// Return true if a white kingside castle follows all castle rules.
bool isValidWKMove(char* b, char f, char t) {
    if (f == 4 && t == 6 && b[5] == EMPTY && b[6] == EMPTY) {
        // No need to check king and rook positions since moving them turns off castling ability.

        if (kingNotInCheck(b, 4)) {
            b[4] = -1;
            b[5] = 5;
            if (kingNotInCheck(b, 5)) {
                b[5] = -1;
                b[6] = 5;
                if (kingNotInCheck(b, 6)) {
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
bool isValidWQMove(char* b, char f, char t) {
    if (f == 4 && t == 2 && b[3] == EMPTY && b[2] == EMPTY) {
        // No need to check king and rook positions since moving them turns off castling ability.

        if (kingNotInCheck(b, 4)) {
            b[4] = -1;
            b[3] = 5;
            if (kingNotInCheck(b, 3)) {
                b[3] = -1;
                b[2] = 5;
                if (kingNotInCheck(b, 2)) {
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
bool isValidBKMove(char* b, char f, char t) {
    if (f == 60 && t == 62 && b[61] == EMPTY && b[62] == EMPTY) {
        // No need to check king and rook positions since moving them turns off castling ability.

        if (kingNotInCheck(b, 60)) {
            b[60] = -1;
            b[61] = 11;
            if (kingNotInCheck(b, 61)) {
                b[61] = -1;
                b[62] = 11;
                if (kingNotInCheck(b, 62)) {
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
bool isValidBQMove(char* b, char f, char t) {
    if (f == 60 && t == 58 && b[59] == EMPTY && b[58] == EMPTY) {
        // No need to check king and rook positions since moving them turns off castling ability.

        if (kingNotInCheck(b, 60)) {
            b[60] = -1;
            b[59] = 11;
            if (kingNotInCheck(b, 59)) {
                b[59] = -1;
                b[58] = 11;
                if (kingNotInCheck(b, 58)) {
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

// Return true if a non-castle king move follows all king rules.
bool isValidKingMove(char f, char t) {
    char rf = f / 8, cf = f % 8, rt = t / 8, ct = t % 8;
    char rd = rt - rf, cd = ct - cf;

    return rd >= -1 && rd <= 1 && cd >= -1 && cd <= 1;
}


// Return true if a move follows the piece moving rules.
bool isSemilegalMove(position* pos, char moveFrom, char moveTo) {
    char* b = pos->board;
    char p = b[moveFrom];
    char q = b[moveTo];

    switch (p) {
    case 0:
        return isValidWhitePawnMove(b, moveFrom, moveTo, pos->EN_PASSANT_FILE);
    case 6:
        return isValidBlackPawnMove(b, moveFrom, moveTo, pos->EN_PASSANT_FILE);
    case 1:
    case 7:
        return isValidKnightMove(moveFrom, moveTo);
    case 2:
    case 8:
        return isValidBishopMove(b, moveFrom, moveTo);
    case 3:
    case 9:
        return isValidRookMove(b, moveFrom, moveTo);
    case 4:
    case 10:
        return isValidQueenMove(b, moveFrom, moveTo);
    case 5:
        return isValidKingMove(moveFrom, moveTo) || (pos->wKINGSIDE_CASTLE && isValidWKMove(b, moveFrom, moveTo)) || (pos->wQUEENSIDE_CASTLE && isValidWQMove(b, moveFrom, moveTo));
    case 11:
        return isValidKingMove(moveFrom, moveTo) || (pos->bKINGSIDE_CASTLE && isValidBKMove(b, moveFrom, moveTo)) || (pos->bQUEENSIDE_CASTLE && isValidBQMove(b, moveFrom, moveTo));
    }

    // If moved piece is not a piece, return 0;
    return 0;
}

// Check if the given move follows the piece moving rules and does not move into check.
// Record the move. Return whether it is legal.
bool isLegalMove(position* pos, char moveFrom, char moveTo) {
    char* b = pos->board;
    char playerTurn = pos->PLAYER_TURN;
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
    if (!isSemilegalMove(pos, moveFrom, moveTo)) {
        return 0;
    }

    // Simulate moving any pieces involved in this move.
    position copy = *pos;

    pos->SQUARE_FROM = moveFrom;
    pos->SQUARE_TO = moveTo;

    playMoveUpdating(&copy);
    
    char kingSquare = playerTurn == BLACK ? copy.bKING_SQUARE : copy.wKING_SQUARE;
    bool notInCheck = kingNotInCheck(copy.board, kingSquare);
    return notInCheck;
}

// Return true if the given state has occurred at least twice previously in the game history.
bool checkThreefoldRepetition() {
    bool count = 0;
    position* pos = history + gameLength - 1;

    // Check every second game state (all previous states with same player's turn as now) for equality.
    for (int i = gameLength - 3; i >= 0; i -= 2) {

        // Check all board squares for equality.
        if (history[gameLength - 1].board == history[i].board) {
            // Check only castling and en passant states.
            position* pi = history + i;
            if (pi->wKINGSIDE_CASTLE != pos->wKINGSIDE_CASTLE) continue;
            if (pi->wQUEENSIDE_CASTLE != pos->wQUEENSIDE_CASTLE) continue;
            if (pi->bKINGSIDE_CASTLE != pos->bKINGSIDE_CASTLE) continue;
            if (pi->bQUEENSIDE_CASTLE != pos->bQUEENSIDE_CASTLE) continue;
            if (pi->EN_PASSANT_FILE != pos->EN_PASSANT_FILE) continue;

            if (count) return 1;
            count = 1;
        }
    }
    return 0;
}

// Fill the given weights with zeroes.
void fillWeights0s(W* w) {
    for (int i = 0; i < NUM_PIECES; i++) {
        for (int j = 0; j < 64; j++) {
            for (int k = 0; k < NUM_PIECES; k++) {
                for (int l = 0; l < 64; l++) {
                    w->weight[i][j][k][l] = 0;
                }
            }
        }
    }
}

// Write the given weights to the given file.
void writeWeights(W* w, const char* path) {
    eval* data = (eval*)fileLine;

    // Fill the file with the given weights.
    int p = 0;
    for (int i = 0; i < NUM_PIECES; i++) {
        for (int j = 0; j < 64; j++) {
            for (int k = 0; k < NUM_PIECES; k++) {
                for (int l = 0; l < 64; l++) {
                    data[p++] = w->weight[i][j][k][l];
                }
            }
        }
    }

    FILE* fp;
    fopen_s(&fp, path, "wb");
    if (fp == NULL) {
        printf("Failed to open %s.\n", path);
        exit(1);
    }
    fwrite(fileLine, 1, p * 2, fp);
    fclose(fp);
}

// Read the given weights to the given file.
void readWeights(W* w, const char* path) {
    FILE* fp;
    fopen_s(&fp, path, "rb");
    if (fp == NULL) {
        printf("Failed to open %s.\n", path);
        exit(1);
    }
    fread(fileLine, 1, MAX_FILE_SIZE, fp);
    fclose(fp);

    // Set all values to 0.0 in case some do not get filled.
    fillWeights0s(w);

    double x = 0.0;
    eval* data = (eval*)fileLine;

    // Fill the given weights with the file values.
    int p = 0;
    for (int i = 0; i < NUM_PIECES; i++) {
        for (int j = 0; j < 64; j++) {
            for (int k = 0; k < NUM_PIECES; k++) {
                for (int l = 0; l < 64; l++) {
                    w->weight[i][j][k][l] = data[p++];
                }
            }
        }
    }
}

// Read the data from savedData or choose weights and fill bestWeights.
void setupWeights(bool useSavedData) {
    W* w = &bestWeights;
    currentWeights = &bestWeights;

    if (useSavedData) {
        // Read from savedData.
        readWeights(w, "savedData");
    }
    else {
        // Fill bestWeights with default values.
        for (int i = 0; i < NUM_PIECES; i++) {
            for (int j = 0; j < 64; j++) {
                int r = j / 8, c = j % 8;
                for (int k = 0; k < NUM_PIECES; k++) {
                    for (int l = 0; l < 64; l++) {
                        int weight = 0;

                        if (i == k && j == l) {
                            // Make each piece's self-weight the piece's point value.
                            eval startingWeight = piecePointValues[i];
                            weight = startingWeight;

                            // Change each piece's self-weight by a value depending on the piece type and location.
                            if (i == wPAWN) {
                                weight += 24 * (r - 2);
                                int x = r > 3 ? 7 - r : r;
                                weight += 24 * (x - 1);
                            }
                            else if (i == bPAWN) {
                                weight -= 24 * (5 - r);
                                int x = r > 3 ? 7 - r : r;
                                weight -= 24 * (x - 1);
                            }
                            else {
                                if (i >= wPAWN && i <= wKING) {
                                    int x = r > 3 ? 7 - r : r;
                                    int y = c > 3 ? 7 - c : c;
                                    weight += 10.0 * (double)(x + y - 3);
                                }
                                else {
                                    int x = r > 3 ? 7 - r : r;
                                    int y = c > 3 ? 7 - c : c;
                                    weight += 10.0 * (double)(x + y - 3);
                                }
                            }
                        } else {
                            weight = 0;
                        }

                        if (weight > WHITE_WINS_EVAL) weight = WHITE_WINS_EVAL;
                        if (weight < BLACK_WINS_EVAL) weight = BLACK_WINS_EVAL;
                        w->weight[i][j][k][l] = weight;
                    }
                }
            }
        }
    }
}

// Randomly uniformly generate every weight in the range [minWeight, maxWeight].
void randomizeWeights(W* w, eval minWeight, eval maxWeight) {
    for (int i = 0; i < NUM_PIECES; i++) {
        for (int j = 0; j < 64; j++) {
            for (int k = 0; k < NUM_PIECES; k++) {
                for (int l = 0; l < 64; l++) {
                    w->weight[i][j][k][l] = randomRange(minWeight, maxWeight);
                }
            }
        }
    }
}

// Read a string from console.
void getLine() {

    while (1) {
        for (int i = 0; i < MAX_LINE_SIZE; i++) {
            inLine[i] = 0;
        }

        char* r = fgets(inLine, MAX_LINE_SIZE, stdin);

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
    return inLine[0];
}

// Read and return a non-negative number from console. upperBound can be at most 200 million.
double getNumber(double lowerBound, double upperBound, bool allowDecimal) {
    bool invalid = 1;
    double x = 0.0;
    while (invalid) {
        invalid = 0;
        int i = 0;
        x = 0.0;
        double d = -1.0;
        getLine();

        while (inLine[i] != '\n') {
            char c = inLine[i];

            if (isNumeric(c)) {
                if (x >= 2000000000) {
                    printf("Input number must be less than 2000000000: ");
                    invalid = 1;
                    break;
                }
                if (d == -1.0) {
                    x *= 10.0;
                    x += (double)(inLine[i] - '0');
                }
                else {
                    x += (double)(inLine[i] - '0') * d;
                    d *= 0.1;
                }
            }
            if (c == '.' && allowDecimal && d == -1.0) {
                d = 0.1;
            }

            i++;
            if (i >= MAX_LINE_SIZE) {
                printf("Line is too long: ");
                invalid = 1;
                break;
            }
        }

        if (!invalid) {

            if (x < lowerBound || x > upperBound) {
                if (allowDecimal) {
                    printf("Input decimal must be in the range [%f, %f]: ", lowerBound, upperBound);
                }
                else {
                    printf("Input integer must be in the range [%i, %i]: ", (int)lowerBound, (int)upperBound);
                }

                invalid = 1;
            }
        }
    }

    return x;
}

// Prepare to evaluate a position, making a deep copy of the given position.
bool setupEvaluation(position* pos, W* w, bool multithread) {

    if (!initComplete) return 0;

    currentWeights = w;

    // Clear the tree.
    numNodes.store(0);
    nodeCapReached.store(0);

    // Reset the calc statistics.
    resetCalcStats();

    rootPosition = *pos;

    // Construct the root node (nodes[0]) from the given data.
    calcNumNodesAdded.fetch_add(1);
    numNodes.fetch_add(1);

    nodes->moveFrom = UNDEFINED;
    nodes->moveTo = UNDEFINED;
    nodes->numChildren = 0;
    nodes->childStartIndex = UNDEFINED;
    nodes->e = 0.0; // This will get updated when examining the root.
    nodes->cost = 0;

    // Get all moves from the root.
    // This ensures that if we do not evaluate or evaluation goes wrong, we at least know what the legal moves are.
    threads->nodePathLength = 1;
    threads->nodePathIndices[0] = 0;
    examineLeaf(threads);

    setupComplete = 1;
    return 1;
}

// Function called with a thread until we close the thread (can persist over multiple position evaluations).
void runThread(int id) {
    T* t = threads + id;
    
    // Keep evaluating until init() is called and the threads are killed.
    for (int i = 0;; i++) {

        // Checking if we should evaluate.
        struct timespec now;
        timespec_get(&now, TIME_UTC);
        unsigned long long time = getTime();
        if (time >= endTime) {
            continue;
        }
        if (calcNumNodesAdded.load() >= nodeAddLimit) {
            continue;
        }
        if (calcNumNodesExamined.load() >= nodeExamineLimit) {
            continue;
        }

        if (nodeCapReached) continue;
        searchCycle(t);
    }
}

// End the thread function for each thread.
void killAllThreads() {

    // Ask the threads to stop.
    for (int i = 1; i < numThreads; i++) {
        killThreads.store(1);
    }

    // Wait until all threads have exited their runThread() function.
    while (numThreadsAlive.load() > 0) {}

    // Join all threads.
    for (int i = 1; i < numThreads; i++) {
        if (threads[i].thr.joinable()) {
            threads[i].thr.join();
        }
    }
}

void startEvaluation(int timeLimitMS, int addLimit, int examineLimit) {
    nodeAddLimit = addLimit;
    nodeExamineLimit = examineLimit;
    endTime = getTime() + (unsigned long long)timeLimitMS * 1000000ull;
}

void stopEvaluation() {
    endTime = 0;
}

void waitEvaluation(int timeLimitMS, int addLimit, int examineLimit) {
    if (!setupComplete) {
        return;
    }
    startEvaluation(timeLimitMS, addLimit, examineLimit);
    while (getTime() < endTime) {}
    stopEvaluation();
    getSortedChoices();
}

// Initialize the engine by configuring settings and allocating position memory.
// This must be called at the start of this application and when other apps run this app.
// Can also be called during and between position examinations to change the memory allowed and number of threads.
// totalNumNodesAllowed should be very large (suggested: 20 million) as we use sizeof(node) = 20 bytes per node.
bool init(int totalNumNodesAllowed, int threadCount) {

    // Return early (do not enable initComplete) if bad parameters.
    if (totalNumNodesAllowed < 1000 || totalNumNodesAllowed > 2000000000) return 0;
    if (threadCount < 2 || threadCount > 100) return 0;

    setupComplete = 0;

    killAllThreads();

    numThreads = threadCount;

    // Generate the threads.
    clear(threads);
    threads = (T*)calloc(numThreads, sizeof(T));

    for (int i = 0; i < numThreads; i++) {
        T* t = threads + i;
        t->nodePathLength = 0;
        t->childPoolLength = 0;
        t->hasPath.store(0);
    }

    // Allocate global nodes.
    nodes = (node*)realloc(nodes, totalNumNodesAllowed * sizeof(node));
    numNodes.store(0);
    nodeCap.store(totalNumNodesAllowed);

    // Start all threads.
    killThreads.store(0);
    numThreadsAlive.store(numThreads - 1);
    for (int i = 1; i < numThreads; i++) {
        threads[i].thr = thread(runThread, i);
    }

    initComplete = 1;
    return 1;
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
        for (int i = 0; inLine[i] != '\n'; i++) {
            char c = inLine[i];
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
char getPieceMoving(position* pos, char piece, char t, char row, char col) {
    char* b = pos->board;
    char type = pieceCharToType(piece, pos->PLAYER_TURN);
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
            if (isLegalMove(pos, f, t)) {
                return f;
            }
        }
    }

    return -127;
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
// Make the move and return whether the move is fully legal.
bool parseMove(position* pos, char* s, int l) {
    char* b = pos->board;
    char playerTurn = pos->PLAYER_TURN;
    char* f = &(pos->SQUARE_FROM);
    char* t = &(pos->SQUARE_TO);
    *f = -128; *t = -128;

    switch (l) {
    case 2:

        // Pawn move (e4)
        if (isAH(s[0]) && is18(s[1])) {
            *t = (s[1] - '1') * 8 + s[0] - 'a';
            if (playerTurn) {
                if (*t >= 56) {
                    printf("No black pawn can move to %s: ", squareToString(*t));
                    *f = -126;
                }
                else if (b[*t + 8] == 6) {
                    *f = *t + 8;
                }
                else if (*t / 8 == 4 && b[*t + 16] == 6) {
                    *f = *t + 16;
                }
                else {
                    printf("No black pawn can move to %s: ", squareToString(*t));
                    *f = -126;
                }
            }
            else {
                if (*t < 8) {
                    printf("No white pawn can move to %s: ", squareToString(*t));
                    *f = -126;
                }
                else if (b[*t - 8] == 0) {
                    *f = *t - 8;
                }
                else if (*t / 8 == 3 && b[*t - 16] == 0) {
                    *f = *t - 16;
                }
                else {
                    printf("No white pawn can move to %s: ", squareToString(*t));
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
            *f = getPieceMoving(pos, s[0], *t, -1, -1);
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
                    printf("No black pawn can capture to %s: ", squareToString(*t));
                    *f = -126;
                }
                else if (b[*f] != bPAWN) {
                    printf("No black pawn can capture to %s: ", squareToString(*t));
                    *f = -126;
                }
            }
            else {
                *f = (s[2] - '2') * 8 + s[0] - 'a';
                if (*t >= 56 || *t < 8) {
                    printf("No white pawn can capture to %s: ", squareToString(*t));
                    *f = -126;
                }
                else if (b[*f] != wPAWN) {
                    printf("No white pawn can capture to %s: ", squareToString(*t));
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
                    printf("No black pawn can promote to %s%c: ", squareToString(*t), s[2]);
                    *f = -126;
                }
                else if (b[*f] == bPAWN) {

                }
                else {
                    printf("No black pawn can promote to %s%c: ", squareToString(*t), s[2]);
                    *f = -126;
                }
            }
            else {
                *f = 48 + s[0] - 'a';
                if (s[1] != '8') {
                    printf("No white pawn can promote to %s%c: ", squareToString(*t), s[2]);
                    *f = -126;
                }
                else if (b[*f] == wPAWN) {

                }
                else {
                    printf("No white pawn can promote to %s%c: ", squareToString(*t), s[2]);
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
            *f = getPieceMoving(pos, s[0], *t, s[1] - '1', -1);
        }

        // Piece move with column (Nce4)
        if (isPiece(s[0]) && isAH(s[1]) && isAH(s[2]) && is18(s[3])) {
            *t = (s[3] - '1') * 8 + s[2] - 'a';
            *f = getPieceMoving(pos, s[0], *t, -1, s[1] - 'a');
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
                    printf("No black pawn can capture to %s: ", squareToString(*t));
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
                    printf("No white pawn can capture to %s: ", squareToString(*t));
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
            *f = getPieceMoving(pos, s[0], *t, s[2] - '1', s[1] - 'a');
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
        if (isLegalMove(pos, *f, *t)) {
            return 1;
        }
        else {
            printf("Move from %s to %s is illegal: ", squareToString(*f), squareToString(*t));
            return 0;
        }
    }
    return 0;
}

// Gets a move from the user and records the movefrom and moveto in the given parameter when legal.
// Repeats until the move is fully legal on the given parameters.
bool playerChooseMove(position* pos) {
    char playerTurn = pos->PLAYER_TURN;

    printf("Enter a move for ");
    playerTurn == BLACK ? printf("Black: ") : printf("White: ");

    while (1) {
        getMoveString();

        if (moveStringLength == 0) {
            return 0;
        }

        bool legal = parseMove(pos, moveString, moveStringLength);
        if (legal) {
            return 1;
        }
    }
    return 1;
}

// Choose a move using the evals and difficulty (from DIFFICULTY_MIN to DIFFICULTY_MAX).
// Return the index of the move.
// Return -1 if no moves found.
int chooseMove(eval maxEvalLossAllowed) {

    int numChoices = nodes->numChildren;

    if (evaluationPrintChoices) {
        if (numChoices > 0) {
            printf("%i choices with best eval (current position eval) ", numChoices);
            printEval(sortedMoves[0]->e);
            printf(":\n");
            for (int i = 0; i < numChoices; i++) {
                printf(moveToString(i, history + gameLength - 1));
                printf("\t");
                printEval(sortedMoves[0]->e);
                printf("\n");
            }
        }
        else {
            printf("No move choices found.\n");
        }
    }

    if (numChoices <= 0) return -1;

    // Get the number of good moves to choose from depending on the engine difficulty.
    eval best = sortedMoves[0]->e;
    int numActualChoices = numChoices;
    for (int i = 1; i < numChoices; i++) {
        eval e = sortedMoves[i]->e;
        if ((int)e - (int)best > (int)maxEvalLossAllowed || (int)best - (int)e > (int)maxEvalLossAllowed) {
            numActualChoices = i;
            break;
        }
    }

    // Get the 0-indexed choice.
    return randomRange(0, numActualChoices - 1);
}

// Return the number of legal moves in a position.
// This is done by examining all moves with a copy of the position.
int numMoves(position* p) {
    // Create a copy of the position to examine pieces with.
    // childPool and eval data will get reset by examinePieces, so there is no need to set them here.
    T t;
    for (int i = 0; i < 64; i++) {
        t.pos.board[i] = p->board[i];
    }
    t.pos.wKINGSIDE_CASTLE = p->wKINGSIDE_CASTLE;
    t.pos.wQUEENSIDE_CASTLE = p->wQUEENSIDE_CASTLE;
    t.pos.bKINGSIDE_CASTLE = p->bKINGSIDE_CASTLE;
    t.pos.bQUEENSIDE_CASTLE = p->bQUEENSIDE_CASTLE;
    t.pos.EN_PASSANT_FILE = p->EN_PASSANT_FILE;
    t.pos.FIFTY_MOVE_COUNTER = p->FIFTY_MOVE_COUNTER;
    t.pos.wKING_SQUARE = p->wKING_SQUARE;
    t.pos.bKING_SQUARE = p->bKING_SQUARE;
    t.pos.SQUARE_FROM = p->SQUARE_FROM;
    t.pos.SQUARE_TO = p->SQUARE_TO;
    t.pos.PLAYER_TURN = p->PLAYER_TURN;
    t.pos.GAME_STATE = p->GAME_STATE;
    examinePieces(&t);
    return t.childPoolLength;
}

int minimumSufficientPieceCounts[NUM_PIECES] = { 1, 2, 2, 1, 1, 0, 1, 2, 2, 1, 1, 0 };

// Return 1 if neither player has the material to checkmate.
bool checkInsufficientMatingMaterial() {
    bool whiteMinorPiece = 0, blackMinorPiece = 0;
    char* b = history[gameLength - 1].board;

    for (int i = 0; i < 64; i++) {
        switch (b[i]) {
        case wPAWN:
        case wROOK:
        case wQUEEN:
        case bPAWN:
        case bROOK:
        case bQUEEN:
            return 0;
        case wKNIGHT:
        case wBISHOP:
            if (whiteMinorPiece) return 0;
            whiteMinorPiece = 1;
            break;
        case bKNIGHT:
        case bBISHOP:
            if (blackMinorPiece) return 0;
            blackMinorPiece = 1;
            break;
        }
    }

    return 1;
}

// Check the game position for 50-move rule, insufficient mating material, and threefold repetition draws.
// Return 1 if ending the game in a draw.
bool checkDrawsEngine() {
    position* pos = history + gameLength - 1;
    return pos->FIFTY_MOVE_COUNTER >= 100 || checkInsufficientMatingMaterial() || checkThreefoldRepetition();
}

// Check the game position for 50-move rule, insufficient mating material, and threefold repetition draws.
// Return 1 if ending the game in a draw.
bool checkDraws() {
    position* pos = history + gameLength - 1;
    if (pos->FIFTY_MOVE_COUNTER >= 100) {
        if (drawSetting == FORCE) {
            return 1;
        }
        else {
            printf("Fifty-move rule. Would you like to claim a draw? (y/n)\n");
            char c = getChar();
            return c != '\0' && c != '\n' && c != 'n' && c != 'N';
        }
    }

    if (checkInsufficientMatingMaterial()) {
        if (drawSetting == FORCE) {
            return 1;
        }
        else {
            printf("Insufficient mating material. Would you like to claim a draw? (y/n)\n");
            char c = getChar();
            return c != '\0' && c != '\n' && c != 'n' && c != 'N';
        }
    }

    if (checkThreefoldRepetition()) {
        if (drawSetting == FORCE) {
            return 1;
        }
        else {
            printf("Threefold repetition. Would you like to claim a draw? (y/n)\n");
            char c = getChar();
            return c != '\0' && c != '\n' && c != 'n' && c != 'N';
        }
    }

    return 0;
}

// SQUARE_FROM, SQUARE_TO, and PLAYER_TURN must be set.
// Current turn (last index in history) must be at least 1, game length must be at least 2.
// Return whether to end the game.
bool playAndCheckEndOfGame(bool engine) {
    position* pos = history + gameLength - 1;
    char* b = pos->board;
    playMoveUpdating(pos);

    char newPlayerTurn = pos->PLAYER_TURN;

    // If there are no legal moves, end the game as either checkmate or stalemate.
    if (numMoves(pos) == 0) {
        // Find out whether the king of the player whose turn it is after the move is in check.
        int kingSquare = newPlayerTurn == BLACK ? pos->bKING_SQUARE : pos->wKING_SQUARE;

        if (kingNotInCheck(b, kingSquare)) {
            if (!engine) {
                drawBoard(b, newPlayerTurn);
                printf("Stalemate!\n\n");
            }
            pos->GAME_STATE = DRAW;
        }
        else {
            if (!engine) {
                drawBoard(b, newPlayerTurn);
                printf("Checkmate!\n");
            }
            if (pos->PLAYER_TURN == BLACK) {
                pos->GAME_STATE = WHITE_WIN;
            }else{
                pos->GAME_STATE = BLACK_WIN;
            }
        }
        return 1;
    }

    if (engine) {
        if (checkDrawsEngine()) {
            pos->GAME_STATE = DRAW;
            return 1;
        }
    }else{
        if (checkDraws()) {
            pos->GAME_STATE = DRAW;
            return 1;
        }
    }

    pos->GAME_STATE = NORMAL;
    return 0;
}

// Set the xth square in the FEN code order to piece on the board. Return the board square.
char setFENBoard(char* b, int x, char piece) {
    char square = (7 - (x / 8)) * 8 + (x % 8);
    b[square] = piece;
    return square;
}

// Get input and parse the FEN code stored in inLine and return 1 if valid.
// Must handle empty line case (returns 0) outside this function.
// Parameters must be the allocated board and data, which get cleared and replaced with the FEN data.
bool parseFEN(position* pos) {
    getLine();

    if (inLine[0] == '\n' || inLine[0] == '\0') {
        return 0;
    }

    int l = -1;
    for (int i = 1; i < MAX_LINE_SIZE; i++) {
        if (inLine[i] == '\n' || inLine[i] == '\0') {
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
    char* b = pos->board;
    for (int i = 0; i < 64; i++) {
        b[i] = EMPTY;
    }
    pos->EN_PASSANT_FILE = -1;
    pos->FIFTY_MOVE_COUNTER = 0;
    pos->SQUARE_FROM = UNDEFINED;
    pos->SQUARE_TO = UNDEFINED;
    pos->GAME_STATE = NORMAL;

    // Parse the piece locations on the board.
    int x = 0;
    int i = 0;
    for (;; i++) {
        if (x >= 64) break;
        switch (inLine[i]) {

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
            pos->wKING_SQUARE = setFENBoard(b, x++, wKING);
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
            pos->bKING_SQUARE = setFENBoard(b, x++, bKING);
            numBlackKings++;
            break;

        default:
            // Numbers indicate consecutive empty board spaces.
            if (inLine[i] >= '0' && inLine[i] <= '8') {
                int c = inLine[i] - '0';
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
        bool flag = 0;

        switch (inLine[i]) {
            case '\n':
            case '\0':
                printf("FEN code ended early at player turn indicator.\n");
                return 0;
            case 'w':
            case 'W':
                pos->PLAYER_TURN = WHITE;
                flag = 1;
                break;
            case 'b':
            case 'B':
                pos->PLAYER_TURN = BLACK;
                flag = 1;
                break;
        }
        i++;

        if (flag) break;
    }


    // Assume we can castle if kings and rooks are in the right positions.
    if (b[4] == wKING) {
        if (b[0] == wROOK) pos->wQUEENSIDE_CASTLE = 1;
        if (b[7] == wROOK) pos->wKINGSIDE_CASTLE = 1;
    }
    if (b[60] == bKING) {
        if (b[56] == bROOK) pos->bQUEENSIDE_CASTLE = 1;
        if (b[63] == bROOK) pos->bKINGSIDE_CASTLE = 1;
    }

    if (numMoves(pos) == 0) {
        printf("There are no legal moves in this FEN code position.\n");
        return 0;
    }

    return 1;
}

// Get a valid FEN code from the user and return 0 if the user enters a blank line.
bool getFEN(position* pos) {
    while (!parseFEN(pos)) {
        if (inLine[0] == '\n' || inLine[0] == '\0') return 0;
        printf("Type a valid FEN code: ");
    }

    return 1;
}

// Copy the last game position in the history to a new one.
void copyGamePosition() {
    // Allocate space for this position.
    gameLength++;
    history = (position*)realloc(history, gameLength * sizeof(position));

    history[gameLength - 1] = history[gameLength - 2];
}

// Have the engine evaluate the last position in the history and choose a move.
// Store the move in the last position in the history.
bool engineChooseMove(eval maxEvalLossAllowed) {
    double t = evaluationTimeLimitMin + ((double)random() / (double)ULLONG_MAX) * (evaluationTimeLimitMax - evaluationTimeLimitMin);
    position* pos = history + gameLength - 1;
    setupEvaluation(pos, &bestWeights, 1);
    waitEvaluation((int)t, INT_MAX, INT_MAX);

    int choice = chooseMove(maxEvalLossAllowed);
    if (choice == -1) {
        printf("Engine could not find a move. Ending the game.\n");
        return 0;
    }
    else {
        printf("Engine plays %s.\n", moveToString(choice, pos));
    }

    pos->SQUARE_FROM = sortedMoves[choice]->moveFrom;
    pos->SQUARE_TO = sortedMoves[choice]->moveTo;
    return 1;
}

// Plays a game between two engines.
void play0Player() {
    clearConsole();

    printf("Enter a starting FEN code or a blank line for the default starting position: ");
    setupBoard();
    if (!getFEN(history)) {
        setupBoard();
    }

    printf("Enter engine difficulty (%i-%i): ", DIFFICULTY_MIN, DIFFICULTY_MAX);
    int difficulty = getNumber(DIFFICULTY_MIN, DIFFICULTY_MAX, 0);
    eval maxEvalLossAllowed = ((double)(DIFFICULTY_MAX - difficulty) / (double)(DIFFICULTY_MAX - DIFFICULTY_MIN)) * 50;

    while (1) {
        position* pos = history + gameLength - 1;
        char* b = pos->board;
        clearConsole();
        drawBoard(b, pos->PLAYER_TURN);

        copyGamePosition();

        // Either engine chooses.
        if (!engineChooseMove(difficulty)) return;
        pos->PLAYER_TURN = 1 - (pos + gameLength - 2)->PLAYER_TURN;

        if (playAndCheckEndOfGame(0)) break;
    }
}

// Plays a game between the player and engine.
void play1Player() {
    clearConsole();

    printf("Enter a starting FEN code or a blank line for the default starting position: ");
    setupBoard();
    if (!getFEN(history)) {
        setupBoard();
    }

    printf("Enter engine difficulty (%i-%i): ", DIFFICULTY_MIN, DIFFICULTY_MAX);
    int difficulty = getNumber(DIFFICULTY_MIN, DIFFICULTY_MAX, 0);

    printf("Choose white, black, or random (w/b/any): ");
    switch (getChar()) {
    case '\n':
        return;
    case 'w':
    case 'W':
        playerRole = WHITE;
        break;
    case 'b':
    case 'B':
        playerRole = BLACK;
        break;
    default:
        playerRole = randomRange(0, 1);
        break;
    }

    while (1) {
        position* pos = history + gameLength - 1;
        char* b = pos->board;
        clearConsole();
        drawBoard(b, pos->PLAYER_TURN);

        copyGamePosition();

        // Get the move to play next and store it in historyD.
        if (playerRole == pos->PLAYER_TURN) {
            // Player chooses.
            if (!playerChooseMove(pos)) return;
        }
        else {
            // Engine chooses.
            if(!engineChooseMove(difficulty)) return;
        }
        pos->PLAYER_TURN = 1 - (pos + gameLength - 2)->PLAYER_TURN;

        if (playAndCheckEndOfGame(0)) break;
    }
}

// Plays a game between two players.
void play2Player() {
    clearConsole();

    printf("Enter a starting FEN code or a blank line for the default starting position: ");
    setupBoard();
    if (!getFEN(history)) {
        setupBoard();
    }

    while (1) {
        position* pos = history + gameLength - 1;
        char* b = pos->board;
        clearConsole();
        drawBoard(b, pos->PLAYER_TURN);

        copyGamePosition();

        // Either player chooses.
        if (!playerChooseMove(pos)) return;
        pos->PLAYER_TURN = 1 - (pos + gameLength - 2)->PLAYER_TURN;

        if (playAndCheckEndOfGame(0)) break;
    }
}

// Analyze a position typed by the user.
void analyzePosition() {
    printf("Enter a position FEN code to analyze: ");

    if (!getFEN(&analysisPos)) {
        return;
    }

    bool playerTurn = analysisPos.PLAYER_TURN;
    char* b = analysisPos.board;
    drawBoard(b, playerTurn);

    printf("Analyzing for %i ms...\n\n", evaluationTimeLimitAnalysis);

    setupEvaluation(&analysisPos, &bestWeights, 1);

    unsigned long long start = getTime();

    waitEvaluation(evaluationTimeLimitAnalysis, INT_MAX, INT_MAX);

    unsigned long long end = getTime();

    // Print the choices and their evals.
    int numChoices = nodes->numChildren;
    double ms = (double)(end - start) / 1000000.0;
    printf("Analyzed for max %i ms (actual %i ms) and found %i moves for ", evaluationTimeLimitAnalysis, (int)ms, numChoices);
    playerTurn ? printf("Black") : printf("White");

    printf(" with %i nodes.\n", numNodes.load());

    int nodesAdded = calcNumNodesAdded.load();
    int nodesExamined = calcNumNodesExamined.load();
    printf("%i nodes added (%f per ms)\n", nodesAdded, (double)nodesAdded / ms);
    printf("%i nodes examined (%f per ms)\n", nodesExamined, (double)nodesExamined / ms);

    printf("# stalemates / white wins / black wins / normals found: %i/%i/%i/%i\n", calcNumStalematesFound.load(), calcNumWhiteWinsFound.load(), calcNumBlackWinsFound.load(), calcNumNormalsFound.load());

    for (int i = 0; i < numChoices; i++) {
        printf("----------");
        printf(moveToString(i, &analysisPos));
        printf(": ");
        eval e = sortedMoves[i]->e;
        printEval(e);
        printf("----------\n");

        if (printAnalysisResponses) {
            int nc = sortedMoves[i]->numChildren;
            for (int j = 0; j < nc; j++) {
                node* child = nodes + sortedMoves[i]->childStartIndex + j;
                printf("%s%s: ", squareToString(child->moveFrom), squareToString(child->moveTo));
                printEval(child->e);
                printf("   ");
            }
            printf("\n");
        }
    }
    printf("\n");
}

// Set a bool setting based on the user typing y/n.
void setBoolSetting(bool* s) {

    char c = getChar();
    switch (c) {
    case 'y':
    case 'Y':
        *s = 1;
        break;
    case 'n':
    case 'N':
        *s = 0;
        break;
    }
}

void printSettings() {
    printf("Draw board with Unicode characters: %s\n", unicodeEnabled ? "YES" : "NO");
    printf("Reverse the White/Black letters on the board: %s\n", reverseWhiteBlackLetters ? "YES" : "NO");
    printf("Use * instead of . to draw empty board squares: %s\n", useAsterisk ? "YES" : "NO");
    printf("Show the board coordinates: %s\n", showBoardCoordinates ? "YES" : "NO");
    printf("Use capital letters for board coordinates: %s\n", useCapitalCoordinates ? "YES" : "NO");
    printf("Print the move choices after evaluating in a 1-player game: %s\n", evaluationPrintChoices ? "YES" : "NO");
    printf("Use pluses on eval numbers: %s\n", usePlusesOnEvalNumbers ? "YES" : "NO");
    printf("Print responses to moves in analysis: %s\n", printAnalysisResponses ? "YES" : "NO");
    printf("Minimum time limit for game evaluation: %i ms\n", evaluationTimeLimitMin);
    printf("Maximum time limit for game evaluation: %i ms\n", evaluationTimeLimitMax);
    printf("Time limit for analysis evaluation: %i ms\n", evaluationTimeLimitAnalysis);
    printf("Evaluation depth limit: %i\n", evaluationDepthLimit);
    
    printf("Draw offering: ");
    switch (drawSetting) {
    case NO_DRAWS:
        printf("NO DRAWS\n\n");
        break;
    case ASK:
        printf("ASK FOR DRAW\n\n");
        break;
    case FORCE:
        printf("FORCE DRAW\n\n");
        break;
    }
}

// Settings menu.
void settings() {

    printf("Current settings:\n");
    printSettings();

    printf("Draw board with Unicode characters (y/n): ");
    setBoolSetting(&unicodeEnabled);

    printf("Reverse the White/Black letters on the board (y/n): ");
    setBoolSetting(&reverseWhiteBlackLetters);

    printf("Use * instead of . to draw empty board squares (y/n): ");
    setBoolSetting(&useAsterisk);

    printf("Show the board coordinates (y/n): ");
    setBoolSetting(&showBoardCoordinates);

    printf("Use capital letters for board coordinates (y/n): ");
    setBoolSetting(&useCapitalCoordinates);

    printf("Print the move choices after evaluating in a 1-player game (y/n): ");
    setBoolSetting(&evaluationPrintChoices);

    printf("Use pluses on eval numbers (y/n): ");
    setBoolSetting(&usePlusesOnEvalNumbers);

    printf("Print responses to moves in analysis (y/n): ");
    setBoolSetting(&printAnalysisResponses);

    printf("Minimum time limit for game evaluation [1, 100000]: ");
    evaluationTimeLimitMin = getNumber(1, 100000, 0);

    printf("Maximum time limit for game evaluation [%i, 100000]: ", evaluationTimeLimitMin);
    evaluationTimeLimitMax = getNumber(evaluationTimeLimitMin, 100000, 0);

    printf("Time limit for analysis evaluation [1, 100000]: ");
    evaluationTimeLimitAnalysis = getNumber(1, 100000, 1);

    printf("Evaluation depth limit [1, 100]: ");
    evaluationDepthLimit = getNumber(1, 100, 0);

    printf("Draw offering (n for no draws, a to ask for a draw, f to force a draw: ");
    switch (getChar()) {
    case 'n':
    case 'N':
        drawSetting = NO_DRAWS;
        break;
    case 'a':
    case 'A':
        drawSetting = ASK;
        break;
    case 'f':
    case 'F':
        drawSetting = FORCE;
        break;
    }

    printf("\nNew settings:\n");
    printSettings();
}

// Print a map of all self-weights for the given piece in the given weights table.
void printSelfWeights(W* w, char piece) {
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            int x = (7 - i) * 8 + j;
            printf("%6i ", w->weight[piece][x][piece][x]);
        }
        printf("\n");
    }
    printf("\n");
}

// Perform one step of the evaluation training process.
// adjustmentFactor is between 0 and 1 and is how much of a piece's point value its self-weight can change by per step.
void trainStep(double spm, double adjustmentFactor) {
    int numModels = 2;

    // Allocate trainingWeights if unallocated.
    trainingWeights = (W*)realloc(trainingWeights, sizeof(W) * numModels);

    // Copy the bestWeights to trainingWeights with some variation.
    for (int m = 0; m < numModels; m++) {
        for (int i = 0; i < NUM_PIECES; i++) {
            for (int j = 0; j < 64; j++) {
                for (int k = 0; k < NUM_PIECES; k++) {
                    for (int l = 0; l < 64; l++) {
                        // Calculate the range of possible changes.
                        double v = adjustmentFactor * (double)piecePointValues[i]; // Start with a fraction of the piece's point value.
                        
                        // Get the distance between square j and square l.
                        int x = (l % 8) - (j % 8), y = (l / 8) - (j / 8);
                        if (x < 0) x = -x;
                        if (y < 0) y = -y;
                        int d = x + y;

                        double f = 1.0 / (double)(1 << d);
                        v *= f;
                        int r = (int)v;

                        // Vary trainingWeights randomly.
                        eval e = bestWeights.weight[i][j][k][l] + randomRange(-r, r);
                        if (e >= 5000) {
                            e = 5000;
                        }else if(e <= -5000){
                            e = -5000;
                        }
                        (trainingWeights + m)->weight[i][j][k][l] = e;
                    }
                }
            }
        }
    }

    setupBoard();

    // Have the models play a game against each other.
    while (1) {
        position* pos = history + gameLength - 1;
        char* b = pos->board;
        clearConsole();
        drawBoard(b, WHITE);
        copyGamePosition();

        printSelfWeights(trainingWeights, 2);

        // Either model chooses a move.
        setupEvaluation(pos, trainingWeights + (gameLength % 2), 1);
        waitEvaluation(spm, INT_MAX, INT_MAX);

        if (nodes->numChildren == 0) break;

        pos->SQUARE_FROM = sortedMoves[0]->moveFrom;
        pos->SQUARE_TO = sortedMoves[0]->moveTo;
        pos->PLAYER_TURN = 1 - (history + gameLength - 2)->PLAYER_TURN;

        if (playAndCheckEndOfGame(1)) break;
    }

    // If the game ended in a checkmate, copy the trainingWeights of the winner to bestWeights.
    position* pos = history + gameLength - 1;
    int result = pos->GAME_STATE;
    int winner = -1;
    if (result == 1) {
        winner = 0;
    }else if(result == 2){
        winner = 1;
    }
    if(winner >= 0) {
        for (int i = 0; i < NUM_PIECES; i++) {
            for (int j = 0; j < 64; j++) {
                for (int k = 0; k < NUM_PIECES; k++) {
                    for (int l = 0; l < 64; l++) {
                        bestWeights.weight[i][j][k][l] = trainingWeights[winner].weight[i][j][k][l];
                    }
                }
            }
        }
    }

    clearConsole();
    drawBoard(pos->board, WHITE);
    printf("Finished a training game. Winner was %i.\n", winner);
}

// Training menu.
void train() {

    printf("Seconds to train for [0.001, 100000.0]: ");
    double time = getNumber(0.001, 100000.0, 1);
    printf("Seconds per move [0.00001, 100.0]: ");
    double spm = getNumber(0.00001, 100.0, 1);
    printf("Weight adjustment factor [0.0, 1.0]: ");
    double adjustmentFactor = getNumber(0.0, 1.0, 1);

    struct timespec start;
    timespec_get(&start, TIME_UTC);
    long long s = start.tv_sec;
    long long ns = start.tv_nsec;

    while (1) {

        // Check if exceeding the time limit.
        struct timespec now;
        timespec_get(&now, TIME_UTC);
        long long diff = ((long long)now.tv_sec - s) * 1000000000ll + ((long long)now.tv_nsec - ns);
        if ((double)diff >= time * 1000000000.0) {
            break;
        }

        // Perform one step of the training.
        trainStep(spm, adjustmentFactor);
    }
}

// Main driver menu.
bool menu() {
    printf("Enter a blank line at any time to return to this menu.\n");
    printf("Play 0 player (0), 1 player (1), 2 players (2), train engine (t), analyze a position (p), or settings (s), or anything else to exit: ");

    switch (getChar()) {
    case '0':
        play0Player();
        break;
    case '1':
        play1Player();
        break;
    case '2':
        play2Player();
        break;
    case 't':
    case 'T':
        train();
        break;
    case 'p':
    case 'P':
        analyzePosition();
        break;
    case 's':
    case 'S':
        settings();
        break;
    case '\n':
        return 0;
    }

    return 1;
}

void resetBuffers() {
    if (inLine == NULL) {
        inLine = (char*)calloc(MAX_LINE_SIZE, 1);
    } else {
        for (int i = 0; i < MAX_LINE_SIZE; i++) {
            inLine[i] = 0;
        }
    }
    if (outLine == NULL) {
        outLine = (char*)calloc(MAX_LINE_SIZE, 1);
    }else{
        for (int i = 0; i < MAX_LINE_SIZE; i++) {
            outLine[i] = 0;
        }
    }
    if (fileLine == NULL) {
        fileLine = (char*)calloc(MAX_LINE_SIZE, 1);
    }
    else {
        for (int i = 0; i < MAX_LINE_SIZE; i++) {
            fileLine[i] = 0;
        }
    }
}

// Run the user interface application.
void runUI() {

    // Loop the menu screen if the user returns to the menu at any time.
    while (menu()) {}
}

int readInt() {

    int x = 0;

    bool neg = 0;
    if (inLine[inLinePos] == '-') {
        neg = 1;
        inLinePos++;
    }

    // Read this number.
    while (inLine[inLinePos] >= '0' && inLine[inLinePos] <= '9') {
        x *= 10;
        x += inLine[inLinePos] - '0';
        inLinePos++;
    }

    inLinePos++;

    if (neg) x *= -1;
    return x;
}

// Read a position code and allocate and set the analysisBoard to the position.
void readPosition(char* text, position* pos) {

    // Fill the analysisBoard with chars from input.
    for (int i = 0; i < 64; i++) {
        pos->board[i] = readInt();
    }
    pos->wKINGSIDE_CASTLE = readInt();
    pos->wQUEENSIDE_CASTLE = readInt();
    pos->bKINGSIDE_CASTLE = readInt();
    pos->bQUEENSIDE_CASTLE = readInt();
    pos->EN_PASSANT_FILE = readInt();
    pos->FIFTY_MOVE_COUNTER = readInt();
    pos->wKING_SQUARE = readInt();
    pos->bKING_SQUARE = readInt();
    pos->SQUARE_FROM = readInt();
    pos->SQUARE_TO = readInt();
    pos->PLAYER_TURN = readInt();
    pos->GAME_STATE = readInt();
}

void writeBool(bool x) {
    if (x) {
        outLine[outLinePos] = '1';
        outLinePos++;
    }else{
        outLine[outLinePos] = '0';
        outLinePos++;
    }
    outLine[outLinePos] = ' ';
    outLinePos++;
}

void writeInt(long long x) {
    if (x < 0) {
        outLine[outLinePos] = '-';
        outLinePos++;
        x *= -1;
    }
    if (x == 0) {
        outLine[outLinePos] = '0';
        outLinePos++;
    }
    else {
        long long p = 1;
        while (p <= x) {
            p *= 10;
        }
        p /= 10;
        while (p > 0) {
            outLine[outLinePos] = '0' + (x / p);
            outLinePos++;
            x -= p * (x / p);
            p /= 10;
        }
    }
    outLine[outLinePos] = ' ';
    outLinePos++;
}

void writeString(char* x) {
    for (int i = 0; x[i] != '\0'; i++) {
        outLine[outLinePos] = x[i];
        outLinePos++;
    }
    outLine[outLinePos] = ' ';
    outLinePos++;
}

void _init(int totalNumNodesAllowed, int threadCount) {
    writeBool(init(totalNumNodesAllowed, threadCount));
}

// Run the setup for analysis operation after init has been called.
void _setupEvaluation(int depthLimit, char* text) {

    // Set settings based on the details.
    evaluationDepthLimit = depthLimit;

    readPosition(text, &analysisPos);
    writeBool(setupEvaluation(&analysisPos, &bestWeights, 1));
}

// Test a position for legality.
// Print a 1 or 0 depending on whether the given move is legal on the given position.
void _testLegality(char f, char t, char* text) {
    position testPos;
    readPosition(text, &testPos);

    // Return if the move is legal.
    writeBool(isLegalMove(&testPos, f, t));
}

// Test a position for check.
// Print a 1 or 0 depending on whether the given king is in check on the given position.
void _testCheck(bool isBlack, char* text) {
    position testPos;
    readPosition(text, &testPos);

    // Return if the king is in check.
    char kingSquare = isBlack ? analysisPos.bKING_SQUARE : analysisPos.wKING_SQUARE;
    writeBool(!kingNotInCheck(testPos.board, kingSquare));
}

void _getOutputData() {
    getSortedChoices();

    if (nodes == 0) {
        writeInt(0);
    }
    else {
        writeInt(nodes->numChildren);
        int numChoices = nodes->numChildren;
        for (int i = 0; i < numChoices; i++) {
            writeInt(sortedMoves[i]->moveFrom);
            writeInt(sortedMoves[i]->moveTo);
            writeInt(sortedMoves[i]->e);
            writeString(moveToString(i, &analysisPos));
        }
    }
    writeInt(calcNumNodesAdded.load());
    writeInt(calcNumNodesExamined.load());
}

void _startEvaluation(int timeLimitMS, int addLimit, int examineLimit) {
    if (!setupComplete) {
        writeBool(0);
        return;
    }
    startEvaluation(timeLimitMS, addLimit, examineLimit);
    writeBool(1);
}

void _stopEvaluation() {
    stopEvaluation();
    getSortedChoices();
    writeBool(1);
}

inline bool firstTwo(char a, char b) {
    return inLine[0] == a && inLine[1] == b;
}

int main(int argc, char* argv[]) {
    resetBuffers();
    setupWeights(0);
    
    if (argc == 1) {
        // Run the input checker.
        while (1) {
            getLine();
            inLinePos = 3;
            outLinePos = 0;
            
            if (inLine[0] == '\n') {
                break; // Escape the input checker.
            } else if (firstTwo('e', 'x')) {
                return 0;
            } else if (firstTwo('t', 'l')) {
                char f = readInt();
                char t = readInt();
                _testLegality(f, t, inLine + inLinePos);
            } else if (firstTwo('t', 'c')) {
                bool isBlack = readInt() != 0;
                _testCheck(isBlack, inLine + inLinePos);
            } else if (firstTwo('i', 'n')) {
                int totalNumNodesAllowed = readInt();
                int threadCount = readInt();
                _init(totalNumNodesAllowed, threadCount);
            } else if (firstTwo('s', 'e')) {
                int depthLimit = readInt();
                _setupEvaluation(depthLimit, inLine + inLinePos);
            } else if (firstTwo('g', 'o')) {
                // Start the evaluation. This thread will keep checking input for whether to stop while other threads evaluate.
                _startEvaluation(INT_MAX, INT_MAX, INT_MAX);
            } else if (firstTwo('s', 't')) {
                // Stop the evaluation.
                _stopEvaluation();
            } else if (firstTwo('g', 'd')) {
                _getOutputData();
            }

            // Finish and print the outLine.
            outLine[outLinePos] = '\n';
            outLinePos++;
            outLine[outLinePos] = '\0';
            outLinePos++;
            printf(outLine);
            fflush(stdout);
        }
    }

    SetConsoleOutputCP(CP_UTF8);

    init(20000000, 2);

    runUI();
    
    return 0;
}
