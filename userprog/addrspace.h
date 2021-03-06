// addrspace.h
//  Data structures to keep track of executing user programs
//  (address spaces).
//
//  For now, we don't keep any information about address spaces.
//  The user level CPU state is saved and restored in the thread
//  executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include "noff.h"

#define UserStackSize 1024  // Increase this as necessary!

enum pageState {
    kNotInMemory,
    kInMemory,
    kSwappedOut
};

class AddrSpace {
 public:
    // Create an address space, initializing it with the program
    // stored in the file "executable"
    explicit AddrSpace(OpenFile *executable);

    // De-allocate an address space
    ~AddrSpace();

    // Initialize user-level CPU registers, before jumping to user code
    void InitRegisters();

    // Save/restore address space-specific info on a context switch
    void SaveState();
    void RestoreState();

    int Translate(int virtualAddress);

    TranslationEntry* GetPage(int virtualPageNumber);

    #ifdef DEMAND_PAGING
    void LoadPage(int virtualPageNumber);
    #endif

    #ifdef PAGING
    void SwapIn(int virtualPage);
    void SwapOut(int virtualPage);
    int MakeRoom();
    int Clock();
    #endif

 private:
    // Assume linear page table translation for now!
    TranslationEntry *pageTable;

    // Number of pages in the virtual address space
    unsigned int numPages;

    OpenFile* executable;
    NoffHeader noffH;

    #ifdef DEMAND_PAGING
    // Table for keeping track of pages state when using demand paging
    pageState* shadowTable;
    #endif

    #ifdef PAGING
    char* swapName;
    OpenFile* swap;
    #endif
};

#endif  // ADDRSPACE_H
