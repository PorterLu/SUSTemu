/*
 * Dhrystone 2.1 — bare-metal RISC-V port for sustemu
 *
 * Adapted from the public-domain Dhrystone 2.1 benchmark
 * (Reinhold P. Weicker, 1988).  This port removes all OS/stdlib
 * dependencies and uses:
 *   - MMIO serial port  (0xa00003f8) for output
 *   - rdcycle CSR       for cycle counting
 *   - No heap / malloc  (all allocations are static/stack)
 */

/* ── Minimal runtime ─────────────────────────────────────────────────────── */

typedef unsigned long  uint64_t;
typedef unsigned int   uint32_t;
typedef unsigned char  uint8_t;
typedef long           int64_t;
typedef int            int32_t;

/* Serial port — same as test/main.c */
#define UART_THR_ADDR 0xa00003f8ULL
#define UART_THR      (*(volatile char *)UART_THR_ADDR)

static void uart_putc(char c) { UART_THR = c; }

static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_put_uint(uint64_t v)
{
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[--i] = '0' + (v % 10); v /= 10; }
    uart_puts(buf + i);
}

static inline uint64_t rdcycle(void)
{
    uint64_t c;
    asm volatile("rdcycle %0" : "=r"(c));
    return c;
}

/* ── Dhrystone 2.1 types and constants ───────────────────────────────────── */

#define LOOPS  500000   /* number of benchmark iterations */

typedef int     Boolean;
typedef char    Char;
typedef int     Int;
typedef long    Long;
typedef char   *Str30;   /* pointer to char, <= 30 chars */

#define TRUE    1
#define FALSE   0

typedef enum { Ident_1, Ident_2, Ident_3, Ident_4, Ident_5 } Enumeration;

typedef struct Record {
    struct Record  *Ptr_Comp;
    Enumeration     Discr;
    union {
        struct {
            Enumeration Enum_Comp;
            Int         Int_Comp;
            char        Str_Comp[31];
        } var_1;
        struct {
            Enumeration E_Comp_2;
            char        Str_2_Comp[31];
        } var_2;
        struct {
            char Char_1_Comp;
            char Char_2_Comp;
        } var_3;
    } variant;
} RecordType, *RecordPtr;

/* ── Minimal string helpers ──────────────────────────────────────────────── */

static void my_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++));
}

static int my_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int my_strlen(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

/* ── Global variables ────────────────────────────────────────────────────── */

static RecordType Rec_1;
static RecordType Rec_2;
static RecordPtr  Ptr_Glob;
static RecordPtr  Next_Ptr_Glob;

static Int    Int_Glob;
static Boolean Bool_Glob;
static char   Char_Glob_1;
static char   Char_Glob_2;
static Int    Arr_1_Glob[50];
static Int    Arr_2_Glob[50][50];
static char   Str_1_Glob[31];
static char   Str_2_Glob[31];

/* ── Forward declarations ────────────────────────────────────────────────── */

static void      Proc_1(RecordPtr PtrParIn);
static void      Proc_2(Int *IntParIO);
static void      Proc_3(RecordPtr *PtrParOut);
static void      Proc_4(void);
static void      Proc_5(void);
static void      Proc_6(Enumeration EnumParIn, Enumeration *EnumParOut);
static void      Proc_7(Int IntParI1, Int IntParI2, Int *IntParOut);
static void      Proc_8(Int ArrParI1[], Int ArrParI2[][50],
                        Int IntParI1, Int IntParI2);
static Enumeration Func_1(char CharPar1, char CharPar2);
static Boolean     Func_2(Str30 StrParI1, Str30 StrParI2);
static Boolean     Func_3(Enumeration EnumParIn);

/* ── Dhrystone procedures ────────────────────────────────────────────────── */

static void Proc_1(RecordPtr PtrParIn)
{
    RecordPtr NextRecord = PtrParIn->Ptr_Comp;

    *PtrParIn->Ptr_Comp = *Ptr_Glob;
    PtrParIn->variant.var_1.Int_Comp = 5;
    NextRecord->variant.var_1.Int_Comp = PtrParIn->variant.var_1.Int_Comp;
    NextRecord->Ptr_Comp = PtrParIn->Ptr_Comp;
    Proc_3(&NextRecord->Ptr_Comp);

    if (NextRecord->Discr == Ident_1) {
        NextRecord->variant.var_1.Int_Comp = 6;
        Proc_6(PtrParIn->variant.var_1.Enum_Comp,
               &NextRecord->variant.var_1.Enum_Comp);
        NextRecord->Ptr_Comp = Ptr_Glob->Ptr_Comp;
        Proc_7(NextRecord->variant.var_1.Int_Comp, 10,
               &NextRecord->variant.var_1.Int_Comp);
    } else {
        *PtrParIn = *PtrParIn->Ptr_Comp;
    }
}

static void Proc_2(Int *IntParIO)
{
    Int IntLoc;
    Enumeration EnumLoc;

    IntLoc = *IntParIO + 10;
    for (;;) {
        if (Char_Glob_1 == 'A') {
            IntLoc--;
            *IntParIO = IntLoc - Int_Glob;
            EnumLoc = Ident_1;
        }
        if (EnumLoc == Ident_1) break;
    }
}

static void Proc_3(RecordPtr *PtrParOut)
{
    if (Ptr_Glob != (RecordPtr)0)
        *PtrParOut = Ptr_Glob->Ptr_Comp;
    else
        Int_Glob = 100;
    Proc_7(10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp);
}

static void Proc_4(void)
{
    Boolean BoolLoc;
    BoolLoc = Char_Glob_1 == 'A';
    Bool_Glob = BoolLoc | Bool_Glob;
    Char_Glob_2 = 'B';
}

static void Proc_5(void)
{
    Char_Glob_1 = 'A';
    Bool_Glob = FALSE;
}

static void Proc_6(Enumeration EnumParIn, Enumeration *EnumParOut)
{
    *EnumParOut = EnumParIn;
    if (!Func_3(EnumParIn))
        *EnumParOut = Ident_4;

    switch (EnumParIn) {
    case Ident_1: *EnumParOut = Ident_1; break;
    case Ident_2:
        if (Int_Glob > 100) *EnumParOut = Ident_1;
        else                *EnumParOut = Ident_4;
        break;
    case Ident_3: *EnumParOut = Ident_2; break;
    case Ident_4:                         break;
    case Ident_5: *EnumParOut = Ident_3; break;
    }
}

static void Proc_7(Int IntParI1, Int IntParI2, Int *IntParOut)
{
    Int IntLoc;
    IntLoc = IntParI1 + 2;
    *IntParOut = IntParI2 + IntLoc;
}

static void Proc_8(Int ArrParI1[], Int ArrParI2[][50],
                   Int IntParI1, Int IntParI2)
{
    Int IntLoc;
    Int IntIndex;

    IntLoc = IntParI1 + 5;
    ArrParI1[IntLoc] = IntParI2;
    ArrParI1[IntLoc + 1] = ArrParI1[IntLoc];
    ArrParI1[IntLoc + 30] = IntLoc;
    for (IntIndex = IntLoc; IntIndex <= IntLoc + 1; IntIndex++)
        ArrParI2[IntLoc][IntIndex] = IntLoc;
    ArrParI2[IntLoc][IntLoc - 1]++;
    ArrParI2[IntLoc + 20][IntLoc] = ArrParI1[IntLoc];
    Int_Glob = 5;
}

static Enumeration Func_1(char CharPar1, char CharPar2)
{
    char CharLoc1, CharLoc2;
    CharLoc1 = CharPar1;
    CharLoc2 = CharLoc1;
    if (CharLoc2 != CharPar2)
        return Ident_1;
    else {
        (void)CharLoc2;
        return Ident_2;
    }
}

static Boolean Func_2(Str30 StrParI1, Str30 StrParI2)
{
    Int IntLoc;
    char CharLoc;

    IntLoc = 2;
    while (IntLoc <= 2) {
        if (Func_1(StrParI1[IntLoc], StrParI2[IntLoc + 1]) == Ident_1) {
            CharLoc = 'A';
            IntLoc++;
        }
    }
    if (CharLoc >= 'W' && CharLoc < 'Z')
        IntLoc = 7;
    if (CharLoc == 'R')
        return TRUE;
    else {
        if (my_strcmp(StrParI1, StrParI2) > 0) {
            IntLoc += 7;
            return TRUE;
        } else
            return FALSE;
    }
}

static Boolean Func_3(Enumeration EnumParIn)
{
    Enumeration EnumLoc = EnumParIn;
    if (EnumLoc == Ident_3)
        return TRUE;
    else
        return FALSE;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Local variables for main loop */
    Int     IntLoc1, IntLoc2, IntLoc3;
    char    CharLoc, CharIndex;
    Enumeration EnumLoc;
    char    Str_1_Loc[31];
    char    Str_2_Loc[31];
    uint64_t start_cycle, end_cycle, elapsed;
    Int Run_Index;

    uart_puts("Dhrystone 2.1 Bare-metal RISC-V\r\n");
    uart_puts("Loops: ");
    uart_put_uint(LOOPS);
    uart_puts("\r\n\r\n");

    /* Initialise */
    Next_Ptr_Glob = &Rec_1;
    Ptr_Glob      = &Rec_2;

    Ptr_Glob->Ptr_Comp             = Next_Ptr_Glob;
    Ptr_Glob->Discr                = Ident_1;
    Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    Ptr_Glob->variant.var_1.Int_Comp  = 40;
    my_strcpy(Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING");

    my_strcpy(Str_1_Glob, "DHRYSTONE PROGRAM, 1'ST STRING");
    Arr_2_Glob[8][7] = 10;

    /* ── Warm-up pass (not timed) ────────────────────────────────────────── */
    for (Run_Index = 1; Run_Index <= LOOPS / 10; Run_Index++) {
        Proc_5();
        Proc_4();
        IntLoc1 = 2;  IntLoc2 = 3;
        my_strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        EnumLoc = Ident_2;
        Bool_Glob = !Func_2(Str_1_Glob, Str_2_Loc);
        while (IntLoc1 < IntLoc2) {
            IntLoc3 = 5 * IntLoc1 - IntLoc2;
            Proc_7(IntLoc1, IntLoc2, &IntLoc3);
            IntLoc1++;
        }
        Proc_8(Arr_1_Glob, Arr_2_Glob, IntLoc1, IntLoc3);
        Proc_1(Ptr_Glob);
        for (CharIndex = 'A'; CharIndex <= Char_Glob_2; CharIndex++) {
            if (EnumLoc == Func_1(CharIndex, 'C'))
                Proc_6(Ident_1, &EnumLoc);
        }
        IntLoc3 = IntLoc2 * IntLoc1;
        IntLoc2 = IntLoc3 / IntLoc1;
        IntLoc2 = 7 * (IntLoc3 - IntLoc2) - IntLoc1;
        Proc_2(&IntLoc1);
        (void)my_strlen(Str_1_Glob);
        (void)my_strlen(Str_2_Loc);
    }

    /* ── Timed run ───────────────────────────────────────────────────────── */
    start_cycle = rdcycle();

    for (Run_Index = 1; Run_Index <= LOOPS; Run_Index++) {
        Proc_5();
        Proc_4();
        IntLoc1 = 2;  IntLoc2 = 3;
        my_strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        EnumLoc = Ident_2;
        Bool_Glob = !Func_2(Str_1_Glob, Str_2_Loc);
        while (IntLoc1 < IntLoc2) {
            IntLoc3 = 5 * IntLoc1 - IntLoc2;
            Proc_7(IntLoc1, IntLoc2, &IntLoc3);
            IntLoc1++;
        }
        Proc_8(Arr_1_Glob, Arr_2_Glob, IntLoc1, IntLoc3);
        Proc_1(Ptr_Glob);
        for (CharIndex = 'A'; CharIndex <= Char_Glob_2; CharIndex++) {
            if (EnumLoc == Func_1(CharIndex, 'C'))
                Proc_6(Ident_1, &EnumLoc);
        }
        IntLoc3 = IntLoc2 * IntLoc1;
        IntLoc2 = IntLoc3 / IntLoc1;
        IntLoc2 = 7 * (IntLoc3 - IntLoc2) - IntLoc1;
        Proc_2(&IntLoc1);
    }

    end_cycle = rdcycle();
    elapsed = end_cycle - start_cycle;

    /* ── Results ─────────────────────────────────────────────────────────── */
    uart_puts("Cycles elapsed : ");
    uart_put_uint(elapsed);
    uart_puts("\r\nLoops          : ");
    uart_put_uint(LOOPS);
    uart_puts("\r\nCycles/loop    : ");
    uart_put_uint(elapsed / LOOPS);
    uart_puts("\r\n\r\n");

    /* Correctness check */
    uart_puts("Correctness check:\r\n");
    uart_puts("  Int_Glob     = "); uart_put_uint(Int_Glob);   uart_puts(" (expect 5)\r\n");
    uart_puts("  Bool_Glob    = "); uart_put_uint(Bool_Glob);  uart_puts(" (expect 1)\r\n");
    uart_puts("  Char_Glob_1  = "); uart_putc(Char_Glob_1);   uart_puts(" (expect A)\r\n");
    uart_puts("  Char_Glob_2  = "); uart_putc(Char_Glob_2);   uart_puts(" (expect B)\r\n");
    uart_puts("  Arr_2[8][7]  = "); uart_put_uint(Arr_2_Glob[8][7]);
    uart_puts(" (expect "); uart_put_uint(LOOPS + LOOPS / 10 + 10); uart_puts(")\r\n");
    uart_puts("  IntLoc1      = "); uart_put_uint(IntLoc1);   uart_puts(" (expect 7)\r\n");
    uart_puts("  IntLoc2      = "); uart_put_uint(IntLoc2);   uart_puts(" (expect 39)\r\n");
    uart_puts("  IntLoc3      = "); uart_put_uint(IntLoc3);   uart_puts(" (expect 9)\r\n");

    return 0;
}
