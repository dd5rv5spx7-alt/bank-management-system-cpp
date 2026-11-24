/*
 * ============================================================
 *  BANK MANAGEMENT SYSTEM
 *  Course : CSE202 - Object Oriented Programming using C++
 *  SRM University - AP, Andhra Pradesh
 *
 *  A console-based banking application that simulates deposits,
 *  withdrawals, transfers and balance enquiries, secured with
 *  PIN authentication. All data is stored persistently using
 *  file handling (accounts.txt).
 *
 *  Only concepts from the CSE202 syllabus are used:
 *   Unit 1 : data types, loops, arrays, strings, pointers,
 *            functions, reference arguments, storage classes,
 *            dynamic memory (new / delete)
 *   Unit 2 : classes & objects, encapsulation, access
 *            specifiers, constructors / destructors, copy
 *            constructor, static members, inline functions,
 *            friend functions, this pointer
 *   Unit 3 : function overloading, operator overloading
 *   Unit 4 : inheritance, abstract class, virtual functions,
 *            runtime polymorphism, pointers to base class
 *   Unit 5 : function & class templates, exceptions (custom,
 *            multiple catch, re-throw), STL (vector, map,
 *            algorithms, iterators), file handling
 * ============================================================
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <cstdlib>
using namespace std;

// ------------------------------------------------------------
//  Global constants (bank rules)
// ------------------------------------------------------------
const double SAVINGS_MIN_BALANCE    = 500.0;     // must stay in a savings account
const double SAVINGS_WITHDRAW_LIMIT = 20000.0;   // per withdrawal
const double CURRENT_OVERDRAFT      = 5000.0;    // current account may go this far negative
const double MAX_AMOUNT             = 1000000.0; // max amount in one transaction
const int    MAX_PIN_ATTEMPTS       = 3;
const int    STAFF_CODE             = 9999;      // used by staff to unlock accounts
const string DATA_FILE              = "accounts.txt";
const string BACKUP_FILE            = "accounts_backup.txt";

// ------------------------------------------------------------
//  Small helper functions (string conversion)
// ------------------------------------------------------------
string money(double value)
{
    ostringstream out;
    out << fixed << setprecision(2) << value;
    return out.str();
}

string intToStr(int value)
{
    ostringstream out;
    out << value;
    return out.str();
}

// ------------------------------------------------------------
//  Function template : returns the larger of two values
// ------------------------------------------------------------
template <class T>
T maxOf(T a, T b)
{
    return (a > b) ? a : b;
}

// ------------------------------------------------------------
//  Class template : checks that a value lies in a range
// ------------------------------------------------------------
template <class T>
class Validator
{
private:
    T low;
    T high;
public:
    Validator(T lo, T hi) : low(lo), high(hi) {}
    bool inRange(T value) const { return value >= low && value <= high; }
    T getLow() const  { return low; }
    T getHigh() const { return high; }
};

// ============================================================
//  EXCEPTION CLASSES (custom exceptions, built using inheritance)
// ============================================================
class BankException
{
protected:
    string message;
public:
    BankException(const string& msg) : message(msg) {}
    virtual ~BankException() {}
    virtual string what() const { return message; }
};

class InsufficientFunds : public BankException
{
public:
    InsufficientFunds(double available)
        : BankException("Insufficient funds. You can withdraw at most Rs. " + money(available)) {}
};

class InvalidAmount : public BankException
{
public:
    InvalidAmount(const string& why) : BankException(why) {}
};

class InvalidPIN : public BankException
{
private:
    int attemptsLeft;
public:
    InvalidPIN(int left)
        : BankException("Incorrect PIN. Attempts left: " + intToStr(left)), attemptsLeft(left) {}
    int getAttemptsLeft() const { return attemptsLeft; }
};

class AccountLocked : public BankException
{
public:
    AccountLocked(int accNo)
        : BankException("Account " + intToStr(accNo) + " is locked. Please contact bank staff.") {}
};

class AccountNotFound : public BankException
{
public:
    AccountNotFound(int accNo)
        : BankException("Account " + intToStr(accNo) + " does not exist.") {}
};

// Thrown when the input stream ends (e.g. Ctrl+D) so the program can quit cleanly
class InputClosed {};

// ============================================================
//  Safe input helpers (use the templates above)
// ============================================================
template <class T>
T readValue(const string& prompt, const Validator<T>& rule)
{
    T value;
    while (true)
    {
        cout << prompt;
        if (cin >> value)
        {
            cin.ignore(1000, '\n');
            if (rule.inRange(value))
                return value;
            cout << "  Value must be between " << rule.getLow() << " and " << rule.getHigh() << ".\n";
        }
        else
        {
            if (cin.eof())
                throw InputClosed();
            cin.clear();
            cin.ignore(1000, '\n');
            cout << "  Invalid input. Please enter a number.\n";
        }
    }
}

string readText(const string& prompt)
{
    string text;
    while (true)
    {
        cout << prompt;
        if (!getline(cin, text))
            throw InputClosed();
        if (!text.empty())
            return text;
        cout << "  Input cannot be empty.\n";
    }
}

// ============================================================
//  Transaction : one line of an account statement
// ============================================================
class Transaction
{
private:
    char   type;          // D = deposit, W = withdrawal, O = transfer out, I = transfer in
    double amount;
    double balanceAfter;
public:
    Transaction(char t = 'D', double amt = 0.0, double bal = 0.0)
        : type(t), amount(amt), balanceAfter(bal) {}

    char   getType() const         { return type; }
    double getAmount() const       { return amount; }
    double getBalanceAfter() const { return balanceAfter; }
    void   setType(char t)         { type = t; }

    friend ostream& operator<<(ostream& os, const Transaction& t);
};

string txLabel(char type)
{
    switch (type)
    {
        case 'D': return "Deposit";
        case 'W': return "Withdrawal";
        case 'O': return "Transfer Out";
        case 'I': return "Transfer In";
    }
    return "Unknown";
}

// Overloaded << operator (friend function) to print a transaction
ostream& operator<<(ostream& os, const Transaction& t)
{
    os << "  " << left << setw(14) << txLabel(t.type)
       << right << "Rs. " << setw(10) << t.amount
       << "    Balance: Rs. " << setw(10) << t.balanceAfter;
    return os;
}

// ============================================================
//  Account : ABSTRACT base class
// ============================================================
class Account
{
protected:                       // visible to derived classes only (encapsulation)
    int    accNo;
    string holder;
    int    pin;
    double balance;
    int    failedAttempts;
    bool   locked;
    vector<Transaction> history; // STL container

    static int totalAccounts;    // static data members, shared by all objects
    static int nextAccNo;

    void record(char type, double amount)
    {
        history.push_back(Transaction(type, amount, balance));
    }

public:
    // Constructor (with a default argument)
    Account(int no, const string& name, int p, double bal = 0.0)
        : accNo(no), holder(name), pin(p), balance(bal), failedAttempts(0), locked(false)
    {
        totalAccounts++;
    }

    // Copy constructor
    Account(const Account& other)
        : accNo(other.accNo), holder(other.holder), pin(other.pin), balance(other.balance),
          failedAttempts(other.failedAttempts), locked(other.locked), history(other.history)
    {
        totalAccounts++;
    }

    // Virtual destructor, so deleting through a base pointer is safe
    virtual ~Account() { totalAccounts--; }

    // ---- pure virtual functions : make Account an abstract class ----
    virtual void    withdraw(double amount) = 0;
    virtual string  accountType() const = 0;
    virtual string  rules() const = 0;
    virtual Account* clone() const = 0;

    // ---- ordinary member functions ----
    void deposit(double amount)
    {
        if (amount <= 0)
            throw InvalidAmount("Deposit amount must be greater than zero.");
        if (amount > MAX_AMOUNT)
            throw InvalidAmount("Amount exceeds the limit of Rs. " + money(MAX_AMOUNT) + " per transaction.");
        balance += amount;
        record('D', amount);
    }

    // Operator overloading : acc += 500 is the same as acc.deposit(500)
    Account& operator+=(double amount)
    {
        deposit(amount);
        return *this;                       // 'this' pointer
    }

    // Operator overloading : two accounts are equal if their numbers match
    bool operator==(const Account& other) const { return accNo == other.accNo; }

    // Inline functions (defined inside the class body)
    int    getAccNo() const          { return accNo; }
    string getHolder() const         { return holder; }
    double getBalance() const        { return balance; }
    bool   isLocked() const          { return locked; }
    int    getFailedAttempts() const { return failedAttempts; }

    bool checkPin(int entered) const;       // defined below as an explicit inline function

    void changePin(int oldPin, int newPin)
    {
        if (!checkPin(oldPin))
            throw BankException("Old PIN is incorrect. PIN not changed.");
        pin = newPin;
    }

    int registerFailedAttempt()
    {
        failedAttempts++;
        if (failedAttempts >= MAX_PIN_ATTEMPTS)
            locked = true;
        return MAX_PIN_ATTEMPTS - failedAttempts;
    }

    void resetAttempts()                  { failedAttempts = 0; }
    void unlock()                         { locked = false; failedAttempts = 0; }
    void setSecurity(int attempts, bool l) { failedAttempts = attempts; locked = l; }
    void addHistory(const Transaction& t) { history.push_back(t); }
    void restoreFrom(const Account& snapshot)
    {
        balance = snapshot.balance;
        history = snapshot.history;
    }

    // Function overloading : same name, different parameters
    void showHistory() const                 // whole statement
    {
        showHistory(static_cast<int>(history.size()));
    }
    void showHistory(int lastN) const        // only the last N transactions
    {
        if (history.empty())
        {
            cout << "  No transactions yet.\n";
            return;
        }
        int total = static_cast<int>(history.size());
        int start = total - lastN;
        if (start < 0)
            start = 0;
        cout << "\n  ----------- STATEMENT (" << (total - start) << " entries) -----------\n";
        for (int i = start; i < total; i++)
            cout << history[i] << "\n";
    }

    // Writes this account to a file stream (file handling)
    void save(ofstream& out) const
    {
        out << accountType() << "\n"
            << accNo << "\n"
            << holder << "\n"
            << pin << "\n"
            << balance << "\n"
            << failedAttempts << "\n"
            << (locked ? 1 : 0) << "\n"
            << history.size() << "\n";
        for (vector<Transaction>::const_iterator it = history.begin(); it != history.end(); ++it)
            out << it->getType() << " " << it->getAmount() << " " << it->getBalanceAfter() << "\n";
    }

    // Static member functions
    static int  getTotalAccounts()        { return totalAccounts; }
    static int  issueAccNo()              { return nextAccNo++; }
    static void syncNextAccNo(int used)   { if (used >= nextAccNo) nextAccNo = used + 1; }

    // Friend functions : not members, but may use private/protected data
    friend ostream& operator<<(ostream& os, const Account& a);
    friend void transferFunds(Account& from, Account& to, double amount);
};

int Account::totalAccounts = 0;
int Account::nextAccNo = 1001;

// Explicit inline function defined outside the class
inline bool Account::checkPin(int entered) const
{
    return pin == entered;
}

// Overloaded << for printing an account
ostream& operator<<(ostream& os, const Account& a)
{
    os << "\n  ------------------------------------------------\n"
       << "  Account No : " << a.accNo << "\n"
       << "  Holder     : " << a.holder << "\n"
       << "  Type       : " << a.accountType() << "   <- decided at runtime\n"
       << "  Balance    : Rs. " << a.balance << "\n"
       << "  Status     : " << (a.locked ? "LOCKED" : "Active") << "\n"
       << "  Rules      : " << a.rules() << "\n"
       << "  ------------------------------------------------";
    return os;
}

// ============================================================
//  SavingsAccount : derived class (inheritance)
// ============================================================
class SavingsAccount : public Account
{
public:
    // Derived class constructor calls the base class constructor
    SavingsAccount(int no, const string& name, int p, double bal = 0.0)
        : Account(no, name, p, bal) {}

    SavingsAccount(const SavingsAccount& other) : Account(other) {}

    // Overriding the pure virtual functions
    void withdraw(double amount)
    {
        if (amount <= 0)
            throw InvalidAmount("Withdrawal amount must be greater than zero.");
        if (amount > SAVINGS_WITHDRAW_LIMIT)
            throw InvalidAmount("A savings account allows at most Rs. " + money(SAVINGS_WITHDRAW_LIMIT) + " per withdrawal.");
        double available = balance - SAVINGS_MIN_BALANCE;
        if (available < 0)
            available = 0;
        if (amount > available)
            throw InsufficientFunds(available);
        balance -= amount;
        record('W', amount);
    }

    string accountType() const { return "Savings"; }

    string rules() const
    {
        return "Min balance Rs. " + money(SAVINGS_MIN_BALANCE) + ", max Rs. " + money(SAVINGS_WITHDRAW_LIMIT) + " per withdrawal";
    }

    Account* clone() const { return new SavingsAccount(*this); }
};

// ============================================================
//  CurrentAccount : another derived class
// ============================================================
class CurrentAccount : public Account
{
public:
    CurrentAccount(int no, const string& name, int p, double bal = 0.0)
        : Account(no, name, p, bal) {}

    CurrentAccount(const CurrentAccount& other) : Account(other) {}

    void withdraw(double amount)
    {
        if (amount <= 0)
            throw InvalidAmount("Withdrawal amount must be greater than zero.");
        double available = balance + CURRENT_OVERDRAFT;
        if (amount > available)
            throw InsufficientFunds(available);
        balance -= amount;
        record('W', amount);
    }

    string accountType() const { return "Current"; }

    string rules() const
    {
        return "Overdraft up to Rs. " + money(CURRENT_OVERDRAFT) + " allowed";
    }

    Account* clone() const { return new CurrentAccount(*this); }
};

// ------------------------------------------------------------
//  Friend function that works on TWO objects : fund transfer.
//  A snapshot (made with the copy constructor via clone()) lets
//  us roll the sender back if anything goes wrong half-way.
// ------------------------------------------------------------
void transferFunds(Account& from, Account& to, double amount)
{
    if (from == to)
        throw InvalidAmount("Cannot transfer to the same account.");

    Account* snapshot = from.clone();            // dynamic memory
    try
    {
        from.withdraw(amount);                   // virtual call -> Savings/Current rules
        if (to.locked)
            throw AccountLocked(to.accNo);
        to.deposit(amount);
        from.history.back().setType('O');        // relabel as transfer entries
        to.history.back().setType('I');
    }
    catch (BankException&)
    {
        from.restoreFrom(*snapshot);             // roll back
        delete snapshot;
        throw;                                   // re-throw to the caller
    }
    delete snapshot;
}

// ------------------------------------------------------------
//  Function object + comparison function used with STL algorithms
// ------------------------------------------------------------
struct MatchAccNo
{
    int number;
    MatchAccNo(int n) : number(n) {}
    bool operator()(const Account* a) const { return a->getAccNo() == number; }
};

bool compareByBalance(const Account* a, const Account* b)
{
    return a->getBalance() > b->getBalance();   // richest first
}

// ============================================================
//  Bank : owns all the accounts and talks to the file
// ============================================================
class Bank
{
private:
    vector<Account*> accounts;    // pointers to base class -> runtime polymorphism
    string fileName;

    // Copying a Bank would double-delete the accounts, so it is disabled
    Bank(const Bank&);
    Bank& operator=(const Bank&);

    Account* readAccount(ifstream& in)
    {
        string type, line;
        if (!getline(in, type) || type.empty())
            return NULL;

        getline(in, line);  int no = atoi(line.c_str());
        string name;        getline(in, name);
        getline(in, line);  int pinValue = atoi(line.c_str());
        getline(in, line);  double bal = atof(line.c_str());
        getline(in, line);  int attempts = atoi(line.c_str());
        getline(in, line);  bool isLocked = (atoi(line.c_str()) == 1);
        getline(in, line);  int count = atoi(line.c_str());

        Account* acc;
        if (type == "Savings")
            acc = new SavingsAccount(no, name, pinValue, bal);
        else if (type == "Current")
            acc = new CurrentAccount(no, name, pinValue, bal);
        else
            throw BankException("Data file is corrupted (unknown account type).");

        acc->setSecurity(attempts, isLocked);
        for (int i = 0; i < count; i++)
        {
            getline(in, line);
            char t = line[0];
            size_t space = line.find(' ', 2);
            double amt = atof(line.substr(2, space - 2).c_str());
            double after = atof(line.substr(space + 1).c_str());
            acc->addHistory(Transaction(t, amt, after));
        }
        return acc;
    }

public:
    Bank(const string& file) : fileName(file) {}

    ~Bank()                                      // release dynamic memory
    {
        for (vector<Account*>::iterator it = accounts.begin(); it != accounts.end(); ++it)
            delete *it;
    }

    void load()
    {
        ifstream in(fileName.c_str());
        if (!in)
            return;                              // first run : no file yet
        Account* acc;
        while ((acc = readAccount(in)) != NULL)
        {
            accounts.push_back(acc);
            Account::syncNextAccNo(acc->getAccNo());
        }
        in.close();
    }

    void save() const
    {
        ofstream out(fileName.c_str());
        if (!out)
            throw BankException("Unable to write to the data file.");
        out << fixed << setprecision(2);
        for (vector<Account*>::const_iterator it = accounts.begin(); it != accounts.end(); ++it)
            (*it)->save(out);
        out.close();
    }

    Account* createAccount(int type, const string& name, int pin, double opening)
    {
        int no = Account::issueAccNo();
        Account* acc;
        if (type == 1)
            acc = new SavingsAccount(no, name, pin);
        else
            acc = new CurrentAccount(no, name, pin);
        if (opening > 0)
            *acc += opening;                     // overloaded += operator
        accounts.push_back(acc);
        save();
        return acc;
    }

    Account* findAccount(int no)
    {
        vector<Account*>::iterator it = find_if(accounts.begin(), accounts.end(), MatchAccNo(no));
        if (it == accounts.end())
            throw AccountNotFound(no);
        return *it;
    }

    Account* login(int no, int pin)
    {
        static int loginsThisRun = 0;            // static storage class : keeps its value between calls

        Account* acc = findAccount(no);
        if (acc->isLocked())
            throw AccountLocked(no);

        try
        {
            if (!acc->checkPin(pin))
                throw InvalidPIN(MAX_PIN_ATTEMPTS - acc->getFailedAttempts() - 1);
        }
        catch (InvalidPIN&)
        {
            acc->registerFailedAttempt();
            save();                              // remember the failed attempt permanently
            if (acc->isLocked())
                throw AccountLocked(no);
            throw;                               // re-throw the same InvalidPIN
        }

        acc->resetAttempts();
        save();
        loginsThisRun++;
        cout << "\n  Login successful (login #" << loginsThisRun << " in this run).\n";
        return acc;
    }

    void unlock(int no, int code)
    {
        if (code != STAFF_CODE)
            throw BankException("Invalid staff code.");
        Account* acc = findAccount(no);
        if (!acc->isLocked())
            throw BankException("That account is not locked.");
        acc->unlock();
        save();
    }

    void summary() const
    {
        if (accounts.empty())
        {
            cout << "\n  The bank has no accounts yet.\n";
            return;
        }

        vector<Account*> sorted = accounts;                        // copy of the pointers
        sort(sorted.begin(), sorted.end(), compareByBalance);      // STL algorithm

        cout << "\n  ===================== BANK SUMMARY =====================\n";
        cout << "  " << left << setw(8) << "Acc No" << setw(18) << "Holder"
             << setw(10) << "Type" << setw(14) << "Balance" << "Status\n";
        cout << "  --------------------------------------------------------\n";

        map<string, int>    countByType;                           // STL map
        map<string, double> totalByType;
        double highest = sorted[0]->getBalance();

        for (vector<Account*>::const_iterator it = sorted.begin(); it != sorted.end(); ++it)
        {
            Account* a = *it;
            cout << "  " << left << setw(8) << a->getAccNo()
                 << setw(18) << a->getHolder()
                 << setw(10) << a->accountType()                   // virtual call
                 << setw(14) << money(a->getBalance())
                 << (a->isLocked() ? "LOCKED" : "Active") << "\n";
            countByType[a->accountType()]++;
            totalByType[a->accountType()] += a->getBalance();
            highest = maxOf(highest, a->getBalance());             // function template
        }

        cout << "  --------------------------------------------------------\n";
        for (map<string, int>::const_iterator it = countByType.begin(); it != countByType.end(); ++it)
            cout << "  " << left << setw(10) << it->first << ": " << it->second
                 << " account(s), total Rs. " << totalByType[it->first] << "\n";
        cout << "  Highest balance : Rs. " << highest << "\n";
        cout << "  Accounts in memory (static counter) : " << Account::getTotalAccounts() << "\n";
    }

    // Copies one file to another, character by character
    void backup(const string& destination) const
    {
        ifstream in(fileName.c_str());
        if (!in)
            throw BankException("Nothing to back up yet - no data file exists.");
        ofstream out(destination.c_str());
        if (!out)
            throw BankException("Could not create the backup file.");
        char ch;
        while (in.get(ch))
            out.put(ch);
        in.close();
        out.close();
    }
};

// ============================================================
//  Menus (arrays of C-strings)
// ============================================================
const char* MAIN_MENU[] = {
    "Create new account",
    "Login to account",
    "Bank summary (all accounts)",
    "Staff : unlock a locked account",
    "Backup data file",
    "Exit"
};
const int MAIN_MENU_COUNT = 6;

const char* SESSION_MENU[] = {
    "Deposit money",
    "Withdraw money",
    "Balance enquiry",
    "Transfer to another account",
    "Mini statement (last 5)",
    "Full statement",
    "Change PIN",
    "Account details",
    "Logout"
};
const int SESSION_MENU_COUNT = 9;

void showMenu(const string& title, const char* items[], int count)
{
    cout << "\n  ========== " << title << " ==========\n";
    for (int i = 0; i < count; i++)
        cout << "   " << (i + 1) << ". " << items[i] << "\n";
}

// ============================================================
//  Customer session (after a successful login)
// ============================================================
void customerSession(Bank& bank, Account* acc)
{
    bool loggedIn = true;
    cout << "  Welcome, " << acc->getHolder() << "!\n";

    while (loggedIn)
    {
        try
        {
            showMenu("ACCOUNT MENU", SESSION_MENU, SESSION_MENU_COUNT);
            int choice = readValue<int>("  Enter choice: ", Validator<int>(1, SESSION_MENU_COUNT));

            switch (choice)
            {
                case 1:
                {
                    double amt = readValue<double>("  Amount to deposit : Rs. ", Validator<double>(0.01, MAX_AMOUNT));
                    *acc += amt;
                    bank.save();
                    cout << "  Deposited Rs. " << amt << ". New balance: Rs. " << acc->getBalance() << "\n";
                    break;
                }
                case 2:
                {
                    double amt = readValue<double>("  Amount to withdraw: Rs. ", Validator<double>(0.01, MAX_AMOUNT));
                    acc->withdraw(amt);                  // runtime polymorphism
                    bank.save();
                    cout << "  Withdrew Rs. " << amt << ". New balance: Rs. " << acc->getBalance() << "\n";
                    break;
                }
                case 3:
                    cout << "  Current balance: Rs. " << acc->getBalance() << "\n";
                    break;
                case 4:
                {
                    int toNo = readValue<int>("  Destination account number: ", Validator<int>(1, 999999));
                    Account* dest = bank.findAccount(toNo);
                    double amt = readValue<double>("  Amount to transfer: Rs. ", Validator<double>(0.01, MAX_AMOUNT));
                    transferFunds(*acc, *dest, amt);
                    bank.save();
                    cout << "  Transferred Rs. " << amt << " to " << dest->getHolder()
                         << ". New balance: Rs. " << acc->getBalance() << "\n";
                    break;
                }
                case 5:
                    acc->showHistory(5);
                    break;
                case 6:
                    acc->showHistory();
                    break;
                case 7:
                {
                    int oldPin = readValue<int>("  Enter current PIN: ", Validator<int>(0, 99999999));
                    int newPin = readValue<int>("  Enter new 4-digit PIN: ", Validator<int>(1000, 9999));
                    acc->changePin(oldPin, newPin);
                    bank.save();
                    cout << "  PIN changed successfully.\n";
                    break;
                }
                case 8:
                    cout << *acc << "\n";
                    break;
                case 9:
                    loggedIn = false;
                    cout << "  Logged out. Thank you for banking with us.\n";
                    break;
            }
        }
        catch (InsufficientFunds& e)                    // multiple catch blocks
        {
            cout << "\n  [Declined] " << e.what() << "\n";
        }
        catch (InvalidAmount& e)
        {
            cout << "\n  [Invalid amount] " << e.what() << "\n";
        }
        catch (BankException& e)                        // base class catches the rest
        {
            cout << "\n  [Error] " << e.what() << "\n";
        }
    }
}

// ============================================================
//  Create-account dialogue
// ============================================================
void openAccount(Bank& bank)
{
    cout << "\n  ----- OPEN A NEW ACCOUNT -----\n";
    string name = readText("  Full name: ");
    int type = readValue<int>("  Account type (1 = Savings, 2 = Current): ", Validator<int>(1, 2));
    int pin = readValue<int>("  Choose a 4-digit PIN (1000-9999): ", Validator<int>(1000, 9999));

    double minimum = (type == 1) ? SAVINGS_MIN_BALANCE : 0.0;
    double opening = readValue<double>("  Opening deposit (min Rs. " + money(minimum) + "): Rs. ",
                                       Validator<double>(minimum, MAX_AMOUNT));

    Account* acc = bank.createAccount(type, name, pin, opening);
    cout << "\n  Account created successfully! Please note your account number.";
    cout << *acc << "\n";
}

// ============================================================
//  main
// ============================================================
int main()
{
    cout << fixed << setprecision(2);
    cout << "\n  *********************************************\n"
         << "  *        BANK MANAGEMENT SYSTEM  (C++)      *\n"
         << "  *        CSE202 - OOP using C++             *\n"
         << "  *********************************************\n";

    Bank bank(DATA_FILE);
    try
    {
        bank.load();
        cout << "  Data loaded from " << DATA_FILE << " ("
             << Account::getTotalAccounts() << " account(s)).\n";
    }
    catch (BankException& e)
    {
        cout << "  [Error while loading] " << e.what() << "\n";
        return 1;
    }

    bool running = true;
    while (running)
    {
        try
        {
            showMenu("MAIN MENU", MAIN_MENU, MAIN_MENU_COUNT);
            int choice = readValue<int>("  Enter choice: ", Validator<int>(1, MAIN_MENU_COUNT));

            switch (choice)
            {
                case 1:
                    openAccount(bank);
                    break;
                case 2:
                {
                    int no  = readValue<int>("  Account number: ", Validator<int>(1, 999999));
                    int pin = readValue<int>("  PIN: ", Validator<int>(0, 99999999));
                    Account* acc = bank.login(no, pin);
                    customerSession(bank, acc);
                    break;
                }
                case 3:
                    bank.summary();
                    break;
                case 4:
                {
                    int no   = readValue<int>("  Account number to unlock: ", Validator<int>(1, 999999));
                    int code = readValue<int>("  Staff code: ", Validator<int>(0, 99999999));
                    bank.unlock(no, code);
                    cout << "  Account " << no << " unlocked.\n";
                    break;
                }
                case 5:
                    bank.backup(BACKUP_FILE);
                    cout << "  Data copied to " << BACKUP_FILE << ".\n";
                    break;
                case 6:
                    running = false;
                    break;
            }
        }
        catch (InputClosed&)
        {
            cout << "\n  Input closed. Exiting.\n";
            running = false;
        }
        catch (BankException& e)
        {
            cout << "\n  [Error] " << e.what() << "\n";
        }
    }

    cout << "\n  Goodbye!\n";
    return 0;
}
