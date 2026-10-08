# Blockchain-Based Library Book Lending Tracker

A command-line lending ledger for a university library (African Leadership University),
written in C11 with OpenSSL. Every borrow, return, renewal, reservation and overdue flag is
an ECDSA-signed record. Records wait in a pending pool, are re-validated by a miner,
and are sealed into a proof-of-work block that is linked to the previous block by its hash
and carries a Merkle root over its records. A token system rewards on-time returns and
collects fines, and runs under either a UTXO model or an account model.

## Requirements

| Item | Notes |
|------|-------|
| Operating system | Linux (developed on Ubuntu 22.04) |
| Compiler | `gcc` with C11 support; `make` |
| OpenSSL | development headers and libraries, version 3.0 or later (`libssl-dev`) |
| Optional | `python3` (only for scenario 8 of the scenario suite) |

```bash
sudo apt update && sudo apt install -y build-essential libssl-dev
```

No other third-party libraries are used. The SHA-256 hashing, ECDSA signing and key generation
use OpenSSL's EVP interface; the Merkle tree, block structure, proof of work, ledgers,
fraud checks and persistence are implemented in this repository.

## Build

```bash
make            # builds ./library_tracker (compiled with -Wall -Wextra -std=c11, no warnings)
make test       # builds and runs every unit test in tests/
make clean      # removes build products
```

## Run

```bash
make run                           # seeds data/ from sample_data/ on first use, then starts the CLI
./library_tracker data             # start on an existing data directory
./library_tracker data --difficulty 3 --mode account
./library_tracker --help
make reset                         # delete data/ (chain, keys, state) and re-seed the sample registries
```

The CLI reads one command per line from standard input, so it works interactively and from a
script (`./library_tracker data < commands.txt`). Use `"double quotes"` around names containing
spaces. `help` lists every command and `help <command>` shows its arguments.

### Data directory

| File | Contents |
|------|----------|
| `books.txt`, `members.txt`, `copies.txt` | Registries, one pipe-separated record per line. Required at start-up |
| `chain.dat` | The blockchain (binary). A corrupt file is never overwritten |
| `pending_pool.dat` | Requests waiting to be mined, including suspicious ones (binary) |
| `utxo_ledger.txt`, `account_ledger.txt` | The two token ledgers |
| `fines.txt`, `reservations.txt` | Settled fines and reservation queues |
| `meta.txt` | Difficulty, token model, system clock offset and parameters |
| `keys/` | One ECDSA P-256 key pair per member and for the librarian (`*_private.pem` is mode 600) |

`data/`, `keys/`, `*.pem` and `*.dat` are listed in `.gitignore`; private keys must never be committed.

## Switching between the UTXO and account models

Two token models are implemented. Choose one when starting, or change it during a session:

```bash
./library_tracker data --mode utxo        # default
./library_tracker data --mode account
```
```
library> set_mode account
library> set_mode utxo
```

The two models keep separate ledgers. Switching does not convert balances: a member's tokens
in one model are not visible in the other. Start a session in the model you intend to demonstrate.
`token_transfer`, `token_balance` and the mining payouts use the active model;
`account_transfer`, `account_nonce` and `transaction_history` apply to the account ledger.
A transfer queued under one model waits in the pool until that model is active.

## Setting the difficulty

Difficulty is the number of leading zero hex digits required in a block hash (1 to 4; the
default is 2, about 256 attempts per block; difficulty 4 is about 65 000).

```bash
./library_tracker data --difficulty 3     # at start-up
```
```
library> set_difficulty 4                 # for blocks mined from now on
library> difficulty_status                # target, expected work, blocks mined at each difficulty
```

Each block stores the difficulty it was mined at, so changing it never invalidates earlier blocks.

## Testing mining

Mining confirms up to five pending requests per block, highest priority first (overdue-related,
then earliest reservation, then arrival order). Nothing changes in the inventory or the ledgers until
a block is mined.

```
library> borrow_book B001 M001
library> borrow_book B002 M002
library> pool_view                  # requests in mining order
library> mine_solo MINER1           # one miner takes the whole reward
library> mine_pool 4                # a pool of 4 miners: random hash rates, shares by attempts, 2% pool fee
library> mine_cloud 3 M002          # rent 1-5 rounds; shows reward, fee, cumulative figures and net profit
library> chain_view                 # list blocks, or chain_view 1 for one block with its records
library> validate_chain             # re-verify hashes, links, proof of work, Merkle roots and signatures
library> tamper_demo block          # corrupt data in memory, show the detection, then revert
```

`set_param` adjusts `block_reward` (default 50), `rental_fee` (20), `loan_days` (14),
`renewal_days` (7), `max_renewals` (2), `max_concurrent` (3) and `fine_per_day` (1).
`advance_time <days>` moves the system clock forward so overdue loans can be demonstrated.

## Commands

| Group | Commands |
|-------|----------|
| Membership | `register_member`, `view_member`, `member_nonce` |
| Books and copies | `register_book`, `view_book`, `add_copy`, `view_copy`, `mark_lost` |
| Lending | `borrow_book`, `return_book`, `renew_book`, `reserve_book`, `reservation_status` |
| Fines and overdue | `check_overdue`, `view_fines`, `settle_fine` |
| Tokens and ledgers | `token_transfer`, `token_balance`, `utxo_view`, `account_balance`, `account_transfer`, `account_nonce`, `transaction_history`, `set_mode` |
| Pool and mining | `pool_view`, `mine_solo`, `mine_pool`, `mine_cloud`, `difficulty_status`, `set_difficulty`, `set_param` |
| Blockchain | `chain_view`, `validate_chain`, `chain_save`, `chain_load`, `tamper_demo`, `advance_time` |
| Fraud and audit | `fraud_review`, `approve_suspicious`, `reject_suspicious`, `lending_history`, `member_history`, `book_history` |

Mutating commands save to disk automatically; `chain_save` and `chain_load` are also available
explicitly. The program saves again on `exit`.

## Behaviour worth knowing

- **Identity and signatures.** A member's public key is their on-chain identity. Each request is
  signed with the member's private key; overdue flags are signed by the librarian key. Signatures are
  checked again when a block is mined and whenever the chain is validated.
- **Nonces.** A member's nonce is the number of their records already on the chain plus their requests
  waiting in the pool, so queued requests carry consecutive nonces and are mined in order.
  `member_nonce` and `account_nonce` show the values.
- **Fraud screening.** A request is marked SUSPICIOUS, and is never mined, if the member already holds
  the maximum number of concurrent borrows, re-borrows within 24 hours of a return, reuses a request id,
  or returns a copy in a condition more than one step worse than it left. A librarian decides with
  `approve_suspicious` or `reject_suspicious`.
- **Fines.** A late return records the fine (days late times `fine_per_day`) in the chain. `settle_fine`
  queues a token transfer to the `LIBRARY` account, which is mined with overdue priority. The member
  must hold enough tokens; the program does not create them.
- **Start-up validation.** The chain is validated when the program starts. If it fails, the session is
  locked: read-only commands work, mutating commands are refused until `validate_chain` or `chain_load`
  succeeds. A `chain.dat` that cannot be parsed locks the session and is never overwritten.
- **Cloud mining accounting.** Rental fees are paid to the provider outside the ledger. The renter is
  credited the reward minus the fee for each round when that is positive, and a warning is printed when
  cumulative fees exceed cumulative rewards.

## Tests

```bash
make test                     # unit tests, one executable per module (tests/test_*.c)
tests/run_scenarios.sh        # 8 end-to-end scenarios: expected vs actual output
tests/run_scenarios.sh 03     # one scenario
```

| Scenario | Demonstrates |
|----------|--------------|
| 01 `lifecycle_utxo` | borrow, mine, on-time return, return reward, UTXO set |
| 02 `late_fine_overdue` | overdue flag with priority, late return, fine assessed then settled |
| 03 `fraud_and_screening` | input rejection, concurrent-borrow and duplicate-id flags, approve and reject |
| 04 `tamper_detection` | record, block and hash-link tampering detected, then reverted |
| 05 `account_mode_nonce` | account balances, wrong-nonce rejection, insufficient balance, transaction history |
| 06 `mining_modes` | pool mining, cloud mining, changing the difficulty |
| 07 `reservation_queue` | reservation queue, copy held for the front member |
| 08 `restart_and_lock` | state survives restart; an altered `chain.dat` locks the session |

Each scenario compares the program's output with the lines in its `.expect` file, in order. The full
actual output of the last run is kept in `scenarios/actual/`.

## Repository layout

```
include/  src/        modules: crypto_utils, merkle, record, blockchain, chain_validate, copy, registry,
                      reservation, pending_pool, fraud, fines, ledger_utxo, ledger_account, mining,
                      lending, confirm, persistence, input
src/cli.c, src/cmd_*.c, src/main.c     command-line front end
tests/                unit tests and run_scenarios.sh
scenarios/            scenario inputs, expected output and last actual output
sample_data/          starter books, members and copies
docs/                 technical report and system design diagram
```

## Limitations

- Private keys are stored unencrypted in `data/keys/` (permissions 600). This is acceptable for a coursework
  demonstration and not for production use.
- It is a single-node simulation: there is no network, peer consensus or fork resolution.
- The UTXO and account ledgers are independent and are not migrated into one another.
- Capacity is fixed at compile time (for example 200 books, 200 members, 1000 copies, 500 pending requests).
