// SPDX-License-Identifier: GPL-3.0-only
// A program for the debugger plugin's test, which names these lines.
#include <cstdio>

int add(int a, int b) {
    int sum = a + b;
    // A line without code.
    return sum;
}

int main(int argc, char**) {
    std::printf("printed %d\n", argc);
    std::fflush(stdout);
    const int result = add(2, 3);
    return result;
}
