/* ============================================================
 * cmd_mining.c
 * pool_view, mine_solo, mine_pool, mine_cloud, difficulty_status,
 * set_difficulty and set_param.
 *
 * All three mining modes confirm the same pending pool through
 * confirm_mine_batch(): a fixed batch of up to DEFAULT_BATCH_SIZE
 * requests per block, highest priority first. They differ only in
 * who is credited with the block reward.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "input.h"
#include "registry.h"

/* ---- pool_view ---- */

static const char *tier_label(const PendingEntry *e, char *buf, size_t n) {
    if (e->is_overdue_related) return "1 overdue";
    if (e->reservation_date > 0) {
        char d[12];
        cli_format_date(e->reservation_date, d);
        snprintf(buf, n, "2 resv %s", d);
        return buf;
    }
    return "3 normal";
}

static int cmd_pool_view(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    PendingPool *p = &ctx->state->pool;
    pending_pool_sort(p);

    printf("Pending pool (%zu request(s)), in mining priority order: overdue-related, then reservation date, then arrival.\n", p->count);
    if (!p->count) { printf("  (empty)\n"); return 0; }
    printf("  %-3s %-12s %-10s %-10s %-12s %-14s %-11s\n", "#", "REQUEST", "ACTION", "MEMBER", "ITEM", "PRIORITY", "STATUS");
    for (size_t i = 0; i < p->count; i++) {
        const PendingEntry *e = &p->entries[i];
        char tier[24], item[40], action[16];
        const char *tl = tier_label(e, tier, sizeof(tier));
        if (e->kind == ENTRY_LENDING) {
            snprintf(action, sizeof(action), "%s", lending_action_to_string(e->record.action));
            snprintf(item, sizeof(item), "%s", e->record.copy_id[0] ? e->record.copy_id : e->record.book_id);
        } else {
            snprintf(action, sizeof(action), "TRANSFER");
            snprintf(item, sizeof(item), "%s%d", e->tx.is_fine ? "fine " : "", e->tx.amount);
        }
        const char *status = e->status == POOL_PENDING ? "PENDING" : e->status == POOL_CONFIRMED ? "CONFIRMED" : "SUSPICIOUS";
        printf("  %-3zu %-12s %-10s %-10s %-12s %-14s %-11s\n", i + 1, e->request_id, action, pending_entry_owner(e),
               item, tl, status);
        if (e->status == POOL_SUSPICIOUS) printf("      reason: %s\n", e->fraud_reason);
    }
    return 0;
}

/* ---- shared mining output ---- */

static void target_prefix(int difficulty, char *out, size_t n) {
    size_t k = (size_t)difficulty < n - 1 ? (size_t)difficulty : n - 1;
    memset(out, '0', k);
    out[k] = '\0';
}

static int mine_block(CliContext *ctx, const char *sealer, MineResult *m) {
    AppState *s = ctx->state;
    char prefix[8];
    target_prefix(s->meta.difficulty, prefix, sizeof(prefix));
    printf("Mining block #%lu at difficulty %d (hash must start with %s)...\n", s->chain.length, s->meta.difficulty, prefix);

    int rc = confirm_mine_batch(s, sealer, DEFAULT_BATCH_SIZE, m);
    for (int i = 0; i < m->rejected_count; i++)
        printf("  Rejected %s: %s\n", m->rejected[i].request_id, m->rejected[i].reason);
    if (rc != 0) {
        printf("Nothing mined: %s\n", m->err);
        return -1;
    }

    printf("Block #%lu mined in %lu hash attempt(s).\n  Hash:      %s\n  Sealed by: %s\n"
           "  Contents:  %d lending record(s), %d token transfer(s); %d request(s) confirmed\n",
           m->block_index, m->attempts, m->block_hash, m->sealed_by, m->records, m->transfers, m->confirmed);
    if (m->return_bonus) printf("  Return rewards credited to members: %d token(s)\n", m->return_bonus);
    if (m->offered) printf("  %d returned cop%s offered to the reservation queue\n", m->offered, m->offered == 1 ? "y" : "ies");
    printf("  Mining reward: %d (block reward) + %d (fees) = %d token(s)\n", m->block_reward, m->fees, m->total_reward);
    if (m->deferred) printf("  %d request(s) remain pending in the pool for the next block (batch limit or not yet eligible).\n", m->deferred);
    return 0;
}

static void after_block(CliContext *ctx) {
    cli_print_ledger_state(ctx->state);
    size_t suspicious = 0;
    for (size_t i = 0; i < ctx->state->pool.count; i++)
        if (ctx->state->pool.entries[i].status == POOL_SUSPICIOUS) suspicious++;
    printf("Pending pool now holds %zu request(s)%s.\n", ctx->state->pool.count,
           suspicious ? " (suspicious ones await fraud_review)" : "");
}

/* ---- solo ---- */

static int cmd_mine_solo(CliContext *ctx, int argc, char **argv) {
    const char *miner = argc >= 2 ? argv[1] : "MINER1";
    if (!input_valid_id(miner, SEALER_ID_LEN)) { printf("Error: invalid miner id '%s'.\n", miner); return 1; }

    MineResult m;
    if (mine_block(ctx, miner, &m) != 0) return 1;
    confirm_pay_reward(ctx->state, miner, m.total_reward);
    printf("Solo mining: %s receives the full reward of %d token(s).\n", miner, m.total_reward);
    after_block(ctx);
    return 0;
}

/* ---- pool ---- */

static int cmd_mine_pool(CliContext *ctx, int argc, char **argv) {
    long n = 4;
    if (argc >= 2 && input_parse_int(argv[1], 2, MAX_POOL_MINERS, &n) != 0) {
        printf("Error: the pool needs between 2 and %d miners.\n", MAX_POOL_MINERS);
        return 1;
    }

    MineResult m;
    if (mine_block(ctx, "POOL", &m) != 0) return 1;

    PoolMiner miners[MAX_POOL_MINERS];
    int fee = 0;
    unsigned long rounds = mining_pool_distribute(m.attempts, (int)n, MINING_POOL_FEE_PERCENT, m.total_reward, miners, &fee);

    printf("Pool mining with %ld miner(s): %lu round(s) of work covered the %lu attempts.\n", n, rounds, m.attempts);
    printf("  %-14s %-10s %-10s %-9s %s\n", "MINER", "HASH RATE", "ATTEMPTS", "SHARE %", "REWARD");
    int paid = 0;
    for (int i = 0; i < n; i++) {
        printf("  %-14s %-10lu %-10lu %-9.2f %d\n", miners[i].miner_id, miners[i].hash_rate, miners[i].attempts,
               miners[i].share_pct, miners[i].reward);
        confirm_pay_reward(ctx->state, miners[i].miner_id, miners[i].reward);
        paid += miners[i].reward;
    }
    confirm_pay_reward(ctx->state, "POOL", fee);
    printf("  Total reward %d: pool fee (%d%%) %d to POOL, %d shared by attempts contributed.\n",
           m.total_reward, MINING_POOL_FEE_PERCENT, fee, paid);
    after_block(ctx);
    return 0;
}

/* ---- cloud ---- */

static int cmd_mine_cloud(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "mine_cloud <rounds 1-5> [renter_id]")) return 1;
    AppState *s = ctx->state;
    long rounds;
    if (input_parse_int(argv[1], 1, 5, &rounds) != 0) { printf("Error: rent between 1 and 5 rounds.\n"); return 1; }
    const char *renter = argc >= 3 ? argv[2] : "CLOUDUSER";
    if (!input_valid_id(renter, MEMBER_ID_LEN)) { printf("Error: invalid renter id '%s'.\n", renter); return 1; }

    char sealer[SEALER_ID_LEN];
    snprintf(sealer, sizeof(sealer), "CLOUD-%.17s", renter);

    int fee = s->meta.rental_fee;
    long gross = 0, fees = 0;
    printf("Cloud mining: %s rents %ld round(s) at %d token(s) per round (block reward %d).\n", renter, rounds, fee, s->meta.block_reward);
    printf("  %-6s %-7s %-10s %-8s %-6s %-12s %-12s %s\n", "ROUND", "BLOCK", "ATTEMPTS", "REWARD", "FEE", "CUM REWARD", "CUM FEES", "NET");

    int done = 0;
    for (long r = 1; r <= rounds; r++) {
        MineResult m;
        if (confirm_mine_batch(s, sealer, DEFAULT_BATCH_SIZE, &m) != 0) {
            for (int i = 0; i < m.rejected_count; i++) printf("  Rejected %s: %s\n", m.rejected[i].request_id, m.rejected[i].reason);
            printf("  Round %ld: nothing left to mine (%s) -- the remaining rounds are not rented.\n", r, m.err);
            break;
        }
        gross += m.total_reward;
        fees += fee;
        done++;
        int payout = m.total_reward - fee;
        if (payout > 0) confirm_pay_reward(s, renter, payout);
        printf("  %-6ld #%-6lu %-10lu %-8d %-6d %-12ld %-12ld %ld\n", r, m.block_index, m.attempts, m.total_reward, fee,
               gross, fees, gross - fees);
        if (fees > gross)
            printf("  WARNING: cumulative rental fees (%ld) exceed cumulative rewards (%ld) after round %ld.\n", fees, gross, r);
        cli_print_ledger_state(s);
    }
    if (!done) return 1;

    printf("Cloud mining summary (%d round(s) rented):\n  Gross earnings: %ld token(s)\n  Total rental fees: %ld token(s)\n"
           "  Net profit: %ld token(s)%s\n", done, gross, fees, gross - fees, gross - fees < 0 ? "  (a loss)" : "");
    printf("  %s was credited the positive net of each round; fees are paid to the cloud provider outside the ledger.\n", renter);
    return 0;
}

/* ---- difficulty and parameters ---- */

static int cmd_difficulty_status(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    AppState *s = ctx->state;
    char prefix[8];
    target_prefix(s->meta.difficulty, prefix, sizeof(prefix));
    unsigned long expected = 1;
    for (int i = 0; i < s->meta.difficulty; i++) expected *= 16;

    printf("Proof-of-work difficulty: %d (range %d-%d)\n  Target: block hash must start with %s\n"
           "  Expected work: about %lu hash attempts per block (16^%d)\n",
           s->meta.difficulty, MIN_DIFFICULTY, MAX_DIFFICULTY, prefix, expected, s->meta.difficulty);

    int counts[MAX_DIFFICULTY + 1] = {0};
    for (const Block *b = s->chain.head; b; b = b->next)
        if (b->index > 0 && b->difficulty >= 0 && b->difficulty <= MAX_DIFFICULTY) counts[b->difficulty]++;
    printf("  Blocks on the chain by the difficulty they were mined at:");
    for (int d = MIN_DIFFICULTY; d <= MAX_DIFFICULTY; d++) printf("  d%d: %d", d, counts[d]);
    printf("\n  Block reward %d, pool fee %d%%, cloud rental fee %d per round, batch size %d request(s) per block.\n",
           s->meta.block_reward, MINING_POOL_FEE_PERCENT, s->meta.rental_fee, DEFAULT_BATCH_SIZE);
    return 0;
}

static int cmd_set_difficulty(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "set_difficulty <1-4>")) return 1;
    long d;
    if (input_parse_int(argv[1], MIN_DIFFICULTY, MAX_DIFFICULTY, &d) != 0) {
        printf("Error: difficulty must be between %d and %d.\n", MIN_DIFFICULTY, MAX_DIFFICULTY);
        return 1;
    }
    ctx->state->meta.difficulty = (int)d;
    printf("Difficulty set to %ld for blocks mined from now on; earlier blocks keep the difficulty they were mined at.\n", d);
    return 0;
}

static int cmd_set_param(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 2, "set_param <block_reward|rental_fee|loan_days|renewal_days|max_renewals|max_concurrent|fine_per_day> <value>")) return 1;
    ChainMeta *m = &ctx->state->meta;
    static const struct { const char *name; long min, max; } LIMITS[] = {
        { "block_reward", 1, 1000 }, { "rental_fee", 0, 1000 }, { "loan_days", 1, 365 }, { "renewal_days", 1, 90 },
        { "max_renewals", 0, 10 }, { "max_concurrent", 1, 20 }, { "fine_per_day", 0, 100 }
    };
    int *targets[] = { &m->block_reward, &m->rental_fee, &m->loan_days, &m->renewal_days, &m->max_renewals,
                       &m->max_concurrent, &m->fine_per_day };
    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); i++) {
        if (strcmp(argv[1], LIMITS[i].name) != 0) continue;
        long v;
        if (input_parse_int(argv[2], LIMITS[i].min, LIMITS[i].max, &v) != 0) {
            printf("Error: %s must be a whole number from %ld to %ld.\n", LIMITS[i].name, LIMITS[i].min, LIMITS[i].max);
            return 1;
        }
        *targets[i] = (int)v;
        printf("%s is now %ld.\n", LIMITS[i].name, v);
        return 0;
    }
    printf("Error: unknown parameter '%s'.\n", argv[1]);
    return 1;
}

const Command mining_commands[] = {
    { "pool_view", cmd_pool_view, "", "pending and suspicious requests in priority order", 0 },
    { "mine_solo", cmd_mine_solo, "[miner_id]", "one miner confirms a batch and takes the full reward", 1 },
    { "mine_pool", cmd_mine_pool, "[miners 2-16]", "a pool confirms a batch; reward shared by attempts, 2% fee", 1 },
    { "mine_cloud", cmd_mine_cloud, "<rounds 1-5> [renter_id]", "rent mining power; fees, rewards and net profit", 1 },
    { "difficulty_status", cmd_difficulty_status, "", "current difficulty, target and expected work", 0 },
    { "set_difficulty", cmd_set_difficulty, "<1-4>", "set proof-of-work difficulty for new blocks", 1 },
    { "set_param", cmd_set_param, "<name> <value>", "adjust reward, fees, loan period and limits", 1 },
    { NULL, NULL, NULL, NULL, 0 }
};
