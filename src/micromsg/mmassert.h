#pragma once

#include <iostream>

#ifndef NDEBUG
#define mm_assert(Expr) __mm_assert(#Expr, Expr, __FILE__, __LINE__, nullptr)
#define mm_assert_m(Expr, Msg) __mm_assert(#Expr, Expr, __FILE__, __LINE__, Msg)
#else
#define mm_assert(Expr);
#define mm_assert_m(Expr, Msg);
#endif

void __mm_assert(const char* expr_str, bool expr, const char* file, int line, const char* msg) {
    if (!expr)
    {
        std::cerr << "Assertion failed:\t" << (msg ? msg : "") << std::endl
            << "Expected:\t" << expr_str << std::endl
            << "Source:\t\t" << file << ", line " << line << std::endl;
        abort();
    }
}