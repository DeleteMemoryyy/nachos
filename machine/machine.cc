// machine.cc
//	Routines for simulating the execution of user programs.
//
//  DO NOT CHANGE -- part of the machine emulation
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "machine.h"
#include "copyright.h"
#include "system.h"

// Textual names of the exceptions that can be generated by user program
// execution, for debugging.
static char *exceptionNames[] = {
    "no exception",  "syscall",  "page fault/no TLB entry", "page read only", "bus error",
    "address error", "overflow", "illegal instruction"};

//----------------------------------------------------------------------
// CheckEndian
// 	Check to be sure that the host really uses the format it says it
//	does, for storing the bytes of an integer.  Stop on error.
//----------------------------------------------------------------------

static void CheckEndian()
{
    union checkit
    {
        char charword[4];
        unsigned int intword;
    } check;

    check.charword[0] = 1;
    check.charword[1] = 2;
    check.charword[2] = 3;
    check.charword[3] = 4;

#ifdef HOST_IS_BIG_ENDIAN
    ASSERT(check.intword == 0x01020304);
#else
    ASSERT(check.intword == 0x04030201);
#endif
}

//----------------------------------------------------------------------
// Machine::Machine
// 	Initialize the simulation of user program execution.
//
//	"debug" -- if TRUE, drop into the debugger after each user instruction
//		is executed.
//----------------------------------------------------------------------

Machine::Machine(bool debug)
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
        registers[i] = 0;
    mainMemory = new char[MemorySize];
    for (i = 0; i < MemorySize; i++)
        mainMemory[i] = 0;
    memStatusMap = new BitMap(NumPhysPages);

    swapSpace = new char[SwapSize];
    for (int i = 0; i < SwapSize; ++i)
        swapSpace[i] = 0;
    swapStatusMap = new BitMap(NumSwapPages);

    PTReplaceStrategy = PT_FIFO;
#ifdef USE_TLB
    printf("    Initializing TLB\n");
    printf("    TLB pages num: %d\n", TLBSize);
    tlb = new TranslationEntry[TLBSize];
    for (i = 0; i < TLBSize; i++)
        tlb[i].valid = FALSE;
    TLBReplaceStrategy = TLB_LRU;
#else  // use linear page table
    printf("        TLB Not Used\n");
    tlb = NULL;
#endif
    pageTable = NULL;
    swapPageTable = NULL;
    execFile = NULL;
    offsetVaddrToFile = 0;

    singleStep = debug;
    timeStamp = 0;
    TLBHitCount = 0;
    TLBMissCount = 0;
    CheckEndian();
}

//----------------------------------------------------------------------
// Machine::~Machine
// 	De-allocate the data structures used to simulate user program execution.
//----------------------------------------------------------------------

Machine::~Machine()
{
    delete[] mainMemory;
    delete memStatusMap;
    delete[] swapSpace;
    delete swapStatusMap;
    if (tlb != NULL)
        {
            delete[] tlb;
        }
}

//----------------------------------------------------------------------
// Machine::RaiseException
// 	Transfer control to the Nachos kernel from user mode, because
//	the user program either invoked a system call, or some exception
//	occured (such as the address translation failed).
//
//	"which" -- the cause of the kernel trap
//	"badVaddr" -- the virtual address causing the trap, if appropriate
//----------------------------------------------------------------------

void Machine::RaiseException(ExceptionType which, int badVAddr)
{
    DEBUG('m', "Exception: %s\n", (exceptionNames[which]));

    //  ASSERT(interrupt->getStatus() == UserMode);
    registers[BadVAddrReg] = badVAddr;
    DelayedLoad(0, 0);  // finish anything in progress
    interrupt->setStatus(SystemMode);
    ExceptionHandler(which);  // interrupts are enabled at this point
    interrupt->setStatus(UserMode);
}

//----------------------------------------------------------------------
// Machine::Debugger
// 	Primitive debugger for user programs.  Note that we can't use
//	gdb to debug user programs, since gdb doesn't run on top of Nachos.
//	It could, but you'd have to implement *a lot* more system calls
//	to get it to work!
//
//	So just allow single-stepping, and printing the contents of memory.
//----------------------------------------------------------------------

void Machine::Debugger()
{
    char *buf = new char[80];
    int num;

    interrupt->DumpState();
    DumpState();
    printf("%d> ", stats->totalTicks);
    fflush(stdout);
    fgets(buf, 80, stdin);
    if (sscanf(buf, "%d", &num) == 1)
        runUntilTime = num;
    else
        {
            runUntilTime = 0;
            switch (*buf)
                {
                    case '\n':
                        break;

                    case 'c':
                        singleStep = FALSE;
                        break;

                    case '?':
                        printf("Machine commands:\n");
                        printf("    <return>  execute one instruction\n");
                        printf("    <number>  run until the given timer tick\n");
                        printf("    c         run until completion\n");
                        printf("    ?         print help message\n");
                        break;
                }
        }
    delete[] buf;
}

//----------------------------------------------------------------------
// Machine::DumpState
// 	Print the user program's CPU state.  We might print the contents
//	of memory, but that seemed like overkill.
//----------------------------------------------------------------------

void Machine::DumpState()
{
    int i;

    printf("Machine registers:\n");
    for (i = 0; i < NumGPRegs; i++)
        switch (i)
            {
                case StackReg:
                    printf("\tSP(%d):\t0x%x%s", i, registers[i], ((i % 4) == 3) ? "\n" : "");
                    break;

                case RetAddrReg:
                    printf("\tRA(%d):\t0x%x%s", i, registers[i], ((i % 4) == 3) ? "\n" : "");
                    break;

                default:
                    printf("\t%d:\t0x%x%s", i, registers[i], ((i % 4) == 3) ? "\n" : "");
                    break;
            }

    printf("\tHi:\t0x%x", registers[HiReg]);
    printf("\tLo:\t0x%x\n", registers[LoReg]);
    printf("\tPC:\t0x%x", registers[PCReg]);
    printf("\tNextPC:\t0x%x", registers[NextPCReg]);
    printf("\tPrevPC:\t0x%x\n", registers[PrevPCReg]);
    printf("\tLoad:\t0x%x", registers[LoadReg]);
    printf("\tLoadV:\t0x%x\n", registers[LoadValueReg]);
    printf("\n");
}

//----------------------------------------------------------------------
// Machine::ReadRegister/WriteRegister
//   	Fetch or write the contents of a user program register.
//----------------------------------------------------------------------

int Machine::ReadRegister(int num)
{
    ASSERT((num >= 0) && (num < NumTotalRegs));
    return registers[num];
}

void Machine::WriteRegister(int num, int value)
{
    ASSERT((num >= 0) && (num < NumTotalRegs));
    // DEBUG('m', "WriteRegister %d, value %d\n", num, value);
    registers[num] = value;
}

void Machine::TLBMissHandler()
{
    int badVAddr = ReadRegister(BadVAddrReg);
    unsigned int vpn = (unsigned)badVAddr / PageSize;

    int physPage = 0;
    if (pageTable[vpn].valid)
        {
            physPage = pageTable[vpn].physicalPage;
        }
    else
        {
            physPage = PageLoad(vpn);
        }

    switch (PTReplaceStrategy)
        {
            case PT_LRU:
                {
                    pageTable[vpn].tValue = machine->timeStamp;  // update last used time
                }
                break;
                // case PT_LFU:
                //     {
                //         pageTable[vpn].tValue++;  // update used count
                //     }
                //     break;
        }

    bool invalidExist = false;
    for (int i = 0; i < TLBSize; ++i)
        {
            if (!tlb[i].valid)
                {
                    tlb[i].virtualPage = vpn;
                    tlb[i].physicalPage = physPage;
                    tlb[i].valid = true;
                    tlb[i].dirty = pageTable[vpn].dirty;
                    tlb[i].readOnly = pageTable[vpn].readOnly;

                    invalidExist = true;
                    break;
                }
        }

    if (!invalidExist)
        {
            int replacedTLB = 0;
            switch (TLBReplaceStrategy)
                {
                    case TLB_LRU:
                        {
                            int earliestUsedTime = (1 << 30);
                            for (int i = 0; i < TLBSize; ++i)
                                {
                                    if (tlb[i].tValue < earliestUsedTime)
                                        {
                                            earliestUsedTime = tlb[i].tValue;
                                            replacedTLB = i;
                                        }
                                }
                        }
                        break;
                    case TLB_FIFO:
                        {
                            int esrliestCreatedTime = (1 << 30);
                            for (int i = 0; i < TLBSize; ++i)
                                {
                                    if (tlb[i].tValue < esrliestCreatedTime)
                                        {
                                            esrliestCreatedTime = tlb[i].tValue;
                                            replacedTLB = i;
                                        }
                                }
                            tlb[replacedTLB].tValue = machine->timeStamp;  // update created time
                        }
                        break;
                    // case TLB_LFU:
                    //     {
                    //         int minUsedCount = (1 << 30);
                    //         for (int i = 0; i < TLBSize; ++i)
                    //             {
                    //                 if (tlb[i].tValue < minUsedCount)
                    //                     {
                    //                         minUsedCount = tlb[i].tValue;
                    //                         replacedTLB = i;
                    //                     }
                    //             }
                    //         tlb[replacedTLB].tValue = 0;  // initialize used count
                    //     }
                    //     break;
                    default:
                        ASSERT(false);  // unknow replacement strategy
                }
            tlb[replacedTLB].virtualPage = vpn;
            tlb[replacedTLB].physicalPage = physPage;
            tlb[replacedTLB].valid = true;
            pageTable[replacedTLB].dirty =
                tlb[replacedTLB].dirty;  // update dirty bit in page table
        }
}

void Machine::PageFaultHandler()
{
    int badVAddr = ReadRegister(BadVAddrReg);
    unsigned int vpn = (unsigned)badVAddr / PageSize;
    PageLoad(vpn);
}

int Machine::PageLoad(int vpn)
{
    int physPage = memStatusMap->Find();
    if (physPage == -1)  // physical space has been used up, find a page to swap out
        {
            int swapOutPage = -1;
            int earliestUsedTime = (1 << 30);
            for (int i = 0; i < pageTableSize; ++i)
                {
                    if (pageTable[i].valid && pageTable[i].tValue < earliestUsedTime)
                        {
                            earliestUsedTime = pageTable[i].tValue;
                            swapOutPage = i;
                        }
                }
            ASSERT(swapOutPage >= 0);
            int swapSpacePage = swapStatusMap->Find();
            ASSERT(swapSpacePage >= 0);
            physPage = pageTable[swapOutPage].physicalPage;
            int swapOutAddrStart = physPage * PageSize, swapAddrStart = swapSpacePage * PageSize;
            for (int i = 0; i < PageSize; ++i)
                swapSpace[swapAddrStart + i] = mainMemory[swapOutAddrStart + i];
            swapPageTable[swapOutPage].virtualPage = swapOutPage;
            swapPageTable[swapOutPage].physicalPage = swapSpacePage;
            swapPageTable[swapOutPage].valid = true;
            swapPageTable[swapOutPage].dirty = pageTable[swapOutPage].dirty;
            swapPageTable[swapOutPage].readOnly = pageTable[swapOutPage].readOnly;
            pageTable[swapOutPage].valid = false;

            if (tlb != NULL)
                {
                    for (int i = 0; i < TLBSize; ++i)
                        if (tlb[i].valid && tlb[i].virtualPage == swapOutPage)
                            {
                                tlb[i].valid = false;
                                break;
                            }
                }

            printf("Page Swap Out: vpn=%d, ppn=%d, spn=%d\n", swapOutPage, physPage, swapSpacePage);
        }

    int physAddrStart = physPage * PageSize;
    if (swapPageTable[vpn].valid)  // file in swap space
        {
            int swapAddrStart = swapPageTable[vpn].physicalPage * PageSize;
            for (int i = 0; i < PageSize; ++i)
                mainMemory[physAddrStart + i] = swapSpace[swapAddrStart + i];
            pageTable[vpn].dirty = swapPageTable[vpn].dirty;
            pageTable[vpn].readOnly = swapPageTable[vpn].readOnly;
            swapPageTable[vpn].valid = false;
            swapStatusMap->Clear(swapPageTable[vpn].physicalPage);
            printf("Page load from swap space: vpn=%d, ppn=%d, spn=%d\n", vpn, physPage,
                   swapPageTable[vpn].physicalPage);
        }
    else  // file in disk
        {
            ASSERT(execFile != NULL);
            execFile->ReadAt(&(mainMemory[physAddrStart]), PageSize,
                             (char *)(vpn * PageSize + offsetVaddrToFile));
            pageTable[vpn].dirty = false;
            pageTable[vpn].readOnly =
                (vpn >= readOnlyPageStart && vpn < readOnlyPageEnd) ? true : false;
            printf("Page load from disk: vpn=%d, ppn=%d\n", vpn, physPage);
        }
    pageTable[vpn].virtualPage = vpn;
    pageTable[vpn].physicalPage = physPage;
    pageTable[vpn].valid = true;

    switch (PTReplaceStrategy)
        {
            case PT_FIFO:
                {
                    pageTable[vpn].tValue = timeStamp;
                }
                break;
        }

    return physPage;
}

void Machine::printTLBStat()
{
    printf("TLB hit: %d    TLB miss: %d    ", TLBHitCount, TLBMissCount);
    printf("Hitting rate: %.5f\n", (float)TLBHitCount / (float)(TLBHitCount + TLBMissCount));
}