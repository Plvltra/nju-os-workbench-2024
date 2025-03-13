#include <os.h>
#include <common.h>
#include <objs/objs.h>
#include <devices.h>

#define N 1
#define NPROD 4
#define NCONS 4

typedef struct Handler{
    int seq;
    int event;
    handler_t handler;
    struct Handler *next;
} Handler;
static Handler *handler_list = NULL;

static void tty_reader(void *arg) {
    TRACE_ENTRY;
    device_t *tty = dev->lookup(arg);
    char cmd[128], resp[128], ps[16];
    snprintf(ps, 16, "(%s) $ ", arg);
    while (1) {
        tty->ops->write(tty, 0, ps, strlen(ps));
        int nread = tty->ops->read(tty, 0, cmd, sizeof(cmd) - 1);
        cmd[nread] = '\0';
        sprintf(resp, "tty reader task: got %d character(s).\n", strlen(cmd));
        tty->ops->write(tty, 0, resp, strlen(resp));
    }
    TRACE_EXIT;
}

#ifdef DEBUG_PRODUCER_CONSUMER
sem_t empty, fill;
void T_produce(void *arg) { 
    while (1) {
        P(&empty);
        putch('(');
        V(&fill);
    }
}
void T_consume(void *arg) {
    while (1) {
        P(&fill);
        putch(')');
        V(&empty);
    }
}

static void test_producer_consumer() {
    kmt->sem_init(&empty, "empty", N);
    kmt->sem_init(&fill,  "fill",  0);
    for (int i = 0; i < NPROD; i++) {
        kmt->create(pmm->alloc(sizeof(task_t)), "producer", T_produce, NULL);
    }
    for (int i = 0; i < NCONS; i++) {
        kmt->create(pmm->alloc(sizeof(task_t)), "consumer", T_consume, NULL);
    }
}
#endif

static void os_init() {
    // Module initialization
    pmm->init(); // Init pmm first
    kmt->init();
#ifdef DEBUG_TTY
    dev->init();

    kmt->create(pmm->alloc(sizeof(task_t)), "tty_reader1", tty_reader, "tty1");
    kmt->create(pmm->alloc(sizeof(task_t)), "tty_reader2", tty_reader, "tty2");
#endif

#ifdef DEBUG_PRODUCER_CONSUMER
    test_producer_consumer();
#endif
}

// Registered in mpe_init in main.c, it is called after os initialization
static void os_run() {
    for (const char *s = "Hello World from CPU #*\n"; *s; s++) {
        putch(*s == '*' ? '0' + cpu_current() : *s);
    }

    iset(true);
    while (1);
}

bool sane_context(Context *next)
{
    if (next->rsp < 0x2300000 || next->rsp > 0x4000000)
        return true;
    return false;
}

static Context *os_trap(Event ev, Context *ctx) {
    TRACE_ENTRY;
    CHECK_RSP;
    push_off(); // Disable interrupts in interrupt handler.

    Context *next = NULL;
    Handler *h = handler_list;
    while (h != NULL) {
        if (h->event == EVENT_NULL || h->event == ev.event) {
            Context *r = h->handler(ev, ctx);
            panic_on(r && next, "return to multiple contexts");
            if (r) next = r;
        }
        h = h->next;
    }
    panic_on(!next, "return to NULL context");
    panic_on(sane_context(next), "return to invalid context");

    pop_off(); // TOFIX: after user_handler, __am_irq_handle still be interupted
    TRACE_EXIT;
    return next;
}

static void os_on_irq(int seq, int event, handler_t handler)
{
    Handler *h = (Handler *)pmm->alloc(sizeof(Handler));
    h->seq = seq;
    h->event = event;
    h->handler = handler;
    h->next = NULL;

    Handler dummy_first = { .seq = INT_MIN, .event = EVENT_NULL, .handler = NULL, .next = handler_list };
    for (Handler *iter = &dummy_first; iter != NULL; iter = iter->next)
    {
        bool can_insert = iter->seq <= h->seq
            && (iter->next == NULL || iter->next->seq >= h->seq);
        if (can_insert) {
            h->next = iter->next;
            iter->next = h;
            break;
        }
    }
    handler_list = dummy_first.next;
}

MODULE_DEF(os) = {
    .init   = os_init,
    .run    = os_run,
    .trap   = os_trap,
    .on_irq = os_on_irq,
};
