// SPDX-License-Identifier: GPL-3.0-only
// A program that runs until it is stopped, for the debugger plugin's test.
#include <unistd.h>

int main() {
    for (;;) sleep(1);
}
