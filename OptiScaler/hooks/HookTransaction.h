#pragma once
namespace HookLifecycle {
struct Result { long code; const char* stage; explicit operator bool() const { return code == 0; } };
template<class Begin, class Update, class Modify, class Commit, class Abort>
Result Transact(Begin begin, Update update, Modify modify, Commit commit, Abort abort)
{
    long code = begin();
    if (code) return {code, "begin"};
    code = update();
    if (code) { abort(); return {code, "update_thread"}; }
    code = modify();
    if (code) { abort(); return {code, "modify"}; }
    code = commit(); // Commit completes (or rolls back) the transaction.
    return {code, "commit"};
}
}
