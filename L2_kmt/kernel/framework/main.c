// DO NOT MODIFY: Will be reverted by the Online Judge.

#include <kernel.h>
#include <klib.h>

int main() {
    ioe_init();
    cte_init(os->trap);
    os->init();
    mpe_init(os->run); // all cores call os->run()
    return 1;
}
