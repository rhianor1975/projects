/*
 * fidhcheall (fidch) - chess engine tester
 * ================================
 * Named for the ancient Gaelic strategy board game (Scottish Gaelic
 * "fidhcheall"); the word became the modern Gaelic word for chess.
 *
 * Plays a match of N games between two UCI chess engines (launched as
 * subprocesses) and saves the results as a single PGN file.
 *
 * Pure C, no external libraries: includes its own small chess rules
 * engine (legal move generation, checkmate/stalemate/draw detection,
 * SAN notation) and talks to the engines over pipes using fork/exec.
 * POSIX only (fork/pipe/select/pthread) -- macOS and Linux, not native
 * Windows without WSL.
 *
 * Build:
 *   gcc -O2 -Wall -pthread -o fidch fidhcheall.c -lm
 *
 * Usage:
 *   ./fidch -r 100 -e1 ./engineA -e2 ./engineB
 *   ./fidch -r 20 -e1 ./ferrum -e2 ./stockfish -t 500 -o match.pgn
 *   ./fidch -r 50 -e1 ./ferrum -e2 ./ferrum -n1 "Ferrum-A" -n2 "Ferrum-B" \
 *         --e2-options Threads=4,Hash=256 --random-plies 4 --seed 1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>      /* for O_WRONLY */
#include <pthread.h>    /* for -c/--concurrency worker pool */

#define FIDCH_VERSION "0.1.0"

/* ============================================================
 * BOARD / MOVE TYPES
 * ============================================================ */

typedef enum { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE } PieceType;
typedef enum { WHITE = 0, BLACK = 1 } Color;

/* Squares are numbered 0..63, a1=0, b1=1, ..., h1=7, a2=8, ..., h8=63
 * (i.e. square = rank*8 + file). bit(s) is that square's bit in a 64-bit
 * board mask; fl()/rk() recover file/rank from a square index. */
#define bit(s) (1ULL << (s))
#define fl(s) ((s) % 8)
#define rk(s) ((s) / 8)
#define sq(f, r) ((r) * 8 + (f))
#define popcount(x) __builtin_popcountll(x)

/* Board is a hybrid representation: bitboards (pieces[]/colors[]) for fast
 * set operations during move generation, plus a parallel mailbox
 * (piece_on[]/color_on[]) for O(1) "what's on this square" lookups (used
 * heavily by SAN generation and capture detection). Both are kept in sync
 * by every function that mutates a Board -- see apply_move and fen_to_board. */
typedef struct {
    uint64_t pieces[6];    /* one bitboard per PieceType, both colors combined */
    uint64_t colors[2];    /* one bitboard per side; pieces[pt] & colors[c] = that side's pieces of that type */
    int side_to_move;
    int castling_rights;   /* bit0=WK bit1=WQ bit2=BK bit3=BQ */
    int en_passant;        /* square or -1 */
    int halfmove_clock;
    int fullmove_number;
    uint8_t piece_on[64];
    int color_on[64];
} Board;

typedef struct {
    int from, to;
    PieceType piece;
    PieceType captured;    /* NO_PIECE if none */
    PieceType promotion;   /* NO_PIECE if none */
    bool is_ep;
    bool is_castle_k;
    bool is_castle_q;
} Move;

#define MAX_MOVES 256
typedef struct {
    Move moves[MAX_MOVES];
    int count;
} MoveList;

/* Precomputed per-square attack masks for the non-sliding pieces (knight,
 * king, pawn captures) -- filled once by init_tables(). Sliding pieces
 * (bishop/rook/queen) can't be precomputed this way since their attacks
 * depend on board occupancy, so those are recomputed on the fly below. */
static uint64_t knight_attacks[64];
static uint64_t king_attacks[64];
static uint64_t pawn_attack_from[2][64];

static const int bishop_dirs[4][2] = { {1,1}, {1,-1}, {-1,1}, {-1,-1} };
static const int rook_dirs[4][2]   = { {1,0}, {-1,0}, {0,1}, {0,-1} };

/* Rays outward from `s` in each of `dirs`, stopping (inclusive) at the first
 * occupied square in each direction -- the classic ray-cast approach to
 * sliding-piece attacks, with no precomputed magic-bitboard tables. */
static uint64_t sliding_attacks(int s, uint64_t occ, const int dirs[][2], int ndirs) {
    uint64_t att = 0;
    int f0 = fl(s), r0 = rk(s);
    for (int d = 0; d < ndirs; d++) {
        int f = f0 + dirs[d][0], r = r0 + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            int t = sq(f, r);
            att |= bit(t);
            if (occ & bit(t)) break;
            f += dirs[d][0]; r += dirs[d][1];
        }
    }
    return att;
}
static uint64_t bishop_attacks(int s, uint64_t occ) { return sliding_attacks(s, occ, bishop_dirs, 4); }
static uint64_t rook_attacks(int s, uint64_t occ)   { return sliding_attacks(s, occ, rook_dirs, 4); }
static uint64_t queen_attacks(int s, uint64_t occ)  { return bishop_attacks(s, occ) | rook_attacks(s, occ); }

/* Fills knight_attacks[]/king_attacks[]/pawn_attack_from[][] for every
 * square. Must run once before any move generation (main() calls it right
 * after arg parsing); the tables are read-only afterwards, including under
 * -c N concurrency, so no locking is needed once this returns. */
static void init_tables(void) {
    for (int s = 0; s < 64; s++) {
        int f = fl(s), r = rk(s);
        uint64_t n = 0;
        int nf[8] = {1,1,-1,-1,2,2,-2,-2}, nr[8] = {2,-2,2,-2,1,-1,1,-1};
        for (int i = 0; i < 8; i++) {
            int tf = f + nf[i], tr = r + nr[i];
            if (tf >= 0 && tf < 8 && tr >= 0 && tr < 8) n |= bit(sq(tf, tr));
        }
        knight_attacks[s] = n;

        uint64_t k = 0;
        for (int df = -1; df <= 1; df++)
            for (int dr = -1; dr <= 1; dr++) {
                if (!df && !dr) continue;
                int tf = f + df, tr = r + dr;
                if (tf >= 0 && tf < 8 && tr >= 0 && tr < 8) k |= bit(sq(tf, tr));
            }
        king_attacks[s] = k;

        uint64_t pw = 0;
        if (f > 0 && r < 7) pw |= bit(sq(f - 1, r + 1));
        if (f < 7 && r < 7) pw |= bit(sq(f + 1, r + 1));
        pawn_attack_from[WHITE][s] = pw;

        uint64_t pb = 0;
        if (f > 0 && r > 0) pb |= bit(sq(f - 1, r - 1));
        if (f < 7 && r > 0) pb |= bit(sq(f + 1, r - 1));
        pawn_attack_from[BLACK][s] = pb;
    }
}

/* Sets up the standard chess starting position. The piece bitboard constants
 * below are the classic square-set literals (e.g. pieces[PAWN] has both rank
 * 2 and rank 7 set); the loop at the end derives piece_on[]/color_on[] from
 * them so the two representations start in sync (see the Board comment). */
static void init_board(Board* b) {
    memset(b, 0, sizeof(*b));
    b->pieces[PAWN]   = 0x00FF00000000FF00ULL;
    b->pieces[KNIGHT] = 0x4200000000000042ULL;
    b->pieces[BISHOP] = 0x2400000000000024ULL;
    b->pieces[ROOK]   = 0x8100000000000081ULL;
    b->pieces[QUEEN]  = 0x0800000000000008ULL;
    b->pieces[KING]   = 0x1000000000000010ULL;
    b->colors[WHITE]  = 0x000000000000FFFFULL;
    b->colors[BLACK]  = 0xFFFF000000000000ULL;
    b->side_to_move = WHITE;
    b->castling_rights = 0xF;
    b->en_passant = -1;
    b->halfmove_clock = 0;
    b->fullmove_number = 1;
    for (int s = 0; s < 64; s++) { b->piece_on[s] = NO_PIECE; b->color_on[s] = -1; }
    for (int c = 0; c < 2; c++)
        for (int pt = 0; pt < 6; pt++) {
            uint64_t bb = b->pieces[pt] & b->colors[c];
            while (bb) { int s = __builtin_ctzll(bb); bb &= bb - 1; b->piece_on[s] = pt; b->color_on[s] = c; }
        }
}

/* Parses a FEN (or the position fields of an EPD line -- trailing opcodes
 * like "bm Nf3; id \"foo\";" are simply ignored past the 4th/6th token).
 * Returns false and leaves *b untouched on a malformed board field. */
static bool fen_to_board(const char* fen, Board* b) {
    char board_part[128] = "", side_part[8] = "w", castle_part[8] = "-", ep_part[8] = "-";
    int halfmove = 0, fullmove = 1;
    int n = sscanf(fen, "%127s %7s %7s %7s %d %d", board_part, side_part, castle_part, ep_part, &halfmove, &fullmove);
    if (n < 1 || !board_part[0]) return false;

    Board nb; memset(&nb, 0, sizeof(nb));
    int f = 0, r = 7;
    for (const char* p = board_part; *p; p++) {
        if (*p == '/') { f = 0; r--; if (r < 0) return false; continue; }
        if (isdigit((unsigned char)*p)) { f += (*p - '0'); continue; }
        if (f > 7 || r < 0) return false;
        int s = sq(f, r);
        int color = isupper((unsigned char)*p) ? WHITE : BLACK;
        PieceType pt;
        switch (tolower((unsigned char)*p)) {
            case 'p': pt = PAWN; break; case 'n': pt = KNIGHT; break;
            case 'b': pt = BISHOP; break; case 'r': pt = ROOK; break;
            case 'q': pt = QUEEN; break; case 'k': pt = KING; break;
            default: return false;
        }
        nb.pieces[pt] |= bit(s);
        nb.colors[color] |= bit(s);
        f++;
    }
    nb.side_to_move = (side_part[0] == 'b') ? BLACK : WHITE;
    nb.castling_rights = 0;
    if (strchr(castle_part, 'K')) nb.castling_rights |= 1;
    if (strchr(castle_part, 'Q')) nb.castling_rights |= 2;
    if (strchr(castle_part, 'k')) nb.castling_rights |= 4;
    if (strchr(castle_part, 'q')) nb.castling_rights |= 8;
    nb.en_passant = -1;
    if (ep_part[0] != '-' && strlen(ep_part) >= 2) {
        int ef = ep_part[0] - 'a', er = ep_part[1] - '1';
        if (ef >= 0 && ef < 8 && er >= 0 && er < 8) nb.en_passant = sq(ef, er);
    }
    nb.halfmove_clock = (n >= 5) ? halfmove : 0;
    nb.fullmove_number = (n >= 6) ? fullmove : 1;

    for (int s = 0; s < 64; s++) { nb.piece_on[s] = NO_PIECE; nb.color_on[s] = -1; }
    for (int c = 0; c < 2; c++)
        for (int pt = 0; pt < 6; pt++) {
            uint64_t bb = nb.pieces[pt] & nb.colors[c];
            while (bb) { int s = __builtin_ctzll(bb); bb &= bb - 1; nb.piece_on[s] = pt; nb.color_on[s] = c; }
        }
    *b = nb;
    return true;
}

/* True if `by_color` has any piece attacking square `s` on the current
 * board. Used both for check detection (in_check below) and for castling
 * legality (a king may not castle through or into check). */
static bool square_attacked(const Board* b, int s, int by_color) {
    uint64_t occ = b->colors[WHITE] | b->colors[BLACK];
    if (by_color == WHITE) {
        if (pawn_attack_from[BLACK][s] & b->pieces[PAWN] & b->colors[WHITE]) return true;
    } else {
        if (pawn_attack_from[WHITE][s] & b->pieces[PAWN] & b->colors[BLACK]) return true;
    }
    if (knight_attacks[s] & b->pieces[KNIGHT] & b->colors[by_color]) return true;
    if (king_attacks[s] & b->pieces[KING] & b->colors[by_color]) return true;
    uint64_t bq = (b->pieces[BISHOP] | b->pieces[QUEEN]) & b->colors[by_color];
    if (bishop_attacks(s, occ) & bq) return true;
    uint64_t rq = (b->pieces[ROOK] | b->pieces[QUEEN]) & b->colors[by_color];
    if (rook_attacks(s, occ) & rq) return true;
    return false;
}

static int king_square(const Board* b, int color) {
    uint64_t k = b->pieces[KING] & b->colors[color];
    return k ? __builtin_ctzll(k) : -1;
}
static bool in_check(const Board* b, int color) {
    int ks = king_square(b, color);
    return ks == -1 ? false : square_attacked(b, ks, !color);
}

static void add_move(MoveList* ml, int from, int to, PieceType piece, PieceType captured,
                      PieceType promo, bool ep, bool ck, bool cq) {
    Move* m = &ml->moves[ml->count++];
    m->from = from; m->to = to; m->piece = piece; m->captured = captured;
    m->promotion = promo; m->is_ep = ep; m->is_castle_k = ck; m->is_castle_q = cq;
}

/* Generates every pseudo-legal move for the side to move -- i.e. moves that
 * follow each piece's movement rules but may leave (or fail to resolve) the
 * mover's own king in check. generate_legal_moves below filters those out
 * by actually applying each one and checking in_check(), which is simpler
 * (if somewhat slower) than tracking pins incrementally -- fine for an
 * engine tester that isn't itself searching a tree. */
static void generate_pseudo_moves(const Board* b, MoveList* ml) {
    ml->count = 0;
    Color c = b->side_to_move;
    uint64_t occ = b->colors[WHITE] | b->colors[BLACK];
    uint64_t own = b->colors[c], enemy = b->colors[!c];

    /* Pawns */
    uint64_t pawns = b->pieces[PAWN] & own;
    int fwd = (c == WHITE) ? 8 : -8;
    int start_rank = (c == WHITE) ? 1 : 6;
    int promo_rank = (c == WHITE) ? 7 : 0;
    while (pawns) {
        int s = __builtin_ctzll(pawns); pawns &= pawns - 1;
        int one = s + fwd;
        if (one >= 0 && one < 64 && !(occ & bit(one))) {
            if (rk(one) == promo_rank) {
                add_move(ml, s, one, PAWN, NO_PIECE, QUEEN, false, false, false);
                add_move(ml, s, one, PAWN, NO_PIECE, ROOK, false, false, false);
                add_move(ml, s, one, PAWN, NO_PIECE, BISHOP, false, false, false);
                add_move(ml, s, one, PAWN, NO_PIECE, KNIGHT, false, false, false);
            } else {
                add_move(ml, s, one, PAWN, NO_PIECE, NO_PIECE, false, false, false);
                if (rk(s) == start_rank) {
                    int two = s + 2 * fwd;
                    if (!(occ & bit(two))) add_move(ml, s, two, PAWN, NO_PIECE, NO_PIECE, false, false, false);
                }
            }
        }
        uint64_t caps = pawn_attack_from[c][s];
        uint64_t targets = caps & enemy;
        while (targets) {
            int t = __builtin_ctzll(targets); targets &= targets - 1;
            PieceType cap = (PieceType)b->piece_on[t];
            if (rk(t) == promo_rank) {
                add_move(ml, s, t, PAWN, cap, QUEEN, false, false, false);
                add_move(ml, s, t, PAWN, cap, ROOK, false, false, false);
                add_move(ml, s, t, PAWN, cap, BISHOP, false, false, false);
                add_move(ml, s, t, PAWN, cap, KNIGHT, false, false, false);
            } else {
                add_move(ml, s, t, PAWN, cap, NO_PIECE, false, false, false);
            }
        }
        if (b->en_passant != -1 && (caps & bit(b->en_passant))) {
            add_move(ml, s, b->en_passant, PAWN, PAWN, NO_PIECE, true, false, false);
        }
    }

    /* Knights */
    uint64_t knights = b->pieces[KNIGHT] & own;
    while (knights) {
        int s = __builtin_ctzll(knights); knights &= knights - 1;
        uint64_t targets = knight_attacks[s] & ~own;
        while (targets) {
            int t = __builtin_ctzll(targets); targets &= targets - 1;
            add_move(ml, s, t, KNIGHT, (PieceType)b->piece_on[t], NO_PIECE, false, false, false);
        }
    }
    /* Bishops / Rooks / Queens */
    for (int pt = BISHOP; pt <= QUEEN; pt++) {
        uint64_t pcs = b->pieces[pt] & own;
        while (pcs) {
            int s = __builtin_ctzll(pcs); pcs &= pcs - 1;
            uint64_t att = (pt == BISHOP) ? bishop_attacks(s, occ) :
                            (pt == ROOK)  ? rook_attacks(s, occ) : queen_attacks(s, occ);
            uint64_t targets = att & ~own;
            while (targets) {
                int t = __builtin_ctzll(targets); targets &= targets - 1;
                add_move(ml, s, t, (PieceType)pt, (PieceType)b->piece_on[t], NO_PIECE, false, false, false);
            }
        }
    }
    /* King */
    int ks = king_square(b, c);
    if (ks != -1) {
        uint64_t targets = king_attacks[ks] & ~own;
        while (targets) {
            int t = __builtin_ctzll(targets); targets &= targets - 1;
            add_move(ml, ks, t, KING, (PieceType)b->piece_on[t], NO_PIECE, false, false, false);
        }
        /* Castling */
        if (c == WHITE) {
            if ((b->castling_rights & 1) && !(occ & (bit(5) | bit(6))) &&
                !square_attacked(b, 4, BLACK) && !square_attacked(b, 5, BLACK) && !square_attacked(b, 6, BLACK))
                add_move(ml, 4, 6, KING, NO_PIECE, NO_PIECE, false, true, false);
            if ((b->castling_rights & 2) && !(occ & (bit(1) | bit(2) | bit(3))) &&
                !square_attacked(b, 4, BLACK) && !square_attacked(b, 3, BLACK) && !square_attacked(b, 2, BLACK))
                add_move(ml, 4, 2, KING, NO_PIECE, NO_PIECE, false, false, true);
        } else {
            if ((b->castling_rights & 4) && !(occ & (bit(61) | bit(62))) &&
                !square_attacked(b, 60, WHITE) && !square_attacked(b, 61, WHITE) && !square_attacked(b, 62, WHITE))
                add_move(ml, 60, 62, KING, NO_PIECE, NO_PIECE, false, true, false);
            if ((b->castling_rights & 8) && !(occ & (bit(57) | bit(58) | bit(59))) &&
                !square_attacked(b, 60, WHITE) && !square_attacked(b, 59, WHITE) && !square_attacked(b, 58, WHITE))
                add_move(ml, 60, 58, KING, NO_PIECE, NO_PIECE, false, false, true);
        }
    }
}

/* Mutates `b` in place to reflect playing `m` (assumed legal -- callers are
 * expected to have gotten it from generate_legal_moves or validated it via
 * uci_to_move). Used both for real game moves and, throughout this file, to
 * probe a hypothetical move on a scratch copy of the board (e.g.
 * generate_legal_moves copies the board before applying each pseudo-move to
 * test for a resulting self-check). */
static void apply_move(Board* b, Move m) {
    Color c = b->side_to_move;
    int oc = !c;

    b->pieces[m.piece] &= ~bit(m.from);
    b->colors[c] &= ~bit(m.from);
    b->piece_on[m.from] = NO_PIECE; b->color_on[m.from] = -1;

    if (m.is_ep) {
        int cap_sq = (c == WHITE) ? m.to - 8 : m.to + 8;
        b->pieces[PAWN] &= ~bit(cap_sq);
        b->colors[oc] &= ~bit(cap_sq);
        b->piece_on[cap_sq] = NO_PIECE; b->color_on[cap_sq] = -1;
    } else if (m.captured != NO_PIECE) {
        b->pieces[m.captured] &= ~bit(m.to);
        b->colors[oc] &= ~bit(m.to);
    }

    PieceType placed = (m.promotion != NO_PIECE) ? m.promotion : m.piece;
    b->pieces[placed] |= bit(m.to);
    b->colors[c] |= bit(m.to);
    b->piece_on[m.to] = placed; b->color_on[m.to] = c;

    if (m.is_castle_k || m.is_castle_q) {
        int rook_from, rook_to;
        if (c == WHITE) { rook_from = m.is_castle_k ? 7 : 0; rook_to = m.is_castle_k ? 5 : 3; }
        else            { rook_from = m.is_castle_k ? 63 : 56; rook_to = m.is_castle_k ? 61 : 59; }
        b->pieces[ROOK] &= ~bit(rook_from); b->colors[c] &= ~bit(rook_from);
        b->piece_on[rook_from] = NO_PIECE; b->color_on[rook_from] = -1;
        b->pieces[ROOK] |= bit(rook_to); b->colors[c] |= bit(rook_to);
        b->piece_on[rook_to] = ROOK; b->color_on[rook_to] = c;
    }

    /* castling rights: king moves clear both of its own rights; each rook's
       home square being either vacated (m.from) or captured on (m.to)
       clears that specific right -- checking both in one condition covers
       "the rook moved" and "the rook was captured" without a separate case. */
    if (m.piece == KING) b->castling_rights &= (c == WHITE) ? ~0x3 : ~0xC;
    if (m.from == 0 || m.to == 0) b->castling_rights &= ~2;
    if (m.from == 7 || m.to == 7) b->castling_rights &= ~1;
    if (m.from == 56 || m.to == 56) b->castling_rights &= ~8;
    if (m.from == 63 || m.to == 63) b->castling_rights &= ~4;

    /* en passant target */
    if (m.piece == PAWN && abs(m.to - m.from) == 16) b->en_passant = (m.from + m.to) / 2;
    else b->en_passant = -1;

    /* clocks */
    if (m.piece == PAWN || m.captured != NO_PIECE) b->halfmove_clock = 0;
    else b->halfmove_clock++;
    if (c == BLACK) b->fullmove_number++;

    b->side_to_move = oc;
}

/* The public move generator: pseudo-legal moves filtered down to ones that
 * don't leave the mover's own king in check. This is the entry point used
 * throughout the file (SAN generation, the game loop, opening-book replay,
 * PGN move parsing) -- generate_pseudo_moves is never called directly
 * outside this function. */
static void generate_legal_moves(const Board* b, MoveList* out) {
    MoveList pseudo;
    generate_pseudo_moves(b, &pseudo);
    out->count = 0;
    Color mover = b->side_to_move;
    for (int i = 0; i < pseudo.count; i++) {
        Board nb = *b;
        apply_move(&nb, pseudo.moves[i]);
        if (!in_check(&nb, mover)) out->moves[out->count++] = pseudo.moves[i];
    }
}

/* FIDE draw-by-insufficient-material check: true if neither side has enough
 * material to force checkmate (bare kings, king+single minor vs king, or
 * opposite sides each down to a single same-colored-square bishop). */
static bool insufficient_material(const Board* b) {
    if (b->pieces[PAWN] || b->pieces[ROOK] || b->pieces[QUEEN]) return false;
    int wn = popcount(b->pieces[KNIGHT] & b->colors[WHITE]);
    int bn = popcount(b->pieces[KNIGHT] & b->colors[BLACK]);
    int wb = popcount(b->pieces[BISHOP] & b->colors[WHITE]);
    int bb = popcount(b->pieces[BISHOP] & b->colors[BLACK]);
    int wminor = wn + wb, bminor = bn + bb;
    if (wminor == 0 && bminor == 0) return true;                 /* K v K */
    if (wminor == 1 && bminor == 0 && wn + wb + bn + bb <= 1) return true; /* K+minor v K */
    if (bminor == 1 && wminor == 0) return true;                 /* K v K+minor */
    if (wn == 0 && bn == 0 && wb == 1 && bb == 1) {               /* KB v KB same color */
        int ws = __builtin_ctzll(b->pieces[BISHOP] & b->colors[WHITE]);
        int bs = __builtin_ctzll(b->pieces[BISHOP] & b->colors[BLACK]);
        if (((fl(ws) + rk(ws)) & 1) == ((fl(bs) + rk(bs)) & 1)) return true;
    }
    return false;
}

/* ============================================================
 * ZOBRIST (for threefold repetition tracking)
 * ============================================================ */
static uint64_t z_piece[2][6][64];
static uint64_t z_side;
static uint64_t z_castle[16];
static uint64_t z_ep[8];

/* 64-bit random value assembled from four 16-bit rand() draws (rand()'s own
 * range is usually only 31 bits, too narrow to fill a zobrist key on its
 * own). Only used to build the zobrist tables below, once. */
static uint64_t rand64(void) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r = (r << 16) ^ (uint64_t)(rand() & 0xFFFF);
    return r;
}
/* Fills the zobrist random tables from a fixed seed (not a real RNG seed --
 * a hardcoded constant), so the hash of a given position is identical across
 * runs and processes. That's required for repetition detection to work at
 * all (it compares hashes computed independently as the game progresses)
 * and is unrelated to --seed, which controls opening variety, not this. */
static void init_zobrist(void) {
    srand(0xC0FFEEu);
    for (int c = 0; c < 2; c++) for (int pt = 0; pt < 6; pt++) for (int s = 0; s < 64; s++) z_piece[c][pt][s] = rand64();
    z_side = rand64();
    for (int i = 0; i < 16; i++) z_castle[i] = rand64();
    for (int i = 0; i < 8; i++) z_ep[i] = rand64();
}
/* XORs together the random values for every piece on the board, side to
 * move, castling rights, and en-passant file into a single incremental hash
 * used purely to detect threefold repetition (see play_game's history[]). */
static uint64_t compute_zobrist(const Board* b) {
    uint64_t h = 0;
    for (int c = 0; c < 2; c++)
        for (int pt = 0; pt < 6; pt++) {
            uint64_t bb = b->pieces[pt] & b->colors[c];
            while (bb) { int s = __builtin_ctzll(bb); bb &= bb - 1; h ^= z_piece[c][pt][s]; }
        }
    if (b->side_to_move == BLACK) h ^= z_side;
    h ^= z_castle[b->castling_rights & 0xF];
    if (b->en_passant != -1) h ^= z_ep[fl(b->en_passant)];
    return h;
}

/* ============================================================
 * SAN NOTATION
 * ============================================================ */
static const char PIECE_LETTER[6] = { 'P', 'N', 'B', 'R', 'Q', 'K' };

static void square_name(int s, char* out) { out[0] = 'a' + fl(s); out[1] = '1' + rk(s); out[2] = 0; }

/* Renders `m` (already known legal in `before`) as Standard Algebraic
 * Notation, e.g. "Nf3", "exd5", "O-O", "e8=Q+". `legal` is the full legal
 * move list in `before` -- needed to detect when a non-pawn move needs
 * disambiguation (two same-type pieces that could both reach the target
 * square) and to tell check ('+') from checkmate ('#') by generating the
 * opponent's replies after the move. */
static void move_to_san(const Board* before, const MoveList* legal, Move m, char* out) {
    if (m.is_castle_k) { strcpy(out, "O-O"); }
    else if (m.is_castle_q) { strcpy(out, "O-O-O"); }
    else if (m.piece == PAWN) {
        char to_sq[3]; square_name(m.to, to_sq);
        char buf[16] = {0};
        if (m.captured != NO_PIECE || m.is_ep) {
            buf[0] = 'a' + fl(m.from); buf[1] = 'x'; buf[2] = 0;
        }
        strcat(buf, to_sq);
        if (m.promotion != NO_PIECE) {
            char p[3] = { '=', PIECE_LETTER[m.promotion], 0 };
            strcat(buf, p);
        }
        strcpy(out, buf);
    } else {
        char buf[16] = {0};
        buf[0] = PIECE_LETTER[m.piece]; buf[1] = 0;

        bool ambiguous = false, same_file = false, same_rank = false;
        for (int i = 0; i < legal->count; i++) {
            const Move* om = &legal->moves[i];
            if (om->piece == m.piece && om->to == m.to && om->from != m.from &&
                !om->is_castle_k && !om->is_castle_q) {
                ambiguous = true;
                if (fl(om->from) == fl(m.from)) same_file = true;
                if (rk(om->from) == rk(m.from)) same_rank = true;
            }
        }
        if (ambiguous) {
            char disamb[3] = {0};
            if (!same_file) { disamb[0] = 'a' + fl(m.from); }
            else if (!same_rank) { disamb[0] = '1' + rk(m.from); }
            else { disamb[0] = 'a' + fl(m.from); disamb[1] = '1' + rk(m.from); }
            strcat(buf, disamb);
        }
        if (m.captured != NO_PIECE) strcat(buf, "x");
        char to_sq[3]; square_name(m.to, to_sq);
        strcat(buf, to_sq);
        strcpy(out, buf);
    }

    Board after = *before;
    apply_move(&after, m);
    if (in_check(&after, after.side_to_move)) {
        MoveList replies; generate_legal_moves(&after, &replies);
        strcat(out, replies.count == 0 ? "#" : "+");
    }
}

/* Parses a UCI move string (e.g. "e2e4", "e7e8q") and matches it against
 * `legal` to recover the full Move (captured piece, castling flags, etc.) --
 * this is both how an engine's own "bestmove" reply is turned into a Move,
 * and how a stored/replayed opening's move list is applied. Returns false
 * (leaving *out untouched) if the string doesn't name a currently-legal
 * move, which callers treat as "illegal move played -- forfeit". */
static bool uci_to_move(const MoveList* legal, const char* s, Move* out) {
    if (strlen(s) < 4) return false;
    int ff = s[0] - 'a', fr = s[1] - '1', tf = s[2] - 'a', tr = s[3] - '1';
    if (ff < 0 || ff > 7 || tf < 0 || tf > 7 || fr < 0 || fr > 7 || tr < 0 || tr > 7) return false;
    int from = sq(ff, fr), to = sq(tf, tr);
    PieceType promo = NO_PIECE;
    if (strlen(s) >= 5) {
        switch (tolower((unsigned char)s[4])) {
            case 'q': promo = QUEEN; break; case 'r': promo = ROOK; break;
            case 'b': promo = BISHOP; break; case 'n': promo = KNIGHT; break;
        }
    }
    for (int i = 0; i < legal->count; i++) {
        const Move* m = &legal->moves[i];
        if (m->from == from && m->to == to && m->promotion == promo) { *out = *m; return true; }
    }
    return false;
}

/* ============================================================
 * ENGINE PROCESS MANAGEMENT (fork + pipes)
 * ============================================================ */

#define READBUF 16384
typedef struct {
    pid_t pid;
    int wfd;    /* write to engine's stdin */
    int rfd;    /* read from engine's stdout */
    char buf[READBUF];
    int len;
    char label[64];   /* human-readable id for log filenames / messages, e.g. "white" */
    char stderr_path[256];
} Engine;

/* Blocking read of one newline-terminated line from the engine's stdout,
 * with a timeout. e->buf/e->len is a small per-engine read buffer: each call
 * first checks for a line already sitting in it (from over-reading on a
 * previous call), then select()s on the pipe and reads more if not. Returns
 * 0 with `out` filled (CR stripped) on success, -1 on timeout or EOF/error
 * -- callers treat -1 as "the engine died or is hung". */
static int reader_getline(Engine* e, char* out, size_t outsize, int timeout_ms) {
    for (;;) {
        for (int i = 0; i < e->len; i++) {
            if (e->buf[i] == '\n') {
                int linelen = i;
                if ((size_t)linelen >= outsize) linelen = (int)outsize - 1;
                memcpy(out, e->buf, linelen);
                out[linelen] = 0;
                if (linelen > 0 && out[linelen - 1] == '\r') out[linelen - 1] = 0;
                memmove(e->buf, e->buf + i + 1, e->len - i - 1);
                e->len -= (i + 1);
                return 0;
            }
        }
        fd_set set; FD_ZERO(&set); FD_SET(e->rfd, &set);
        struct timeval tv; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
        int r = select(e->rfd + 1, &set, NULL, NULL, &tv);
        if (r <= 0) return -1;
        int space = READBUF - e->len;
        if (space <= 0) return -1;
        ssize_t n = read(e->rfd, e->buf + e->len, space);
        if (n <= 0) return -1;
        e->len += (int)n;
    }
}

/* Writes `cmd` plus a trailing newline to the engine's stdin. */
static void engine_send(Engine* e, const char* cmd) {
    /* cmd (e.g. the "position startpos moves ..." string) can grow well
       beyond any fixed stack buffer as a game gets longer. snprintf's
       return value reflects the length that *would* be needed, not how
       much was actually written -- using it directly in write() reads
       past the end of a truncated fixed buffer. Size the buffer to the
       actual command plus terminator/newline instead. */
    size_t cmdlen = strlen(cmd);
    char stackbuf[1024];
    char* buf = stackbuf;
    size_t bufsize = sizeof(stackbuf);
    if (cmdlen + 2 > bufsize) {
        bufsize = cmdlen + 2;
        buf = malloc(bufsize);
        if (!buf) return;
    }
    int n = snprintf(buf, bufsize, "%s\n", cmd);
    if (n > 0) {
        if ((size_t)n >= bufsize) n = (int)bufsize - 1;
        ssize_t w = write(e->wfd, buf, n); (void)w;
    }
    if (buf != stackbuf) free(buf);
}

/* Forks `path` as a subprocess wired up over two pipes (e->wfd to its stdin,
 * e->rfd from its stdout) and returns immediately -- the UCI handshake
 * itself happens in engine_handshake, not here. Safe to call from any
 * worker thread under -c N concurrency: the child between fork() and
 * exec()/  _exit() only calls dup2/close/open, all async-signal-safe, so
 * this is the standard safe fork-in-a-threaded-program pattern despite the
 * process being multi-threaded when engine_start runs. */
static int engine_start(Engine* e, const char* path) {
    int to_engine[2], from_engine[2];
    if (pipe(to_engine) < 0 || pipe(from_engine) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(to_engine[0], STDIN_FILENO);
        dup2(from_engine[1], STDOUT_FILENO);
        close(to_engine[0]); close(to_engine[1]);
        close(from_engine[0]); close(from_engine[1]);
        /* Route the engine's stderr to a log file (instead of /dev/null) so
           crash output -- ASan/UBSan reports, assertion failures, stack
           traces printed by the engine itself, etc. -- is actually visible
           for debugging instead of being silently discarded. */
        int logfd = open(e->stderr_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logfd >= 0) { dup2(logfd, STDERR_FILENO); close(logfd); }
        else {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        }
        execlp(path, path, (char*)NULL);
        _exit(127);
    }
    close(to_engine[0]);
    close(from_engine[1]);
    e->pid = pid;
    e->wfd = to_engine[1];
    e->rfd = from_engine[0];
    e->len = 0;
    return 0;
}

/* Checks (non-blocking) whether an engine process has actually died, and if
   so, describes how -- crashed with a signal, or exited with a status code.
   Returns 1 and fills `desc` if the process is gone; returns 0 if it's still
   running (a genuine timeout/hang rather than a crash). */
static int engine_diagnose_death(Engine* e, char* desc, size_t desclen) {
    int status;
    pid_t r = waitpid(e->pid, &status, WNOHANG);
    if (r != e->pid) return 0; /* still running, or already reaped */
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        snprintf(desc, desclen, "crashed with signal %d (%s)", sig, strsignal(sig));
    } else if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) snprintf(desc, desclen, "exited cleanly (unexpectedly, mid-game)");
        else snprintf(desc, desclen, "exited with status %d", code);
    } else {
        snprintf(desc, desclen, "terminated for an unknown reason");
    }
    return 1;
}

/* Shuts an engine down at the end of a game: asks nicely ("quit"), gives it
 * up to 500ms to exit on its own, then SIGKILLs it. Always reaps the child
 * (the final waitpid) so a game loop that runs many games in a row doesn't
 * accumulate zombies. */
static void engine_stop(Engine* e) {
    engine_send(e, "quit");
    close(e->wfd);
    close(e->rfd);
    /* Give it a moment to exit cleanly, then force it. */
    for (int i = 0; i < 50; i++) {
        int status;
        pid_t r = waitpid(e->pid, &status, WNOHANG);
        if (r == e->pid) return;
        usleep(10000);
    }
    kill(e->pid, SIGKILL);
    waitpid(e->pid, NULL, 0);
}

/* Returns 0 on success (bestmove parsed into out_move, up to 8 chars), -1 on timeout/error. */
static int engine_go(Engine* e, const char* go_cmd, char* out_move, int timeout_ms) {
    char line[1024];
    engine_send(e, go_cmd);
    while (reader_getline(e, line, sizeof(line), timeout_ms) == 0) {
        if (strncmp(line, "bestmove", 8) == 0) {
            char mv[16] = {0};
            sscanf(line + 8, " %15s", mv);
            if (strlen(mv) < 4) return -1;
            strncpy(out_move, mv, 15); out_move[15] = 0;
            return 0;
        }
    }
    return -1;
}

typedef struct { char key[64]; char val[64]; } KV;
/* Splits a "K=V,K=V,..." string (the value of --eN-options / the ;opt=
 * suffix on -e) into up to `maxn` key/value pairs. Each becomes a UCI
 * "setoption name K value V" sent during engine_handshake below. */
static int parse_kv_list(const char* s, KV* out, int maxn) {
    int n = 0;
    if (!s) return 0;
    char buf[2048]; strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    char* tok = strtok(buf, ",");
    while (tok && n < maxn) {
        char* eq = strchr(tok, '=');
        if (eq) {
            *eq = 0;
            strncpy(out[n].key, tok, sizeof(out[n].key) - 1);
            strncpy(out[n].val, eq + 1, sizeof(out[n].val) - 1);
            n++;
        }
        tok = strtok(NULL, ",");
    }
    return n;
}

/* Starts the engine and runs the full UCI startup sequence: uci -> uciok,
 * apply any --eN-options settings, isready -> readyok, ucinewgame,
 * isready -> readyok again (so the engine has definitely settled before the
 * first "position"/"go"). `label` becomes both e->label and the engine's
 * stderr log filename ("fidhcheall_stderr_<label>.log") -- play_game passes
 * "white"/"black" for the sequential path (one accumulating log per color
 * across the whole run) or a per-job-tagged label under -c N concurrency,
 * where multiple engines would otherwise collide on the same log file.
 * Returns 0 on success, -1 if the process never came up or never answered. */
static int engine_handshake(Engine* e, const char* path, const char* options_str, int setup_timeout_ms, const char* label) {
    strncpy(e->label, label, sizeof(e->label) - 1);
    e->label[sizeof(e->label) - 1] = 0;
    snprintf(e->stderr_path, sizeof(e->stderr_path), "fidhcheall_stderr_%s.log", label);
    if (engine_start(e, path) != 0) return -1;
    char line[1024];
    engine_send(e, "uci");
    while (reader_getline(e, line, sizeof(line), setup_timeout_ms) == 0)
        if (strncmp(line, "uciok", 5) == 0) break;

    KV opts[32];
    int nopts = parse_kv_list(options_str, opts, 32);
    for (int i = 0; i < nopts; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "setoption name %s value %s", opts[i].key, opts[i].val);
        engine_send(e, cmd);
    }

    engine_send(e, "isready");
    while (reader_getline(e, line, sizeof(line), setup_timeout_ms) == 0)
        if (strncmp(line, "readyok", 7) == 0) break;
    engine_send(e, "ucinewgame");
    engine_send(e, "isready");
    while (reader_getline(e, line, sizeof(line), setup_timeout_ms) == 0)
        if (strncmp(line, "readyok", 7) == 0) break;
    return 0;
}

/* ============================================================
 * MATCH LOGIC
 * ============================================================ */

#define MAX_ENGINES 16

/* One competitor: engines[0..n_engines-1] in Args. Populated from -eN/-nN/
 * --eN-options, the repeatable -e PATH[;name=][;opt=] form, or --config
 * lines -- see build_argv_with_config and the CLI parsing loop in main(). */
typedef struct {
    char path[512];
    char name[128];
    char options[1024];
} EngineSpec;

typedef enum { TOURN_AUTO = 0, TOURN_ROUNDROBIN, TOURN_GAUNTLET } TournamentMode;

/* Every setting parsed from the command line (and/or --config), passed by
 * const pointer to everything downstream (play_game, run_pairing,
 * run_tournament_concurrent, ...). Filled once in main() and never mutated
 * afterwards, which is what makes it safe to share across worker threads
 * under -c N without any locking. */
typedef struct {
    int rounds;
    EngineSpec engines[MAX_ENGINES];
    int n_engines;
    TournamentMode tournament;
    const char *output;
    int movetime_ms, depth, nodes;
    int max_moves;
    int random_plies;
    long seed;
    const char *openings_file;
    int quiet;
    int go_timeout_extra_ms;
    int sprt;                                   /* 1 if SPRT early-stop enabled */
    double sprt_elo0, sprt_elo1, sprt_alpha, sprt_beta;
    int concurrency;                             /* -c N; 1 = sequential (default) */
} Args;

/* One engine-vs-engine pairing: engines[i] vs engines[j]. File scope so both
 * the sequential driver and the concurrent worker pool can share it. */
typedef struct { int i, j; } Pairing;

static void usage(const char* prog) {
    printf("fidhcheall (fidch) - chess engine tester\n\n");
    printf("Usage: %s -r ROUNDS -e1 ENGINE1 -e2 ENGINE2 [-e3 ENGINE3 ...] [options]\n\n", prog);
    printf("  -r, --rounds N          number of games per pairing (default: 10)\n");
    printf("  -eN PATH                path to engine N (N=1,2,3,...); -e1/-e2 required\n");
    printf("  -nN NAME                display name for engine N (default: filename)\n");
    printf("      --eN-options K=V,K=V  UCI options for engine N, e.g. Threads=4,Hash=128\n");
    printf("  -e PATH[;name=NAME][;opt=K=V,K=V]  repeatable alternative to -eN, for\n");
    printf("                          large fields (adds engines in the order given)\n");
    printf("      --config FILE       read engines/options/settings from a file instead of\n");
    printf("                          the command line (see README for the file format);\n");
    printf("                          any flag also given on the command line overrides it\n");
    printf("      --tournament roundrobin|gauntlet\n");
    printf("                          with >2 engines: every-pair round-robin (default),\n");
    printf("                          or engine 1 (the challenger) vs the rest\n");
    printf("  -o, --output FILE       output PGN file (default: results.pgn)\n");
    printf("  -t, --movetime MS       think time per move in ms (default: 1000)\n");
    printf("  -d, --depth N           fixed search depth per move (overrides -t)\n");
    printf("      --nodes N           fixed node limit per move (overrides -t/-d)\n");
    printf("      --max-moves N       adjudicate a draw after N full moves (default: 300)\n");
    printf("      --random-plies N    play N random legal opening plies first (default: 0)\n");
    printf("      --seed N            random seed for opening variety\n");
    printf("      --openings FILE     curated opening set: FILE.epd (one FEN per line) or\n");
    printf("                          FILE.pgn (games; first N plies replayed, N from\n");
    printf("                          --random-plies or 20 by default). Overrides\n");
    printf("                          --random-plies as the opening source; entries are\n");
    printf("                          shared across every pairing, played both colors.\n");
    printf("  -q, --quiet             only print the final summary\n");
    printf("  -c, --concurrency N     EXPERIMENTAL: run N games at once (default: 1,\n");
    printf("                          sequential). Up to 2N engine processes live at\n");
    printf("                          once -- cap multi-threaded engines (e.g. an engine\n");
    printf("                          option like Threads=1) or you'll oversubscribe\n");
    printf("                          your CPU cores. SPRT may overshoot its stopping\n");
    printf("                          point by a few in-flight games under concurrency.\n");
    printf("      --sprt E0,E1[,a,b]  SPRT early-stop test H0:elo<=E0 vs H1:elo>=E1\n");
    printf("                          (alpha,beta default 0.05). Uses paired openings +\n");
    printf("                          pentanomial LLR. Needs --random-plies for variety.\n");
    printf("                          Applies per pairing.\n");
    printf("  -h, --help              show this help\n");
    printf("      --version           show version and exit\n");
}

static const char* basename_of(const char* path) {
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void build_go_cmd(const Args* a, char* out, size_t outsize) {
    if (a->nodes > 0) snprintf(out, outsize, "go nodes %d", a->nodes);
    else if (a->depth > 0) snprintf(out, outsize, "go depth %d", a->depth);
    else snprintf(out, outsize, "go movetime %d", a->movetime_ms);
}

typedef struct {
    char result[8];          /* "1-0", "0-1", "1/2-1/2" */
    char termination[128];   /* human-readable reason: "Checkmate", "... engine crashed with signal N (SIGSEGV); forfeit", etc. */
    char san_moves[16384];
    int ply_count;
} GameRecord;

/* Plays exactly one game between two already-selected engines and fills
 * `rec` with the outcome. This is THE primitive everything else in the file
 * is a scheduler around: a plain two-engine match, a round-robin/gauntlet
 * pairing (run_pairing), and the -c N worker pool (run_scheduled_game) all
 * just call this in a loop with different (white_path, black_path, opening)
 * combinations -- the arbiter and engine I/O never change between those
 * cases. Everything it touches (Board, Engine structs, buffers) is
 * stack-local, so concurrent calls from different worker threads never
 * interact with each other.
 *
 * opening_uci: a space-separated UCI move list replayed from startpos
 * before real play begins (paired-openings mode); start_fen: a full FEN to
 * start from instead (from an --openings EPD file) -- mutually exclusive,
 * see the opening-selection code in run_pairing/run_scheduled_game. tag is
 * appended to the engine stderr log labels (see engine_handshake). */
static void play_game(const Args* a, const char* white_path, const char* black_path,
                       const char* white_opts, const char* black_opts, GameRecord* rec, int game_num,
                       const char* opening_uci, const char* start_fen, const char* tag) {
    Engine we, be;
    rec->termination[0] = 0;
    /* tag is "" for the sequential path (preserves today's fixed
       fidhcheall_stderr_white.log / _black.log, one accumulating log per
       color across the whole run) and a per-job suffix like "_p2_r5" under
       -c N, where concurrent games would otherwise all fight over the same
       two files. */
    char wlabel[80], blabel[80];
    snprintf(wlabel, sizeof(wlabel), "white%s", tag ? tag : "");
    snprintf(blabel, sizeof(blabel), "black%s", tag ? tag : "");
    if (engine_handshake(&we, white_path, white_opts, 10000, wlabel) != 0) {
        strcpy(rec->result, "0-1"); strcpy(rec->termination, "White engine failed to start"); return;
    }
    if (engine_handshake(&be, black_path, black_opts, 10000, blabel) != 0) {
        engine_stop(&we);
        strcpy(rec->result, "1-0"); strcpy(rec->termination, "Black engine failed to start"); return;
    }
    {
        FILE* wf = fopen(we.stderr_path, "a"); if (wf) { fprintf(wf, "\n===== Game %d =====\n", game_num); fclose(wf); }
        FILE* bf = fopen(be.stderr_path, "a"); if (bf) { fprintf(bf, "\n===== Game %d =====\n", game_num); fclose(bf); }
    }

    Board board;
    if (start_fen && start_fen[0]) {
        if (!fen_to_board(start_fen, &board)) init_board(&board);
    } else {
        init_board(&board);
    }
    char position_moves[16384] = "";
    rec->san_moves[0] = 0;
    rec->ply_count = 0;

    uint64_t history[2048]; int hist_len = 0;
    history[hist_len++] = compute_zobrist(&board);

    /* Opening plies. If opening_uci is given (paired-openings mode) replay those
       exact moves so both games of a pair share an opening; otherwise pick random
       legal moves as before. A start_fen already *is* the opening -- no plies to
       replay on top of it. */
    const char* op = opening_uci;
    int plies_to_replay = (start_fen && start_fen[0]) ? 0 : a->random_plies;
    /* Local, per-call seed for the fallback below (op ran dry before
       plies_to_replay was satisfied -- rare, but rand() is not reentrant and
       this function now runs concurrently across worker threads under -c N). */
    unsigned fallback_seed = (unsigned)game_num * 2654435761u + 12345u;
    for (int i = 0; i < plies_to_replay; i++) {
        MoveList legal; generate_legal_moves(&board, &legal);
        if (legal.count == 0) break;
        Move m;
        if (op && *op) {
            while (*op == ' ') op++;
            char tok[8]; int k = 0;
            while (*op && *op != ' ' && k < 7) tok[k++] = *op++;
            tok[k] = 0;
            if (k == 0 || !uci_to_move(&legal, tok, &m)) break;
        } else {
            m = legal.moves[rand_r(&fallback_seed) % legal.count];
        }
        char san[16]; move_to_san(&board, &legal, m, san);
        char um[3] = {0}, tm[3] = {0};
        square_name(m.from, um); square_name(m.to, tm);
        char uci_mv[8]; snprintf(uci_mv, sizeof(uci_mv), "%s%s%s", um, tm,
            m.promotion == QUEEN ? "q" : m.promotion == ROOK ? "r" :
            m.promotion == BISHOP ? "b" : m.promotion == KNIGHT ? "n" : "");
        strcat(position_moves, position_moves[0] ? " " : "");
        strcat(position_moves, uci_mv);

        char numbuf[16];
        if (board.side_to_move == WHITE) { snprintf(numbuf, sizeof(numbuf), "%d. ", board.fullmove_number); strcat(rec->san_moves, numbuf); }
        strcat(rec->san_moves, san); strcat(rec->san_moves, " ");
        rec->ply_count++;

        apply_move(&board, m);
        history[hist_len++] = compute_zobrist(&board);
    }

    char go_cmd[64]; build_go_cmd(a, go_cmd, sizeof(go_cmd));
    int go_timeout = (a->nodes > 0 || a->depth > 0) ? 120000 : (a->movetime_ms + a->go_timeout_extra_ms);

    char pos_base[300];
    if (start_fen && start_fen[0]) snprintf(pos_base, sizeof(pos_base), "position fen %s", start_fen);
    else snprintf(pos_base, sizeof(pos_base), "position startpos");

    /* Main move loop: check every draw/mate condition first (cheapest to
       most expensive -- generate_legal_moves is already computed for the
       mate/stalemate check above, so the rest piggyback on it), then ask
       whichever side is to move for its move, validate it against our own
       legal-move list (an engine returning garbage or an illegal move is
       treated the same as a crash: immediate forfeit, not a fidhcheall
       error), apply it, and repeat. */
    for (;;) {
        MoveList legal; generate_legal_moves(&board, &legal);

        if (legal.count == 0) {
            if (in_check(&board, board.side_to_move)) {
                strcpy(rec->result, board.side_to_move == WHITE ? "0-1" : "1-0");
                strcpy(rec->termination, "Checkmate");
            } else {
                strcpy(rec->result, "1/2-1/2");
                strcpy(rec->termination, "Stalemate");
            }
            break;
        }
        if (board.halfmove_clock >= 100) {
            strcpy(rec->result, "1/2-1/2"); strcpy(rec->termination, "Fifty-move rule"); break;
        }
        int reps = 0;
        for (int i = 0; i < hist_len; i++) if (history[i] == history[hist_len - 1]) reps++;
        if (reps >= 3) { strcpy(rec->result, "1/2-1/2"); strcpy(rec->termination, "Threefold repetition"); break; }
        if (insufficient_material(&board)) {
            strcpy(rec->result, "1/2-1/2"); strcpy(rec->termination, "Insufficient material"); break;
        }
        if (board.fullmove_number > a->max_moves) {
            strcpy(rec->result, "1/2-1/2"); strcpy(rec->termination, "Adjudicated: move limit reached"); break;
        }

        Engine* mover = (board.side_to_move == WHITE) ? &we : &be;
        char pos_cmd[16384];
        if (position_moves[0])
            snprintf(pos_cmd, sizeof(pos_cmd), "%s moves %s", pos_base, position_moves);
        else
            snprintf(pos_cmd, sizeof(pos_cmd), "%s", pos_base);
        engine_send(mover, pos_cmd);

        char uci_mv[16];
        if (engine_go(mover, go_cmd, uci_mv, go_timeout) != 0) {
            strcpy(rec->result, board.side_to_move == WHITE ? "0-1" : "1-0");
            char how[128];
            if (engine_diagnose_death(mover, how, sizeof(how))) {
                snprintf(rec->termination, sizeof(rec->termination),
                         "%s engine %s; forfeit (see %s)",
                         board.side_to_move == WHITE ? "White" : "Black", how, mover->stderr_path);
            } else {
                snprintf(rec->termination, sizeof(rec->termination),
                         "%s engine timed out (still running, no bestmove within %d ms); forfeit",
                         board.side_to_move == WHITE ? "White" : "Black", go_timeout);
            }
            break;
        }

        Move m;
        if (!uci_to_move(&legal, uci_mv, &m)) {
            strcpy(rec->result, board.side_to_move == WHITE ? "0-1" : "1-0");
            snprintf(rec->termination, sizeof(rec->termination),
                     "%s engine played illegal move '%s'; forfeit",
                     board.side_to_move == WHITE ? "White" : "Black", uci_mv);
            break;
        }

        char san[16]; move_to_san(&board, &legal, m, san);
        char numbuf[16];
        if (board.side_to_move == WHITE) { snprintf(numbuf, sizeof(numbuf), "%d. ", board.fullmove_number); strcat(rec->san_moves, numbuf); }
        strcat(rec->san_moves, san); strcat(rec->san_moves, " ");
        rec->ply_count++;

        strcat(position_moves, position_moves[0] ? " " : "");
        strcat(position_moves, uci_mv);

        apply_move(&board, m);
        if (hist_len < 2048) history[hist_len++] = compute_zobrist(&board);
    }

    engine_stop(&we);
    engine_stop(&be);
}

/* ------------------------------------------------------------------------
 * Paired openings + match statistics (SPRT, pentanomial, Elo error, LOS)
 * ---------------------------------------------------------------------- */

/* Play `plies` random legal moves from the start position using a private RNG
 * state, writing them as a space-separated UCI string. Deterministic in `st`,
 * so the same pair index always yields the same opening (reproducible w/ --seed). */
static void gen_opening(int plies, unsigned* st, char* out, size_t outsz) {
    Board board; init_board(&board);
    out[0] = 0; size_t used = 0;
    for (int i = 0; i < plies; i++) {
        MoveList legal; generate_legal_moves(&board, &legal);
        if (legal.count == 0) break;
        Move m = legal.moves[rand_r(st) % legal.count];
        char um[3] = {0}, tm[3] = {0}; square_name(m.from, um); square_name(m.to, tm);
        char uci[8]; snprintf(uci, sizeof(uci), "%s%s%s", um, tm,
            m.promotion == QUEEN ? "q" : m.promotion == ROOK ? "r" :
            m.promotion == BISHOP ? "b" : m.promotion == KNIGHT ? "n" : "");
        int n = snprintf(out + used, outsz - used, "%s%s", used ? " " : "", uci);
        if (n > 0) used += (size_t)n;
        apply_move(&board, m);
    }
}

/* Standard logistic Elo formula: expected score (0..1) for a side rated
 * `elo` points above its opponent. */
static double elo_to_score(double elo) { return 1.0 / (1.0 + pow(10.0, -elo / 400.0)); }

/* Normalized (logistic) SPRT log-likelihood ratio from the pentanomial counts.
 * penta[i] counts pairs whose e1 result was i quarter-points (0=LL .. 4=WW).
 * Uses the empirical pair-score variance, which folds in draw rate and the
 * color/opening correlation that pairing removes -- far tighter than raw WDL.
 *
 * On the normal path this is exactly the raw-count statistic (unchanged from
 * before, so already-validated early-stop behavior is untouched). Only when
 * the raw variance is degenerate -- e.g. every pair scored WW so far, giving
 * var == 0 -- does it fall back to a Laplace-smoothed (alpha=0.5/bin)
 * estimate. A flat 1e-9 floor on a zero variance produced an LLR in the
 * millions (correct in sign, useless as a number, wildly overconfident off a
 * couple of pairs); the smoothed fallback gives a sane, still-decisive value
 * instead. Gated on var < 1e-6 so it never engages once there's genuine
 * spread in the results. */
static double sprt_llr(const long penta[5], double elo0, double elo1) {
    long n = 0; for (int i = 0; i < 5; i++) n += penta[i];
    if (n < 2) return 0.0;
    double mean = 0;
    for (int i = 0; i < 5; i++) mean += (i * 0.25) * penta[i];
    mean /= n;
    double var = 0;
    for (int i = 0; i < 5; i++) { double d = i * 0.25 - mean; var += d * d * penta[i]; }
    var /= n;
    if (var < 1e-6) {
        const double alpha = 0.5;
        double ntot = n + 5 * alpha;
        double smean = 0;
        for (int i = 0; i < 5; i++) smean += (i * 0.25) * (penta[i] + alpha);
        smean /= ntot;
        double svar = 0;
        for (int i = 0; i < 5; i++) { double d = i * 0.25 - smean; svar += d * d * (penta[i] + alpha); }
        svar /= ntot;
        mean = smean; var = svar; if (var < 1e-9) var = 1e-9;
    }
    double mu0 = elo_to_score(elo0), mu1 = elo_to_score(elo1);
    return n * (mu1 - mu0) / var * (mean - (mu0 + mu1) / 2.0);
}

/* ------------------------------------------------------------------------
 * --openings FILE: a curated opening set (EPD positions or a PGN of games),
 * shared across every pairing the same way the seeded random plies are --
 * entry k is used for pair index k (cycling if the file has fewer entries
 * than pairs needed), each entry played both colors.
 * ---------------------------------------------------------------------- */
#define MAX_OPENINGS 8192
typedef struct {
    int is_fen;             /* 1: fen[] is a full starting position (EPD); 0: moves_uci[] from startpos (PGN) */
    char fen[160];
    char moves_uci[1024];
} OpeningEntry;

static OpeningEntry* g_openings = NULL;
static int g_n_openings = 0;

/* Replays a PGN game's movetext against our own legal-move/SAN generator to
 * recover it as a UCI move string (matching by SAN, ignoring check/mate
 * suffixes). Stops at max_plies, at a result token, or at the first move it
 * can't match (keeping whatever prefix it already resolved). */
static int pgn_extract_uci(const char* movetext, char* out, size_t outsz, int max_plies) {
    Board board; init_board(&board);
    out[0] = 0; size_t used = 0;
    int plies = 0;
    const char* p = movetext;
    while (*p && (max_plies <= 0 || plies < max_plies)) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '{') { const char* e = strchr(p, '}'); if (!e) break; p = e + 1; continue; }
        if (*p == ';') { const char* e = strchr(p, '\n'); if (!e) break; p = e; continue; }
        if (*p == '$') { p++; while (isdigit((unsigned char)*p)) p++; continue; }
        if (isdigit((unsigned char)*p)) {
            const char* q = p;
            while (isdigit((unsigned char)*q)) q++;
            if (*q == '.') { p = q + 1; while (*p == '.') p++; continue; }
        }
        char tok[16]; int tl = 0;
        while (*p && !isspace((unsigned char)*p) && tl < 15) tok[tl++] = *p++;
        tok[tl] = 0;
        if (tl == 0) continue;
        if (!strcmp(tok, "1-0") || !strcmp(tok, "0-1") || !strcmp(tok, "1/2-1/2") || !strcmp(tok, "*")) break;

        char clean[16]; strncpy(clean, tok, sizeof(clean) - 1); clean[sizeof(clean) - 1] = 0;
        size_t cl = strlen(clean);
        while (cl > 0 && strchr("+#!?", clean[cl - 1])) clean[--cl] = 0;

        MoveList legal; generate_legal_moves(&board, &legal);
        Move found; int match = 0;
        for (int i = 0; i < legal.count; i++) {
            char san[16]; move_to_san(&board, &legal, legal.moves[i], san);
            size_t sl = strlen(san);
            while (sl > 0 && (san[sl - 1] == '+' || san[sl - 1] == '#')) san[--sl] = 0;
            if (!strcmp(san, clean)) { found = legal.moves[i]; match = 1; break; }
        }
        if (!match) break;

        char um[3] = {0}, tm[3] = {0}; square_name(found.from, um); square_name(found.to, tm);
        char uci[8]; snprintf(uci, sizeof(uci), "%s%s%s", um, tm,
            found.promotion == QUEEN ? "q" : found.promotion == ROOK ? "r" :
            found.promotion == BISHOP ? "b" : found.promotion == KNIGHT ? "n" : "");
        int wn = snprintf(out + used, outsz - used, "%s%s", used ? " " : "", uci);
        if (wn > 0) used += (size_t)wn;
        apply_move(&board, found);
        plies++;
    }
    return plies;
}

/* ply_cap: for PGN-sourced openings, how many plies to take from each game
 * (0 = a sane default). Ignored for EPD (the position itself is the whole
 * opening). Returns 0 on success, -1 on error (message already printed). */
static int load_openings_file(const char* path, int ply_cap) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "error: cannot open openings file %s\n", path); return -1; }

    const char* ext = strrchr(path, '.');
    int is_pgn = ext && !strcasecmp(ext, ".pgn");
    if (is_pgn && ply_cap <= 0) ply_cap = 20;

    g_openings = malloc(sizeof(OpeningEntry) * MAX_OPENINGS);
    if (!g_openings) { fclose(f); fprintf(stderr, "error: out of memory loading openings\n"); return -1; }
    g_n_openings = 0;

    if (is_pgn) {
        char line[4096];
        char* movetext = malloc(1 << 20); size_t mtlen = 0;
        if (!movetext) { fclose(f); free(g_openings); g_openings = NULL; return -1; }
        movetext[0] = 0;
        int in_game = 0;
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '[') {
                if (in_game && mtlen > 0 && g_n_openings < MAX_OPENINGS) {
                    char uci[1024];
                    int plies = pgn_extract_uci(movetext, uci, sizeof(uci), ply_cap);
                    if (plies > 0) {
                        g_openings[g_n_openings].is_fen = 0;
                        strncpy(g_openings[g_n_openings].moves_uci, uci, sizeof(g_openings[g_n_openings].moves_uci) - 1);
                        g_n_openings++;
                    }
                }
                if (mtlen > 0) { movetext[0] = 0; mtlen = 0; }
                in_game = 1;
                continue;
            }
            size_t ll = strlen(line);
            if (mtlen + ll < (1u << 20) - 1) { strcpy(movetext + mtlen, line); mtlen += ll; }
        }
        if (in_game && mtlen > 0 && g_n_openings < MAX_OPENINGS) {
            char uci[1024];
            int plies = pgn_extract_uci(movetext, uci, sizeof(uci), ply_cap);
            if (plies > 0) {
                g_openings[g_n_openings].is_fen = 0;
                strncpy(g_openings[g_n_openings].moves_uci, uci, sizeof(g_openings[g_n_openings].moves_uci) - 1);
                g_n_openings++;
            }
        }
        free(movetext);
    } else {
        char line[512];
        while (fgets(line, sizeof(line), f) && g_n_openings < MAX_OPENINGS) {
            char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
            char* p = line; while (isspace((unsigned char)*p)) p++;
            if (!*p || *p == '#') continue;
            Board probe;
            if (!fen_to_board(p, &probe)) continue;   /* skip malformed/blank lines */
            g_openings[g_n_openings].is_fen = 1;
            strncpy(g_openings[g_n_openings].fen, p, sizeof(g_openings[g_n_openings].fen) - 1);
            g_openings[g_n_openings].fen[sizeof(g_openings[g_n_openings].fen) - 1] = 0;
            g_n_openings++;
        }
    }
    fclose(f);
    if (g_n_openings == 0) {
        fprintf(stderr, "error: no usable openings found in %s\n", path);
        free(g_openings); g_openings = NULL;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * --config FILE: engines/options/TC out of a text file instead of a long
 * command line. Each non-comment, non-blank line is shorthand for a CLI
 * token pair, translated and spliced in ahead of the real argv -- so the
 * single existing parse loop in main() handles both, and an explicit CLI
 * flag placed after --config still wins (last assignment to a field wins).
 * Supported lines:
 *   engine PATH[;name=NAME][;opt=K=V,K=V]   (repeatable, one engine each)
 *   quiet
 *   rounds/movetime/depth/nodes/max-moves/random-plies/seed/openings/
 *   tournament/output/sprt VALUE
 * ---------------------------------------------------------------------- */
static char** build_argv_with_config(int argc, char** argv, int* out_argc) {
    const char* config_path = NULL;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--config") && i + 1 < argc) { config_path = argv[i + 1]; break; }
    if (!config_path) { *out_argc = argc; return argv; }

    FILE* f = fopen(config_path, "r");
    if (!f) { fprintf(stderr, "error: cannot open config file %s\n", config_path); *out_argc = -1; return NULL; }

    int cap = 2048;
    char** toks = malloc(sizeof(char*) * cap);
    int nt = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (nt >= cap - 2) break;
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        char* p = line; while (isspace((unsigned char)*p)) p++;
        if (!*p || *p == '#') continue;
        char key[64] = {0}, val[900] = {0};
        sscanf(p, "%63s %899[^\n]", key, val);
        size_t vl = strlen(val); while (vl > 0 && isspace((unsigned char)val[vl - 1])) val[--vl] = 0;

        if (!strcmp(key, "engine")) { toks[nt++] = strdup("-e"); toks[nt++] = strdup(val); }
        else if (!strcmp(key, "quiet")) { toks[nt++] = strdup("-q"); }
        else if (!strcmp(key, "rounds") || !strcmp(key, "movetime") || !strcmp(key, "depth") ||
                 !strcmp(key, "nodes") || !strcmp(key, "max-moves") || !strcmp(key, "random-plies") ||
                 !strcmp(key, "seed") || !strcmp(key, "openings") || !strcmp(key, "tournament") ||
                 !strcmp(key, "output") || !strcmp(key, "sprt") || !strcmp(key, "concurrency")) {
            char flag[72]; snprintf(flag, sizeof(flag), "--%s", key);
            toks[nt++] = strdup(flag); toks[nt++] = strdup(val);
        } else {
            fprintf(stderr, "warning: unknown config key '%s' in %s (ignored)\n", key, config_path);
        }
    }
    fclose(f);

    int new_argc = 1 + nt + (argc - 1);
    char** new_argv = malloc(sizeof(char*) * (new_argc + 1));
    int k = 0;
    new_argv[k++] = argv[0];
    for (int i = 0; i < nt; i++) new_argv[k++] = toks[i];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config")) { i++; continue; }   /* drop --config FILE itself */
        new_argv[k++] = argv[i];
    }
    free(toks);
    *out_argc = k;
    return new_argv;
}

/* ------------------------------------------------------------------------
 * CLI: -eN / -nN / --eN-options indexed flags ("-e3", "-n3", "--e3-options")
 * ---------------------------------------------------------------------- */
/* Matches `arg` against "<prefix><digits><suffix>" (e.g. prefix="-e",
 * suffix="" matches "-e3"; prefix="--e", suffix="-options" matches
 * "--e3-options") and, on match, writes the 0-based engine index (digits-1)
 * to *idx. False on no match, leaving *idx untouched -- callers chain these
 * checks per flag type in the main parse loop. */
static bool parse_indexed_flag(const char* arg, const char* prefix, const char* suffix, int* idx) {
    size_t plen = strlen(prefix), slen = strlen(suffix), alen = strlen(arg);
    if (alen <= plen + slen) return false;
    if (strncmp(arg, prefix, plen) != 0) return false;
    if (slen > 0 && strcmp(arg + alen - slen, suffix) != 0) return false;
    size_t numlen = alen - plen - slen;
    if (numlen == 0) return false;
    for (size_t i = 0; i < numlen; i++)
        if (!isdigit((unsigned char)arg[plen + i])) return false;
    *idx = atoi(arg + plen) - 1;
    return true;
}

/* ------------------------------------------------------------------------
 * Tournament: one pairing (engine A vs engine B) is exactly the old
 * single-match loop, generalized. Round-robin/gauntlet are just different
 * schedules of pairings over the same primitive.
 * ---------------------------------------------------------------------- */
typedef struct {
    int score_a, score_b, draws, games_played;
    long penta[5];
} PairingResult;

/* Shared by the sequential path (run_pairing) and the concurrent worker pool
 * (run_tournament_concurrent) so both print the exact same final summary for
 * a pairing, once all its games are settled. */
static void print_pairing_final_report(const Args* a, const char* nameA, const char* nameB,
                                        int score_a, int score_b, int draws, int games_played,
                                        const long penta[5], double llr, const char* sprt_verdict) {
    double llr_lo = log(a->sprt_beta / (1.0 - a->sprt_alpha));
    double llr_hi = log((1.0 - a->sprt_beta) / a->sprt_alpha);

    printf("\n----- Result: %s vs %s -----\n", nameA, nameB);
    printf("%s: %d   %s: %d   Draws: %d   (of %d games)\n",
           nameA, score_a, nameB, score_b, draws, games_played);

    long npairs = penta[0] + penta[1] + penta[2] + penta[3] + penta[4];
    if (npairs > 0) {
        double mean = 0;
        for (int i = 0; i < 5; i++) mean += (i * 0.25) * penta[i];
        mean /= npairs;
        double var = 0;
        for (int i = 0; i < 5; i++) { double d = i * 0.25 - mean; var += d * d * penta[i]; }
        var /= npairs;
        double se = sqrt(var / npairs);
        printf("pentanomial [LL Ld dd/WL dW WW]: %ld %ld %ld %ld %ld  (%ld pairs)\n",
               penta[0], penta[1], penta[2], penta[3], penta[4], npairs);
        if (mean > 0.0 && mean < 1.0) {
            double elo = -400.0 * log10(1.0 / mean - 1.0);
            double dElo = 400.0 / (log(10.0) * mean * (1.0 - mean));
            printf("%s vs %s: %+.1f +/- %.1f Elo (95%%, pentanomial)\n",
                   nameA, nameB, elo, 1.96 * se * dElo);
        } else printf("%s vs %s: one side scored every pair\n", nameA, nameB);
    } else if (score_a + score_b + draws > 0) {
        double s1 = (score_a + 0.5 * draws) / (double)(score_a + score_b + draws);
        if (s1 > 0.0 && s1 < 1.0)
            printf("%s vs %s: %+.1f Elo (approx, unpaired)\n", nameA, nameB, -400.0 * log10(1.0/s1 - 1.0));
    }

    if (score_a + score_b > 0) {
        double los = 0.5 * (1.0 + erf((score_a - score_b) / sqrt(2.0 * (score_a + score_b))));
        printf("LOS (prob %s is stronger): %.1f%%\n", nameA, 100.0 * los);
    }
    if (a->sprt)
        printf("SPRT (H0 elo<=%.0f, H1 elo>=%.0f, a=b=%.2f): LLR %.2f in [%.2f, %.2f] -> %s\n",
               a->sprt_elo0, a->sprt_elo1, a->sprt_alpha, llr, llr_lo, llr_hi,
               sprt_verdict ? sprt_verdict : "inconclusive (games exhausted)");
}

/* Sequential driver for one pairing (a->rounds games between eA and eB,
 * alternating colors, paired two-at-a-time so each opening is played from
 * both sides). This is the -c 1 (default) path -- see run_tournament_
 * concurrent for the -c N>1 worker-pool equivalent of this same loop.
 * `multi` is n_pairings > 1 (round-robin/gauntlet with several pairings);
 * it just gates printing a "--- Pairing: A vs B ---" header. `game_no` is a
 * running counter across the whole tournament, used for the PGN [Round]
 * tag so round numbers stay unique across pairings. */
static PairingResult run_pairing(const Args* a, const EngineSpec* eA, const EngineSpec* eB,
                                  unsigned obase, const char* datestr, int* game_no, int multi) {
    PairingResult res; memset(&res, 0, sizeof(res));
    char opening[8192] = "";
    const char* use_fen = NULL;
    int pair_pts = 0;
    double llr = 0.0;
    double llr_lo = log(a->sprt_beta / (1.0 - a->sprt_alpha));
    double llr_hi = log((1.0 - a->sprt_beta) / a->sprt_alpha);
    const char* sprt_verdict = NULL;

    if (multi) printf("\n--- Pairing: %s vs %s ---\n", eA->name, eB->name);

    for (int g = 1; g <= a->rounds; g++) {
        int pair_idx = (g - 1) / 2;
        int first_of_pair = ((g - 1) % 2 == 0);
        if (first_of_pair) {
            pair_pts = 0;
            if (g_n_openings > 0) {
                OpeningEntry* oe = &g_openings[pair_idx % g_n_openings];
                if (oe->is_fen) { use_fen = oe->fen; opening[0] = 0; }
                else { use_fen = NULL; strncpy(opening, oe->moves_uci, sizeof(opening) - 1); opening[sizeof(opening) - 1] = 0; }
            } else if (a->random_plies > 0) {
                use_fen = NULL;
                unsigned st = obase * 2654435761u + (unsigned)pair_idx * 40503u + 12345u;
                gen_opening(a->random_plies, &st, opening, sizeof(opening));
            } else { use_fen = NULL; opening[0] = 0; }
        }

        int a_white = (g % 2 == 1);
        const char* wpath = a_white ? eA->path : eB->path;
        const char* bpath = a_white ? eB->path : eA->path;
        const char* wopt  = a_white ? eA->options : eB->options;
        const char* bopt  = a_white ? eB->options : eA->options;
        const char* wname = a_white ? eA->name : eB->name;
        const char* bname = a_white ? eB->name : eA->name;

        printf("Game %d/%d: %s (white) vs %s (black) ... ", g, a->rounds, wname, bname);
        fflush(stdout);

        GameRecord rec;
        (*game_no)++;
        play_game(a, wpath, bpath, wopt, bopt, &rec, *game_no, opening, use_fen, "");
        res.games_played = g;

        int a_won = 0, b_won = 0, drew = 0;
        if (!strcmp(rec.result, "1-0")) { if (a_white) a_won = 1; else b_won = 1; }
        else if (!strcmp(rec.result, "0-1")) { if (a_white) b_won = 1; else a_won = 1; }
        else drew = 1;
        res.score_a += a_won; res.score_b += b_won; res.draws += drew;
        pair_pts += a_won ? 2 : (drew ? 1 : 0);

        if (!a->quiet) {
            printf("-> %s  [%s]\n", rec.result, rec.termination);
            printf("    Score: %s %d - %d %s  (draws: %d)\n", eA->name, res.score_a, res.score_b, eB->name, res.draws);
        } else printf("\n");

        /* Append to PGN */
        FILE* f = fopen(a->output, "a");
        if (f) {
            fprintf(f, "[Event \"fidhcheall match\"]\n[Site \"fidhcheall\"]\n[Date \"%s\"]\n[Round \"%d\"]\n"
                       "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n[Termination \"%s\"]\n",
                    datestr, *game_no, wname, bname, rec.result, rec.termination);
            if (use_fen && use_fen[0]) fprintf(f, "[FEN \"%s\"]\n[SetUp \"1\"]\n", use_fen);
            fprintf(f, "\n");
            int col = 0;
            for (const char* p = rec.san_moves; *p; p++) {
                fputc(*p, f); col++;
                if (*p == ' ' && col > 78) { fputc('\n', f); col = 0; }
            }
            fprintf(f, "%s\n\n", rec.result);
            fclose(f);
        }

        /* End of a pair: record pentanomial outcome and test SPRT. */
        if (!first_of_pair) {
            res.penta[pair_pts]++;
            if (a->sprt) {
                llr = sprt_llr(res.penta, a->sprt_elo0, a->sprt_elo1);
                if (!a->quiet)
                    printf("    SPRT: LLR %.2f  in [%.2f, %.2f]\n", llr, llr_lo, llr_hi);
                if (llr >= llr_hi) sprt_verdict = "H1 accepted -- engine1 is stronger";
                else if (llr <= llr_lo) sprt_verdict = "H0 accepted -- engine1 is NOT stronger";
                if (sprt_verdict) break;          /* early stop */
            }
        }
    }

    print_pairing_final_report(a, eA->name, eB->name, res.score_a, res.score_b, res.draws,
                                res.games_played, res.penta, llr, sprt_verdict);
    return res;
}

/* ------------------------------------------------------------------------
 * -c N: concurrent worker pool, only used when N > 1 (default N=1 takes the
 * run_pairing path above, completely unchanged). A flat job queue of
 * (pairing, round) lets games from different pairings -- and different
 * rounds of the *same* pairing -- run on separate threads at once, which is
 * what actually speeds up the common two-engine-match case, not just a big
 * round-robin.
 *
 * Locking: each PairingCtx has its own mutex guarding that pairing's
 * score/pentanomial/SPRT state (workers on different pairings never
 * contend). One Scheduler-wide io_lock separately guards stdout and the
 * shared PGN file, since play_game's own state is 100% stack-local per call
 * (board, engines, pipes) and never touches anything shared -- the only
 * cross-game data are those two. The two locks are never held at once by
 * the same thread, so there's no lock-ordering/deadlock question to reason
 * about. Openings are re-derived per game from (obase, pair_idx) rather than
 * cached -- gen_opening/g_openings selection are already pure functions of
 * their inputs, so any worker recomputes the identical opening independently
 * with no shared state or race. */
#define PAIR_PENDING (-1)

typedef struct {
    pthread_mutex_t lock;
    const EngineSpec* eA;
    const EngineSpec* eB;
    int pairing_idx;
    /* guarded by lock */
    int score_a, score_b, draws, games_played;
    long penta[5];
    int* pair_first_pts;     /* size a->rounds; PAIR_PENDING until a pair's first game lands */
    int sprt_stopped;
    double llr;
    const char* sprt_verdict;
} PairingCtx;

typedef struct { int pairing_idx; int round; } GameJob;

typedef struct {
    GameJob* jobs;
    int n_jobs;
    int next_job;
    pthread_mutex_t queue_lock;

    PairingCtx* pairings;
    const Args* a;
    unsigned obase;
    const char* datestr;
    double llr_lo, llr_hi;
    int show_pairing_prefix;   /* n_pairings > 1: tag each game line with its pairing */

    int game_no;               /* guarded by io_lock, continuous across the whole run */
    pthread_mutex_t io_lock;   /* guards stdout + the shared PGN file + game_no */
} Scheduler;

/* Runs one (pairing_idx, round) job end to end: pick that round's opening
 * (deterministic from pair_idx, see the Scheduler comment above), call
 * play_game, then update the pairing's shared stats and emit the game's
 * progress line + PGN entry. Called by worker_main; never called directly
 * with the same job twice (the queue hands each job to exactly one worker). */
static void run_scheduled_game(Scheduler* sch, int pairing_idx, int round) {
    const Args* a = sch->a;
    PairingCtx* ctx = &sch->pairings[pairing_idx];
    int pair_idx = (round - 1) / 2;

    char opening[8192] = "";
    const char* use_fen = NULL;
    if (g_n_openings > 0) {
        OpeningEntry* oe = &g_openings[pair_idx % g_n_openings];
        if (oe->is_fen) use_fen = oe->fen;
        else { strncpy(opening, oe->moves_uci, sizeof(opening) - 1); opening[sizeof(opening) - 1] = 0; }
    } else if (a->random_plies > 0) {
        unsigned st = sch->obase * 2654435761u + (unsigned)pair_idx * 40503u + 12345u;
        gen_opening(a->random_plies, &st, opening, sizeof(opening));
    }

    int a_white = (round % 2 == 1);
    const char* wpath = a_white ? ctx->eA->path : ctx->eB->path;
    const char* bpath = a_white ? ctx->eB->path : ctx->eA->path;
    const char* wopt  = a_white ? ctx->eA->options : ctx->eB->options;
    const char* bopt  = a_white ? ctx->eB->options : ctx->eA->options;
    const char* wname = a_white ? ctx->eA->name : ctx->eB->name;
    const char* bname = a_white ? ctx->eB->name : ctx->eA->name;

    char tag[32]; snprintf(tag, sizeof(tag), "_p%d_r%d", pairing_idx, round);

    GameRecord rec;
    play_game(a, wpath, bpath, wopt, bopt, &rec, round, opening, use_fen, tag);

    int a_won = 0, b_won = 0, drew = 0;
    if (!strcmp(rec.result, "1-0")) { if (a_white) a_won = 1; else b_won = 1; }
    else if (!strcmp(rec.result, "0-1")) { if (a_white) b_won = 1; else a_won = 1; }
    else drew = 1;

    int snap_score_a, snap_score_b, snap_draws;
    double snap_llr = 0.0;
    int pair_completed = 0;

    /* Under -c N the two games of a pair can finish on different worker
       threads in either order (unlike the sequential path, which always
       does first-of-pair then second-of-pair back to back). Whichever one
       lands first just records its own points and waits; whichever lands
       second combines the two into a pentanomial bucket and, if SPRT is on,
       runs the LLR check -- so a pair's stats are only ever touched once,
       by exactly one of its two games. */
    pthread_mutex_lock(&ctx->lock);
    ctx->score_a += a_won; ctx->score_b += b_won; ctx->draws += drew;
    ctx->games_played++;
    int pts = a_won ? 2 : (drew ? 1 : 0);
    if (ctx->pair_first_pts[pair_idx] == PAIR_PENDING) {
        ctx->pair_first_pts[pair_idx] = pts;
    } else {
        int pair_pts = ctx->pair_first_pts[pair_idx] + pts;
        ctx->penta[pair_pts]++;
        pair_completed = 1;
        if (a->sprt && !ctx->sprt_stopped) {
            ctx->llr = sprt_llr(ctx->penta, a->sprt_elo0, a->sprt_elo1);
            if (ctx->llr >= sch->llr_hi) { ctx->sprt_stopped = 1; ctx->sprt_verdict = "H1 accepted -- engine1 is stronger"; }
            else if (ctx->llr <= sch->llr_lo) { ctx->sprt_stopped = 1; ctx->sprt_verdict = "H0 accepted -- engine1 is NOT stronger"; }
        }
    }
    snap_score_a = ctx->score_a; snap_score_b = ctx->score_b; snap_draws = ctx->draws;
    snap_llr = ctx->llr;
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_lock(&sch->io_lock);
    int gn = ++sch->game_no;
    if (!a->quiet) {
        if (sch->show_pairing_prefix) printf("[%s vs %s] ", ctx->eA->name, ctx->eB->name);
        printf("Game %d: %s (white) vs %s (black) -> %s  [%s]\n", gn, wname, bname, rec.result, rec.termination);
        printf("    Score: %s %d - %d %s  (draws: %d)\n", ctx->eA->name, snap_score_a, snap_score_b, ctx->eB->name, snap_draws);
        if (pair_completed && a->sprt)
            printf("    SPRT: LLR %.2f  in [%.2f, %.2f]\n", snap_llr, sch->llr_lo, sch->llr_hi);
    } else {
        if (sch->show_pairing_prefix) printf("[%s vs %s] ", ctx->eA->name, ctx->eB->name);
        printf("Game %d: -> %s\n", gn, rec.result);
    }

    FILE* f = fopen(a->output, "a");
    if (f) {
        fprintf(f, "[Event \"fidhcheall match\"]\n[Site \"fidhcheall\"]\n[Date \"%s\"]\n[Round \"%d\"]\n"
                   "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n[Termination \"%s\"]\n",
                sch->datestr, gn, wname, bname, rec.result, rec.termination);
        if (use_fen && use_fen[0]) fprintf(f, "[FEN \"%s\"]\n[SetUp \"1\"]\n", use_fen);
        fprintf(f, "\n");
        int col = 0;
        for (const char* p = rec.san_moves; *p; p++) {
            fputc(*p, f); col++;
            if (*p == ' ' && col > 78) { fputc('\n', f); col = 0; }
        }
        fprintf(f, "%s\n\n", rec.result);
        fclose(f);
    }
    pthread_mutex_unlock(&sch->io_lock);
}

/* One of the a->concurrency worker threads: pull the next (pairing, round)
 * job off the shared FIFO queue and run it, until the queue is empty. If
 * SPRT has already decided the job's pairing by the time this thread claims
 * it, the job is skipped rather than run -- but a job already claimed by
 * another worker at that moment still finishes normally, so under
 * concurrency SPRT can overshoot its exact stopping point by up to
 * (concurrency - 1) extra games per pairing. That's a deliberate tradeoff,
 * not a bug: cancelling an in-flight game would mean killing engine
 * subprocesses mid-search. */
static void* worker_main(void* argp) {
    Scheduler* sch = (Scheduler*)argp;
    for (;;) {
        pthread_mutex_lock(&sch->queue_lock);
        if (sch->next_job >= sch->n_jobs) { pthread_mutex_unlock(&sch->queue_lock); break; }
        GameJob job = sch->jobs[sch->next_job++];
        pthread_mutex_unlock(&sch->queue_lock);

        PairingCtx* ctx = &sch->pairings[job.pairing_idx];
        pthread_mutex_lock(&ctx->lock);
        int stopped = ctx->sprt_stopped;
        pthread_mutex_unlock(&ctx->lock);
        if (stopped) continue;   /* SPRT already decided this pairing; skip remaining (unstarted) jobs */

        run_scheduled_game(sch, job.pairing_idx, job.round);
    }
    return NULL;
}

/* Drives every pairing through a pool of a->concurrency worker threads, then
 * folds each pairing's final PairingCtx into the same score/drawm/penta_total
 * accumulators the sequential path fills, so main()'s cross-table/gauntlet
 * summary code downstream doesn't need to know which path ran. */
static void run_tournament_concurrent(const Args* a, const Pairing* pairing_list, int n_pairings,
                                       unsigned obase, const char* datestr, int* game_no,
                                       int score[MAX_ENGINES][MAX_ENGINES], int drawm[MAX_ENGINES][MAX_ENGINES],
                                       long penta_total[5]) {
    printf("\nEXPERIMENTAL: -c %d runs %d games concurrently (up to %d engine processes at once).\n"
           "If your engines are multi-threaded, cap their thread count (e.g. an engine\n"
           "option like Threads=1) or you'll oversubscribe your CPU cores.\n\n",
           a->concurrency, a->concurrency, 2 * a->concurrency);

    PairingCtx* ctxs = calloc((size_t)n_pairings, sizeof(PairingCtx));
    int total_jobs = 0;
    for (int p = 0; p < n_pairings; p++) {
        ctxs[p].eA = &a->engines[pairing_list[p].i];
        ctxs[p].eB = &a->engines[pairing_list[p].j];
        ctxs[p].pairing_idx = p;
        ctxs[p].pair_first_pts = malloc(sizeof(int) * (size_t)(a->rounds > 0 ? a->rounds : 1));
        for (int k = 0; k < a->rounds; k++) ctxs[p].pair_first_pts[k] = PAIR_PENDING;
        pthread_mutex_init(&ctxs[p].lock, NULL);
        total_jobs += a->rounds;
        if (n_pairings > 1) printf("--- Pairing: %s vs %s ---\n", ctxs[p].eA->name, ctxs[p].eB->name);
    }

    GameJob* jobs = malloc(sizeof(GameJob) * (size_t)total_jobs);
    int nj = 0;
    for (int p = 0; p < n_pairings; p++)
        for (int r = 1; r <= a->rounds; r++) jobs[nj++] = (GameJob){p, r};

    Scheduler sch;
    sch.jobs = jobs; sch.n_jobs = nj; sch.next_job = 0;
    pthread_mutex_init(&sch.queue_lock, NULL);
    sch.pairings = ctxs; sch.a = a; sch.obase = obase; sch.datestr = datestr;
    sch.llr_lo = log(a->sprt_beta / (1.0 - a->sprt_alpha));
    sch.llr_hi = log((1.0 - a->sprt_beta) / a->sprt_alpha);
    sch.show_pairing_prefix = (n_pairings > 1);
    sch.game_no = *game_no;
    pthread_mutex_init(&sch.io_lock, NULL);

    int nworkers = a->concurrency;
    pthread_t* threads = malloc(sizeof(pthread_t) * (size_t)nworkers);
    for (int t = 0; t < nworkers; t++) pthread_create(&threads[t], NULL, worker_main, &sch);
    for (int t = 0; t < nworkers; t++) pthread_join(threads[t], NULL);
    free(threads);

    *game_no = sch.game_no;

    for (int p = 0; p < n_pairings; p++) {
        PairingCtx* ctx = &ctxs[p];
        print_pairing_final_report(a, ctx->eA->name, ctx->eB->name, ctx->score_a, ctx->score_b,
                                    ctx->draws, ctx->games_played, ctx->penta, ctx->llr, ctx->sprt_verdict);
        int i = pairing_list[p].i, j = pairing_list[p].j;
        score[i][j] += ctx->score_a; score[j][i] += ctx->score_b;
        drawm[i][j] += ctx->draws;   drawm[j][i] += ctx->draws;
        for (int k = 0; k < 5; k++) penta_total[k] += ctx->penta[k];
        pthread_mutex_destroy(&ctx->lock);
        free(ctx->pair_first_pts);
    }
    free(ctxs);
    free(jobs);
    pthread_mutex_destroy(&sch.queue_lock);
    pthread_mutex_destroy(&sch.io_lock);
}

/* score[i][j] = games engine i won against engine j; drawm[i][j] = draws between them. */
static void print_roundrobin_table(const Args* a, int score[MAX_ENGINES][MAX_ENGINES],
                                    int drawm[MAX_ENGINES][MAX_ENGINES]) {
    int n = a->n_engines;
    printf("\n===== ROUND-ROBIN CROSS-TABLE =====\n");
    printf("%-16s", "");
    for (int j = 0; j < n; j++) printf(" %-10.10s", a->engines[j].name);
    printf("  %8s\n", "Points");
    double pts[MAX_ENGINES]; int wins[MAX_ENGINES];
    for (int i = 0; i < n; i++) {
        printf("%-16.16s", a->engines[i].name);
        double p = 0; int w = 0;
        for (int j = 0; j < n; j++) {
            if (i == j) { printf(" %-10s", "--"); continue; }
            int wij = score[i][j], lij = score[j][i], dij = drawm[i][j];
            p += wij + 0.5 * dij; w += wij;
            char cell[16]; snprintf(cell, sizeof(cell), "%d-%d-%d", wij, dij, lij);
            printf(" %-10s", cell);
        }
        printf("  %8.1f\n", p);
        pts[i] = p; wins[i] = w;
    }

    int order[MAX_ENGINES];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (pts[order[j]] > pts[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }

    printf("\nStandings:\n");
    for (int r = 0; r < n; r++) {
        int e = order[r];
        printf("  %d. %-16.16s %5.1f pts  (%d wins)\n", r + 1, a->engines[e].name, pts[e], wins[e]);
    }
}

/* Challenger-vs-field summary for gauntlet mode: engine 0's record against
 * each opponent individually, a total, and a combined Elo estimate pooling
 * every opponent's pentanomial counts together (an approximation -- treats
 * the field as one aggregate opponent -- but a reasonable one since every
 * opponent shares the same opening set). */
static void print_gauntlet_summary(const Args* a, int score[MAX_ENGINES][MAX_ENGINES],
                                    int drawm[MAX_ENGINES][MAX_ENGINES], const long penta_total[5]) {
    int n = a->n_engines;
    printf("\n===== GAUNTLET RESULT: %s vs field =====\n", a->engines[0].name);
    double total_pts = 0; int total_w = 0, total_d = 0, total_l = 0;
    for (int j = 1; j < n; j++) {
        int w = score[0][j], l = score[j][0], d = drawm[0][j];
        total_w += w; total_d += d; total_l += l; total_pts += w + 0.5 * d;
        printf("  vs %-16.16s  %d - %d - %d  (%.1f pts)\n", a->engines[j].name, w, d, l, w + 0.5 * d);
    }
    printf("  ----------------------------------------\n");
    printf("  Total: %d - %d - %d  (%.1f / %d pts)\n", total_w, total_d, total_l, total_pts, total_w + total_d + total_l);

    long npairs = 0; for (int k = 0; k < 5; k++) npairs += penta_total[k];
    if (npairs > 0) {
        double mean = 0;
        for (int k = 0; k < 5; k++) mean += (k * 0.25) * penta_total[k];
        mean /= npairs;
        if (mean > 0.0 && mean < 1.0) {
            double var = 0;
            for (int k = 0; k < 5; k++) { double d = k * 0.25 - mean; var += d * d * penta_total[k]; }
            var /= npairs;
            double se = sqrt(var / npairs);
            double elo = -400.0 * log10(1.0 / mean - 1.0);
            double dElo = 400.0 / (log(10.0) * mean * (1.0 - mean));
            printf("  %s vs field: %+.1f +/- %.1f Elo (95%%, pentanomial, combined across opponents)\n",
                   a->engines[0].name, elo, 1.96 * se * dElo);
        }
    }
}

/* Overall flow: expand --config (if any) into equivalent CLI tokens, parse
 * the combined argument list into Args, validate and default the engine
 * list, load an --openings file (if any), build the pairing schedule
 * (round-robin = every pair, gauntlet = engine 0 vs the rest), run it either
 * sequentially (run_pairing per pairing) or through the -c N worker pool
 * (run_tournament_concurrent), then print the cross-table/gauntlet summary
 * for tournaments of more than two engines. */
int main(int argc, char** argv) {
    /* If an engine process crashes or exits mid-game, writes to its (now
       closed) stdin pipe would raise SIGPIPE, which by default terminates
       this process too -- turning "one engine crashed" into "fidhcheall itself
       silently dies with no diagnostic". Ignore it; write() will instead
       just return -1/EPIPE, which is already handled gracefully. */
    signal(SIGPIPE, SIG_IGN);

    int cfg_argc; char** cfg_argv = build_argv_with_config(argc, argv, &cfg_argc);
    if (cfg_argc < 0) return 1;
    argc = cfg_argc; argv = cfg_argv;

    Args a; memset(&a, 0, sizeof(a));
    a.rounds = 10; a.n_engines = 0; a.tournament = TOURN_AUTO;
    a.output = "results.pgn"; a.movetime_ms = 1000; a.depth = 0; a.nodes = 0;
    a.max_moves = 300; a.random_plies = 0; a.seed = -1;
    a.quiet = 0; a.go_timeout_extra_ms = 5000;
    a.sprt = 0; a.sprt_elo0 = 0; a.sprt_elo1 = 5; a.sprt_alpha = 0.05; a.sprt_beta = 0.05;
    a.concurrency = 1;

    int next_e_slot = 0;   /* next slot the repeatable "-e" form appends to */

    /* ARG_VAL consumes and returns the next argv token as this flag's value
       (e.g. for "-r 20", it advances past "20"); "" if the flag was the last
       token. The parse_indexed_flag checks below (for -eN/-nN/--eN-options)
       don't need to be tried in any particular order -- their prefixes are
       mutually exclusive by construction (e.g. "--e3-options" can never
       match the plain "-e" prefix), so at most one ever matches a given
       argv[i]. */
    for (int i = 1; i < argc; i++) {
        #define ARG_VAL (++i < argc ? argv[i] : "")
        int idx;
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(argv[i], "--version")) { printf("fidhcheall %s\n", FIDCH_VERSION); return 0; }
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--rounds")) a.rounds = atoi(ARG_VAL);
        else if (parse_indexed_flag(argv[i], "--e", "-options", &idx)) {
            const char* v = ARG_VAL;
            if (idx >= 0 && idx < MAX_ENGINES) strncpy(a.engines[idx].options, v, sizeof(a.engines[idx].options) - 1);
        }
        else if (parse_indexed_flag(argv[i], "-e", "", &idx)) {
            const char* v = ARG_VAL;
            if (idx >= 0 && idx < MAX_ENGINES) {
                strncpy(a.engines[idx].path, v, sizeof(a.engines[idx].path) - 1);
                if (idx + 1 > a.n_engines) a.n_engines = idx + 1;
                if (idx + 1 > next_e_slot) next_e_slot = idx + 1;
            }
        }
        else if (parse_indexed_flag(argv[i], "-n", "", &idx)) {
            const char* v = ARG_VAL;
            if (idx >= 0 && idx < MAX_ENGINES) strncpy(a.engines[idx].name, v, sizeof(a.engines[idx].name) - 1);
        }
        else if (!strcmp(argv[i], "-e")) {
            /* repeatable form: -e PATH[;name=NAME][;opt=K=V,K=V] */
            const char* v = ARG_VAL;
            char buf[2048]; strncpy(buf, v, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
            int slot = next_e_slot++;
            if (slot < MAX_ENGINES) {
                char* semi = strchr(buf, ';');
                if (semi) *semi = 0;
                strncpy(a.engines[slot].path, buf, sizeof(a.engines[slot].path) - 1);
                char* rest = semi ? semi + 1 : NULL;
                while (rest) {
                    char* nsemi = strchr(rest, ';');
                    if (nsemi) *nsemi = 0;
                    if (!strncmp(rest, "name=", 5))
                        strncpy(a.engines[slot].name, rest + 5, sizeof(a.engines[slot].name) - 1);
                    else if (!strncmp(rest, "opt=", 4))
                        strncpy(a.engines[slot].options, rest + 4, sizeof(a.engines[slot].options) - 1);
                    rest = nsemi ? nsemi + 1 : NULL;
                }
                if (slot + 1 > a.n_engines) a.n_engines = slot + 1;
            }
        }
        else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) a.output = ARG_VAL;
        else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--movetime")) a.movetime_ms = atoi(ARG_VAL);
        else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--depth")) a.depth = atoi(ARG_VAL);
        else if (!strcmp(argv[i], "--nodes")) a.nodes = atoi(ARG_VAL);
        else if (!strcmp(argv[i], "--max-moves")) a.max_moves = atoi(ARG_VAL);
        else if (!strcmp(argv[i], "--random-plies")) a.random_plies = atoi(ARG_VAL);
        else if (!strcmp(argv[i], "--seed")) a.seed = atol(ARG_VAL);
        else if (!strcmp(argv[i], "--openings")) a.openings_file = ARG_VAL;
        else if (!strcmp(argv[i], "--tournament")) {
            const char* v = ARG_VAL;
            if (!strcmp(v, "roundrobin")) a.tournament = TOURN_ROUNDROBIN;
            else if (!strcmp(v, "gauntlet")) a.tournament = TOURN_GAUNTLET;
            else { fprintf(stderr, "error: --tournament must be roundrobin or gauntlet\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet")) a.quiet = 1;
        else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--concurrency")) {
            int c = atoi(ARG_VAL);
            a.concurrency = c > 0 ? c : 1;
        }
        else if (!strcmp(argv[i], "--sprt")) {
            /* --sprt elo0,elo1[,alpha,beta]  (defaults alpha=beta=0.05) */
            const char* v = ARG_VAL;
            double e0 = 0, e1 = 5, al = 0.05, be = 0.05;
            sscanf(v, "%lf,%lf,%lf,%lf", &e0, &e1, &al, &be);
            a.sprt = 1; a.sprt_elo0 = e0; a.sprt_elo1 = e1; a.sprt_alpha = al; a.sprt_beta = be;
        }
        else { fprintf(stderr, "unknown argument: %s\n", argv[i]); usage(argv[0]); return 1; }
        #undef ARG_VAL
    }

    if (a.n_engines < 2) { fprintf(stderr, "error: need at least two engines (-e1/-e2, or -e PATH twice)\n\n"); usage(argv[0]); return 1; }
    for (int i = 0; i < a.n_engines; i++) {
        if (!a.engines[i].path[0]) { fprintf(stderr, "error: engine %d not specified (missing -e%d)\n", i + 1, i + 1); return 1; }
        if (!a.engines[i].name[0]) strncpy(a.engines[i].name, basename_of(a.engines[i].path), sizeof(a.engines[i].name) - 1);
        if (access(a.engines[i].path, X_OK) != 0) { fprintf(stderr, "error: engine%d not found or not executable: %s\n", i + 1, a.engines[i].path); return 1; }
    }

    TournamentMode eff;
    if (a.n_engines <= 2) eff = TOURN_ROUNDROBIN;               /* plain match: one pairing */
    else eff = (a.tournament == TOURN_AUTO) ? TOURN_ROUNDROBIN : a.tournament;

    srand(a.seed >= 0 ? (unsigned)a.seed : (unsigned)time(NULL));

    init_tables();
    init_zobrist();

    if (a.openings_file) {
        if (load_openings_file(a.openings_file, a.random_plies) != 0) return 1;
    }

    printf("fidhcheall (fidch) - chess engine tester\n");
    if (a.n_engines == 2) {
        printf("  %s  vs  %s\n", a.engines[0].name, a.engines[1].name);
    } else {
        printf("  %d engines (%s): ", a.n_engines, eff == TOURN_GAUNTLET ? "gauntlet" : "round-robin");
        for (int i = 0; i < a.n_engines; i++) printf("%s%s", i ? ", " : "", a.engines[i].name);
        printf("\n");
    }
    if (a.nodes > 0) printf("  rounds/pairing: %d    limit: %d nodes/move\n", a.rounds, a.nodes);
    else if (a.depth > 0) printf("  rounds/pairing: %d    limit: depth %d\n", a.rounds, a.depth);
    else printf("  rounds/pairing: %d    limit: %d ms/move\n", a.rounds, a.movetime_ms);
    if (g_n_openings > 0) printf("  openings: %s (%d positions)    output: %s\n\n", a.openings_file, g_n_openings, a.output);
    else printf("  opening plies: %d    output: %s\n\n", a.random_plies, a.output);

    FILE* out = fopen(a.output, "w");
    if (!out) { fprintf(stderr, "error: cannot open output file %s\n", a.output); return 1; }
    fclose(out);

    /* obase seeds the (seed, pair_idx) -> opening formula used by
       gen_opening/g_openings selection throughout the run. Because it's a
       single value shared by every pairing, pair index k always gets the
       *same* opening no matter which pairing is playing it -- that's what
       makes the comparison across pairings fair (see the file header
       comment on run_scheduled_game / run_pairing's opening-selection code). */
    unsigned obase = (a.seed >= 0) ? (unsigned)a.seed : (unsigned)time(NULL);
    time_t today = time(NULL); struct tm* tm_now = localtime(&today);
    char datestr[16]; strftime(datestr, sizeof(datestr), "%Y.%m.%d", tm_now);

    /* Gauntlet: engine 0 (the challenger) against each of the rest.
       Round-robin (the default for >2 engines): every unordered pair. */
    Pairing pairings[MAX_ENGINES * (MAX_ENGINES - 1) / 2];
    int n_pairings = 0;
    if (eff == TOURN_GAUNTLET) {
        for (int j = 1; j < a.n_engines; j++) pairings[n_pairings++] = (Pairing){0, j};
    } else {
        for (int i = 0; i < a.n_engines; i++)
            for (int j = i + 1; j < a.n_engines; j++) pairings[n_pairings++] = (Pairing){i, j};
    }

    /* score[i][j] = games engine i won against engine j; drawm[i][j] = draws
       between them (symmetric: drawm[i][j] == drawm[j][i]) -- filled either
       by accumulating each run_pairing() call's result (sequential) or, in
       one shot, by run_tournament_concurrent from its per-pairing contexts. */
    int score[MAX_ENGINES][MAX_ENGINES] = {{0}};
    int drawm[MAX_ENGINES][MAX_ENGINES] = {{0}};
    long penta_total[5] = {0, 0, 0, 0, 0};
    int game_no = 0;

    if (a.concurrency <= 1) {
        for (int p = 0; p < n_pairings; p++) {
            int i = pairings[p].i, j = pairings[p].j;
            PairingResult pr = run_pairing(&a, &a.engines[i], &a.engines[j], obase, datestr, &game_no, n_pairings > 1);
            score[i][j] += pr.score_a; score[j][i] += pr.score_b;
            drawm[i][j] += pr.draws;   drawm[j][i] += pr.draws;
            for (int k = 0; k < 5; k++) penta_total[k] += pr.penta[k];
        }
    } else {
        run_tournament_concurrent(&a, pairings, n_pairings, obase, datestr, &game_no, score, drawm, penta_total);
    }

    if (a.n_engines > 2) {
        if (eff == TOURN_GAUNTLET) print_gauntlet_summary(&a, score, drawm, penta_total);
        else print_roundrobin_table(&a, score, drawm);
    }

    printf("\nPGN saved to: %s\n", a.output);
    return 0;
}