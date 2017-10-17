// synch.cc
//	Routines for synchronizing threads.  Three kinds of
//	synchronization routines are defined here: semaphores, locks
//   	and condition variables (the implementation of the last two
//	are left to the reader).
//
// Any implementation of a synchronization routine needs some
// primitive atomic operation.  We assume Nachos is running on
// a uniprocessor, and thus atomicity can be provided by
// turning off interrupts.  While interrupts are disabled, no
// context switch can occur, and thus the current thread is guaranteed
// to hold the CPU throughout, until interrupts are reenabled.
//
// Because some of these routines might be called with interrupts
// already disabled (Semaphore::V for one), instead of turning
// on interrupts at the end of the atomic operation, we always simply
// re-set the interrupt state back to its original value (whether
// that be disabled or enabled).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "synch.h"
#include "copyright.h"
#include "system.h"

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	Initialize a semaphore, so that it can be used for synchronization.
//
//	"debugName" is an arbitrary name, useful for debugging.
//	"initialValue" is the initial value of the semaphore.
//----------------------------------------------------------------------

Semaphore::Semaphore(char *debugName, int initialValue)
{
    name = debugName;
    value = initialValue;
    queue = new List;
}

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	De-allocate semaphore, when no longer needed.  Assume no one
//	is still waiting on the semaphore!
//----------------------------------------------------------------------

Semaphore::~Semaphore()
{
    delete queue;
}

//----------------------------------------------------------------------
// Semaphore::P
// 	Wait until semaphore value > 0, then decrement.  Checking the
//	value and decrementing must be done atomically, so we
//	need to disable interrupts before checking the value.
//
//	Note that Thread::Sleep assumes that interrupts are disabled
//	when it is called.
//----------------------------------------------------------------------

void Semaphore::P()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);  // disable interrupts

    while (value == 0)
        {                                          // semaphore not available
            queue->Append((void *)currentThread);  // so go to sleep
            currentThread->Sleep();
        }
    value--;  // semaphore available,
              // consume its value

    (void)interrupt->SetLevel(oldLevel);  // re-enable interrupts
}

//----------------------------------------------------------------------
// Semaphore::V
// 	Increment semaphore value, waking up a waiter if necessary.
//	As with P(), this operation must be atomic, so we need to disable
//	interrupts.  Scheduler::ReadyToRun() assumes that threads
//	are disabled when it is called.
//----------------------------------------------------------------------

void Semaphore::V()
{
    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    thread = (Thread *)queue->Remove();
    if (thread != NULL)  // make thread ready, consuming the V immediately
        scheduler->ReadyToRun(thread);
    value++;
    (void)interrupt->SetLevel(oldLevel);
}

Lock::Lock(char *debugName)
{
    name = debugName;
    semphore = new Semaphore("semphore in lock", 1);
    holder = NULL;
}

Lock::~Lock()
{
    if (semphore != NULL)
        delete semphore;
}

void Lock::Acquire()
{
    semphore->P();
    holder = currentThread;
}

void Lock::Release()
{
    if (isHeldByCurrentThread())
        {
            holder = NULL;
            semphore->V();
        }
}

// Note -- without a correct implementation of Condition::Wait(),
// the test case in the network assignment won't work!

Condition::Condition(char *debugName)
{
    name = debugName;
    queue = new List;
}

Condition::~Condition()
{
    if (queue != NULL)
        delete queue;
}

void Condition::Wait(Lock *conditionLock)
{
    ASSERT(conditionLock->isHeldByCurrentThread());
    conditionLock->Release();

    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    queue->Append((void *)currentThread);
    currentThread->Sleep();

    (void)interrupt->SetLevel(oldLevel);

    conditionLock->Acquire();
}

void Condition::Signal(Lock *conditionLock)
{
    ASSERT(conditionLock->isHeldByCurrentThread());

    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    thread = (Thread *)queue->Remove();
    if (thread != NULL)
        scheduler->ReadyToRun(thread);

    (void)interrupt->SetLevel(oldLevel);
}

void Condition::Broadcast(Lock *conditionLock)
{
    ASSERT(conditionLock->isHeldByCurrentThread());

    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    while (!queue->IsEmpty())
        {
            thread = (Thread *)queue->Remove();

            if (thread != NULL)
                scheduler->ReadyToRun(thread);
        }

    (void)interrupt->SetLevel(oldLevel);
}

Barrier::Barrier(char *debugName, int initialValue)
{
    name = debugName;
    value = initialValue;
    conditionLock = new Lock("Lock in Barrier");
    cv = new Condition("CV in Barrier");
}

Barrier::~Barrier()
{
    if (conditionLock != NULL)
        delete conditionLock;
    if (cv != NULL)
        delete cv;
}

void Barrier::Wait()
{
    conditionLock->Acquire();

    value--;

    if (value == 0)
        {
            cv->Broadcast(conditionLock);
            conditionLock->Release();
            return;
        }
    else
        cv->Wait(conditionLock);

    conditionLock->Release();
}

RWLock::RWLock(char *debugName)
{
    name = debugName;
    readyToRead = new Condition("readyToRead");
    readyToWrite = new Condition("readyToWrite");
}

RWLock::~RWLock()
{
    if (readyToRead != NULL)
        delete readyToRead;
    if (readyToWrite != NULL)
        delete readyToWrite;
}

void RWLock::DownRead(Lock *rwcLock)
{
    ASSERT(rwcLock->isHeldByCurrentThread());
    while (writerCount > 0)
        {
            waitingReader++;
            readyToRead->Wait(rwcLock);
            waitingReader--;
        }
    readerCount++;
}

void RWLock::UpRead(Lock *rwcLock)
{
    ASSERT(rwcLock->isHeldByCurrentThread());
    readerCount--;
    if (readerCount == 0 && writerCount == 0)
        readyToWrite->Signal(rwcLock);
}

void RWLock::DownWrite(Lock *rwcLock)
{
    ASSERT(rwcLock->isHeldByCurrentThread());
    while (readerCount > 0 || writerCount > 0 || waitingReader > 0)
        readyToWrite->Wait(rwcLock);
    writerCount++;
}

void RWLock::UpWrite(Lock *rwcLock)
{
    ASSERT(rwcLock->isHeldByCurrentThread());
    writerCount--;
    if (waitingReader > 0)
        readyToRead->Broadcast(rwcLock);
    else if (writerCount == 0)
        readyToWrite->Signal(rwcLock);
}
