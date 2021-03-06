// exception.cc
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "syscall.h"
#include "system.h"

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions
//	are in machine.h.
//----------------------------------------------------------------------

void ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);
    // printf("Exception: %d %d\n", which, type);

    switch (which)
        {
            case SyscallException:
                {
                    switch (type)
                        {
                            case SC_Halt:
                                {
                                    DEBUG('a', "Shutdown, initiated by user program.\n");
                                    interrupt->Halt();
                                }
                                break;
                            case SC_Exit:
                                {
                                    printf("User program exit.\n");
                                    machine->printTLBStat();
                                    delete currentThread->space;
                                    currentThread->space = NULL;
                                    if (currentThread->parentThread != NULL)
                                        {
                                            Thread *pThread = currentThread->parentThread;
                                            for (int i = 0; i < MaxChildThreadNum; ++i)
                                                {
                                                    if (pThread->childThread[i] == currentThread)
                                                        {
                                                            pThread->childThread[i] = NULL;
                                                            break;
                                                        }
                                                }
                                            currentThread->Finish();
                                        }
                                    else  // main thread exit
                                        {
                                            machine->WriteRegister(2, 0);
                                            machine->IncreasePC();
                                        }
                                }
                                break;
                            case SC_Exec:
                                {
                                    char *name = (char *)machine->ReadRegister(4);
                                    Thread *newThread = new Thread("Exec");
                                    for (int i = 0; i < MaxChildThreadNum; ++i)
                                        if (currentThread->childThread[i] == NULL)
                                            {
                                                currentThread->childThread[i] = newThread;
                                                newThread->parentThread = currentThread;
                                                newThread->Fork(start_progress, name);
                                                machine->WriteRegister(2, newThread);
                                                machine->IncreasePC();
                                                return;
                                            }
                                    machine->WriteRegister(2, -1);
                                    machine->IncreasePC();
                                }
                                break;
                            case SC_Join:
                                {
                                    SpaceId id = (SpaceId)machine->ReadRegister(4);
                                    Thread *joinThread = (Thread *)id;
                                    for (int i = 0; i < MaxChildThreadNum; ++i)
                                        {
                                            if (currentThread->childThread[i] == joinThread)
                                                {
                                                    while (currentThread->childThread[i] != NULL)
                                                        currentThread->Yield();
                                                    machine->IncreasePC();
                                                    return;
                                                }
                                        }
                                    ASSERT(FALSE);
                                }
                                break;
                            case SC_Create:
                                {
                                    char *name = (char *)machine->ReadRegister(4);
                                    fileSystem->Create(name, 1);
                                    // printf("Syscall: Create\t file name:%s\n", name);
                                    machine->IncreasePC();
                                }
                                break;
                            case SC_Open:
                                {
                                    char *name = (char *)machine->ReadRegister(4);
                                    OpenFileId id = fileSystem->Open(name);
                                    machine->WriteRegister(2, id);
                                    machine->IncreasePC();
                                }
                                break;
                            case SC_Read:
                                {
                                    char *buffer = (char *)machine->ReadRegister(4);
                                    int size = (int)machine->ReadRegister(5);
                                    OpenFileId id = (OpenFileId)machine->ReadRegister(6);
                                    OpenFile *openFile = (OpenFile *)id;
                                    int result = openFile->Read(buffer, size);
                                    machine->WriteRegister(2, result);
                                    machine->IncreasePC();
                                }
                                break;
                            case SC_Write:
                                {
                                    char *buffer = (char *)machine->ReadRegister(4);
                                    int size = (int)machine->ReadRegister(5);
                                    OpenFileId id = (OpenFileId)machine->ReadRegister(6);
                                    OpenFile *openFile = (OpenFile *)id;
                                    openFile->Write(buffer, size);
                                    machine->IncreasePC();
                                }
                                break;
                            case SC_Close:
                                {
                                    OpenFileId id = (OpenFileId)machine->ReadRegister(4);
                                    OpenFile *openFile = (OpenFile *)id;
                                    delete openFile;
                                    machine->IncreasePC();
                                }
                                break;
                            case SC_Fork:
                                {
                                    char *name = (char *)machine->ReadRegister(4);
                                    Thread *newThread = new Thread("Exec");
                                    for (int i = 0; i < MaxChildThreadNum; ++i)
                                        if (currentThread->childThread[i] == NULL)
                                            {
                                                currentThread->childThread[i] = newThread;
                                                newThread->parentThread = currentThread;
                                                AddrSpacePC *parentSpacePC = new AddrSpacePC;
                                                parentSpacePC->space = currentThread->space;
                                                parentSpacePC->space = machine->ReadRegister(PCReg);
                                                newThread->Fork(before_fork, parentSpacePC);
                                                machine->IncreasePC();
                                                delete parentSpacePC;
                                                return;
                                            }
                                    machine->IncreasePC();
                                }
                                break;
                            case SC_Yield:
                                {
                                    machine->IncreasePC();
                                    currentThread->Yield();
                                }
                                break;
                            default:
                                {
                                    printf("Unexpected system call %d %d\n", which, type);
                                    ASSERT(FALSE);
                                }
                        }
                }
                break;
            case PageFaultException:
                {
                    if (machine->tlb != NULL)  // TLB miss
                        {
                            machine->TLBMissHandler();
                        }
                    else  // page fault
                        {
                            machine->PageFaultHandler();
                        }
                }
                break;
            case ReadOnlyException:
                {
                }
                break;
            case BusErrorException:
                {
                }
                break;
            case AddressErrorException:
                {
                }
                break;
            case OverflowException:
                {
                }
                break;
            case IllegalInstrException:
                {
                }
                break;
            case NumExceptionTypes:
                {
                }
                break;
            default:
                printf("Unexpected user mode exception %d %d\n", which, type);
                ASSERT(FALSE);
        }
}
