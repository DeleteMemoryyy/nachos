// threadtest.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "elevatortest.h"
#include "synch.h"
#include "system.h"

// testnum is set in main.cc
int testnum = 1;

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, yielding the CPU to another ready thread
//	each iteration.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void SimpleThread(int which)
{
    int num;

    for (num = 0; num < 5; num++)
        {
            printf("*** thread %d looped %d times\n", which, num);
            currentThread->Yield();
        }
}

//----------------------------------------------------------------------
// ThreadHello
//  Say hello from a new forked thread.
//----------------------------------------------------------------------

void ThreadHello()
{
    printf("Thread %d named %s has been created.\n", currentThread->getTid(),
           currentThread->getName());
}

//----------------------------------------------------------------------
// ThreadTest1
// 	Set up a ping-pong between two threads, by forking a thread
//	to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1");

    Thread *t = new Thread("forked thread");

    t->Fork(SimpleThread, (void *)1);
    SimpleThread(0);
}

//----------------------------------------------------------------------
// ThreadTest2
//----------------------------------------------------------------------

void ThreadTest2()
{
    DEBUG('t', "Entering ThreadTest2");

    for (int i = 0; i < 130; ++i)
        {
            printf("TimeTick: %d\n", stats->totalTicks);
            Thread *t = threadPool->createThread("forked thread");
            if (t != NULL)
                t->Fork(ThreadHello, (void *)1);
            else
                printf("No empty slot in thread pool!\n");
        }

    ThreadStatus();
}

//----------------------------------------------------------------------
// ThreadTest3
//----------------------------------------------------------------------

void ThreadTest3()
{
    DEBUG('t', "Entering ThreadTest3");

    int p[10] = {1, 3, 6, 7, 9, 2, 4, 10, 3, 5};

    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 10; ++j)
            {
                printf("TimeTick: %d\n", stats->totalTicks);
                char name[10];
                memset(name, 0, 10);
                sprintf(name, "%d_%d\0", (i + 1) * (j + 1), p[j]);
                Thread *t = threadPool->createThread(name);
                t->setPriority(p[j]);
                if (t != NULL)
                    t->Fork(ThreadHello, (void *)1);
                else
                    printf("No empty slot in thread pool!\n");
            }

    ThreadStatus();
}

//----------------------------------------------------------------------
// ThreadTest4  Producer-Customer Problem
//----------------------------------------------------------------------

Semaphore semEmpty("Empty", 10), semFull("FUll", 0);
Lock lockBuf[10] = {Lock("Buf0"), Lock("Buf1"), Lock("Buf2"), Lock("Buf3"), Lock("Buf4"),
                    Lock("Buf5"), Lock("Buf6"), Lock("Buf7"), Lock("Buf8"), Lock("Buf9")};
int bufPCP[10];

void Produce()
{
    int cnt = 2;
    while (cnt--)
        {
            semEmpty.P();
            for (int i = 0; i < 10; ++i)
                {
                    lockBuf[i].Acquire();
                    if (bufPCP[i] == 0)
                        {
                            bufPCP[i] = 1;
                            printf("%s changes buf[%d] to 1.\n", currentThread->getName(), i);
                            printf("Buf:|");
                            for (int j = 0; j < 10; ++j)
                                printf("%d|", bufPCP[j]);
                            printf("\n");
                            lockBuf[i].Release();
                            currentThread->Yield();
                            break;
                        }
                    else
                        lockBuf[i].Release();
                }
            semFull.V();
        }
}

void Custom()
{
    int cnt = 2;
    while (cnt--)
        {
            semFull.P();
            for (int i = 0; i < 10; ++i)
                {
                    lockBuf[i].Acquire();
                    if (bufPCP[i] == 1)
                        {
                            bufPCP[i] = 0;
                            printf("%s changes buf[%d] to 0.\n", currentThread->getName(), i);
                            printf("Buf:|");
                            for (int j = 0; j < 10; ++j)
                                printf("%d|", bufPCP[j]);
                            printf("\n");
                            lockBuf[i].Release();
                            currentThread->Yield();
                            break;
                        }
                    else
                        lockBuf[i].Release();
                }
            semEmpty.V();
        }
}

void ThreadTest4()
{
    memset(bufPCP, 0, sizeof(bufPCP));
    Thread *prodThread[4], *custThread[3];
    prodThread[0] = threadPool->createThread("Producer 0");
    prodThread[1] = threadPool->createThread("Producer 1");
    prodThread[2] = threadPool->createThread("Producer 2");
    prodThread[3] = threadPool->createThread("Producer 3");
    custThread[0] = threadPool->createThread("Customer 0");
    custThread[1] = threadPool->createThread("Customer 1");
    custThread[2] = threadPool->createThread("Customer 2");

    for (int i = 0; i < 4; ++i)
        prodThread[i]->Fork(Produce, (void *)1);
    for (int i = 0; i < 3; ++i)
        custThread[i]->Fork(Custom, (void *)1);

    ThreadStatus();
}

//----------------------------------------------------------------------
// ThreadTest5  // Reader-Writer Problem
//----------------------------------------------------------------------
int readerCount = 0, writerCount = 0, waitingReader = 0;
Lock rwcLock("rwcLock");
Condition readyToRead("readyToRead");
Condition readyToWrite("readyToWrite");
char bufRWP;

void rwRead()
{
    int cnt = 3;
    while (cnt--)
        {
            rwcLock.Acquire();
            while (writerCount > 0)
                {
                    waitingReader++;
                    readyToRead.Wait(&rwcLock);
                    waitingReader--;
                }
            readerCount++;
            rwcLock.Release();

            printf("%s is reading the buf.\n", currentThread->getName());
            printf("Buf: %c\n", bufRWP);

            rwcLock.Acquire();
            readerCount--;
            if (readerCount == 0 && writerCount == 0)
                readyToWrite.Signal(&rwcLock);
            rwcLock.Release();
        }
}

void rwWrite()
{
    int cnt = 1;
    while (cnt--)
        {
            rwcLock.Acquire();
            while (readerCount > 0 || writerCount > 0 || waitingReader > 0)
                readyToWrite.Wait(&rwcLock);
            writerCount++;
            rwcLock.Release();

            bufRWP = (currentThread->getName())[7];
            printf("%s is writing the buf.\n", currentThread->getName());
            printf("Buf: %c\n", bufRWP);

            rwcLock.Acquire();
            writerCount--;
            if (waitingReader > 0)
                readyToRead.Broadcast(&rwcLock);
            else if (writerCount == 0)
                readyToWrite.Signal(&rwcLock);
            rwcLock.Release();
        }
}

void ThreadTest5()
{
    bufRWP = '\0';
    readerCount = 0;
    writerCount = 0;
    waitingReader = 0;
    Thread *readThread[4], *writeThread[3];
    readThread[0] = threadPool->createThread("Reader 0");
    readThread[1] = threadPool->createThread("Reader 1");
    readThread[2] = threadPool->createThread("Reader 2");
    readThread[3] = threadPool->createThread("Reader 3");
    writeThread[0] = threadPool->createThread("Writer 0");
    writeThread[1] = threadPool->createThread("Writer 1");
    writeThread[2] = threadPool->createThread("Writer 2");

    for (int i = 0; i < 3; ++i)
        writeThread[i]->Fork(rwWrite, (void *)1);
    for (int i = 0; i < 4; ++i)
        readThread[i]->Fork(rwRead, (void *)1);

    ThreadStatus();
}

//----------------------------------------------------------------------
// ThreadTest6  // Barrier Test
//----------------------------------------------------------------------
// Barrier barrierEven("BarrierEven", 5);
// Barrier barrierOdd("BarrierOdd", 5);

// void AddToBarrierOdd()
// {
//     printf("%s\n",currentThread->getName());
//     barrierOdd.Add();
//     ThreadHello();
//     currentThread->Yield();
// }

// void AddToBarrierEven()
// {
//     printf("%s\n",currentThread->getName());
//     barrierEven.Add();
//     ThreadHello();
//     currentThread->Yield();
// }

// void ThreadTest6()
// {
//     int p[10] = {1, 3, 6, 7, 9, 2, 4, 10, 8, 5};
//     Thread *pool1[10];
//     for (int i = 0; i < 10; ++i)
//         {
//             char name[10];
//             memset(name, 0, 10);
//             sprintf(name, "%d_1\0", p[i]-1);
//             pool1[i] = threadPool->createThread(name);
//         }

//     for (int i = 0; i < 10; ++i)
//         pool1[p[i]-1]->Fork(ThreadHello, (void *)1);

//     printf("\n\n");

//     Thread *pool2[10];
//     for (int i = 0; i < 10; ++i)
//         {
//             char name[10];
//             memset(name, 0, 10);
//             sprintf(name, "%d_2\0", p[i]-1);
//             pool2[i] = threadPool->createThread(name);
//         }

//     for (int i = 0; i < 10; ++i)
//         {
//             if (((p[i]-1) % 2) == 0)
//                 pool2[p[i]-1]->Fork(AddToBarrierEven, (void *)1);
//             else
//                 pool2[p[i]-1]->Fork(AddToBarrierOdd, (void *)1);
//         }
//     ThreadStatus();
// }

//----------------------------------------------------------------------
// ThreadTest7  // RWLock Test
//----------------------------------------------------------------------

RWLock rwlock("RWLock");
void rwlockRead()
{
    int cnt = 3;
    while (cnt--)
        {
            rwcLock.Acquire();
            rwlock.DownRead(&rwcLock);
            rwcLock.Release();

            printf("%s is reading the buf.\n", currentThread->getName());
            printf("Buf: %c\n", bufRWP);

            rwcLock.Acquire();
            rwlock.UpRead(&rwcLock);
            rwcLock.Release();
        }
}

void rwlockWrite()
{
    int cnt = 1;
    while (cnt--)
        {
            rwcLock.Acquire();
            rwlock.DownWrite(&rwcLock);
            rwcLock.Release();

            bufRWP = (currentThread->getName())[7];
            printf("%s is writing the buf.\n", currentThread->getName());
            printf("Buf: %c\n", bufRWP);

            rwcLock.Acquire();
            rwlock.UpWrite(&rwcLock);
            rwcLock.Release();
        }
}

void ThreadTest7()
{
    bufRWP = '\0';
    readerCount = 0;
    writerCount = 0;
    waitingReader = 0;
    Thread *readThread[4], *writeThread[3];
    readThread[0] = threadPool->createThread("Reader 0");
    readThread[1] = threadPool->createThread("Reader 1");
    readThread[2] = threadPool->createThread("Reader 2");
    readThread[3] = threadPool->createThread("Reader 3");
    writeThread[0] = threadPool->createThread("Writer 0");
    writeThread[1] = threadPool->createThread("Writer 1");
    writeThread[2] = threadPool->createThread("Writer 2");

    for (int i = 0; i < 3; ++i)
        writeThread[i]->Fork(rwlockWrite, (void *)1);
    for (int i = 0; i < 4; ++i)
        readThread[i]->Fork(rwlockRead, (void *)1);

    ThreadStatus();
}
//----------------------------------------------------------------------
// ThreadTest
// 	Invoke a test routine.
//----------------------------------------------------------------------

void ThreadTest()
{
    switch (testnum)
        {
            case 1:
                ThreadTest1();
                break;
            case 2:
                ThreadTest2();
                break;
            case 3:
                ThreadTest3();
                break;
            case 4:
                ThreadTest4();
                break;
            case 5:
                ThreadTest5();
                break;
            case 6:
                // ThreadTest6();
                break;
            case 7:
                ThreadTest7();
                break;
            default:
                printf("No test specified. TestNum: %d\n", testnum);
                break;
        }
}
