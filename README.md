# Bank Management System in C++

A console-based banking application built for object-oriented programming practice. The program models savings and current accounts, authenticates customers with PINs, applies banking rules, and persists account data to local files.

## Features

- Create savings and current accounts
- Deposit, withdraw, transfer, and check balances
- Print account statements
- PIN authentication with account lockout after repeated failures
- Staff account unlocking
- Persistent storage and backup using file handling
- C++ demonstrations of classes, inheritance, polymorphism, templates, operator overloading, STL, and custom exceptions
- UML and flow diagrams in [`diagrams/`](diagrams/)

## Tech stack

- C++17
- Standard Library (`iostream`, `fstream`, `vector`, `map`, algorithms, exceptions)
- Local text-file persistence

## Build and run

```bash
g++ -std=c++17 -Wall -Wextra -pedantic bank.cpp -o bank
./bank
```

On Windows, run `bank.exe` after compiling.

The program creates `accounts.txt` and `accounts_backup.txt` in the directory where it runs. These runtime files are ignored by Git so personal or test account data is not published.

## Project structure

```text
.
├── bank.cpp             # Complete application source
├── diagrams/            # Architecture, account, flow, and exception diagrams
└── README.md
```

## Notes

This repository contains the clean source-and-diagrams version of the academic project. Supporting report and presentation files can be added after their remaining placeholders are finalized.

