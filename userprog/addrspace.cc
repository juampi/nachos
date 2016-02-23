// addrspace.cc
//  Routines to manage address spaces (executing user programs).
//
//  In order to run a user program, you must:
//
//  1. link with the -N -T 0 option
//  2. run coff2noff to convert the object file to Nachos format
//      (Nachos object code format is essentially just a simpler
//      version of the UNIX executable object code format)
//  3. load the NOFF file into the Nachos file system
//      (if you haven't implemented the file system yet, you
//      don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"

//----------------------------------------------------------------------
// SwapHeader
//  Do little endian to big endian conversion on the bytes in the
//  object file header, in case the file was generated on a little
//  endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void
SwapHeader(NoffHeader *noffH) {
    noffH->noffMagic = WordToHost(noffH->noffMagic);
    noffH->code.size = WordToHost(noffH->code.size);
    noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
    noffH->initData.size = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
//  Create an address space to run a user program.
//  Load the program from a file "executable", and set everything
//  up so that we can start executing user instructions.
//
//  Assumes that the object code file is in NOFF format.
//
//  First, set up the translation from program memory to physical
//  memory.  For now, this is really simple (1:1), since we are
//  only uniprogramming, and we have a single unsegmented page table
//
//  "executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executableInput) {
    unsigned int i, size;

    executable = executableInput;

    executable->ReadAt(reinterpret_cast<char *>(&noffH), sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
        (WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

    // how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size
            + UserStackSize;  // we need to increase the size
                            // to leave room for the stack
    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    // check we're not trying to run anything too big --
    // at least until we have virtual memory
    // ASSERT(numPages <= NumPhysPages);

    DEBUG('a', "Initializing address space, num pages %d, size %d\n",
        numPages, size);

    // first, set up the translation
    pageTable = new TranslationEntry[numPages];
    #ifdef DEMAND_PAGING
    shadowTable = new pageState[numPages];
    #endif
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;
        #ifdef DEMAND_PAGING
        pageTable[i].physicalPage = -1;
        pageTable[i].valid = false;
        shadowTable[i] = kNotInMemory;
        #else
        pageTable[i].physicalPage = freeList->Find();
        pageTable[i].valid = true;
        #endif
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        // if the code segment was entirely on
        // a separate page, we could set its
        // pages to be read-only
        pageTable[i].readOnly = false;

        // zero out the entire address space,
        // to zero the unitialized data segment and the stack segment
        #ifndef DEMAND_PAGING
        bzero(&machine->mainMemory[pageTable[i].physicalPage * PageSize],
            PageSize);
        #endif
    }

    #ifndef DEMAND_PAGING
    // then, copy in the code and data segments into memory
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
            noffH.code.virtualAddr, noffH.code.size);
        executable->ReadAt(&(machine->mainMemory[Translate(noffH.code.virtualAddr)]),
            noffH.code.size, noffH.code.inFileAddr);
    }

    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
            noffH.initData.virtualAddr, noffH.initData.size);
        executable->ReadAt(&(machine->mainMemory[Translate(noffH.initData.virtualAddr)]),
            noffH.initData.size, noffH.initData.inFileAddr);
    }
    #endif
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
//  Dealloate an address space.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace() {
    for (unsigned int i = 0; i < numPages; i++) {
        if (pageTable[i].valid) {
            DEBUG('v', "Clearing virtual page number %d from physical page number %d\n",
                pageTable[i].virtualPage,
                pageTable[i].physicalPage);
            freeList->Clear(pageTable[i].physicalPage);
        }
    }
    delete[] pageTable;
    #ifdef DEMAND_PAGING
    delete[] shadowTable;
    #endif
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
//  Set the initial values for the user-level register set.
//
//  We write these directly into the "machine" registers, so
//  that we can immediately jump to user code.  Note that these
//  will be saved/restored into the currentThread->userRegisters
//  when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters() {
    int i;

    for (i = 0; i < NumTotalRegs; i++)
    machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

    // Set the stack register to the end of the address space, where we
    // allocated the stack; but subtract off a bit, to make sure we don't
    // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
//  On a context switch, save any machine state, specific
//  to this address space, that needs saving.
//
//  For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() {
    #ifdef USE_TLB
    for (int i = 0; i < TLBSize; i++) {
        if (machine->tlb[i].valid && machine->tlb[i].dirty) {
            pageTable[machine->tlb[i].virtualPage] = machine->tlb[i];
        }
    }
    #endif
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
//  On a context switch, restore the machine state so that
//  this address space can run.
//
//  For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() {
    #ifdef USE_TLB
    for (int i = 0; i < TLBSize; i++) {
        machine->tlb[i].valid = false;
    }
    #else
        machine->pageTable = pageTable;
        machine->pageTableSize = numPages;
    #endif
}

int AddrSpace::Translate(int virtualAddress) {
    int virtualPage = virtualAddress / PageSize;
    int offset = virtualAddress % PageSize;
    return pageTable[virtualPage].physicalPage * PageSize + offset;
}

TranslationEntry* AddrSpace::GetPage(int virtualPageNumber) {
    #ifdef DEMAND_PAGING
    switch (shadowTable[virtualPageNumber]) {
        case kNotInMemory:
            LoadPage(virtualPageNumber);
            break;
        case kInMemory:
            break;
        case kSwapped:
            break;
        default:
            break;
    }
    #endif
    return &pageTable[virtualPageNumber];
}


void AddrSpace::LoadPage(int virtualPageNumber) {
    int virtualAddress = virtualPageNumber * PageSize;
    pageTable[virtualPageNumber].physicalPage = freeList->Find();
    pageTable[virtualPageNumber].valid = true;
    shadowTable[virtualPageNumber] = kInMemory;

    DEBUG('v', "Loading virtual page number %d into physical page number %d\n",
        virtualPageNumber, pageTable[virtualPageNumber].physicalPage);

    if (noffH.code.size > 0) {
        executable->ReadAt(&(machine->mainMemory[Translate(virtualAddress)]),
            PageSize,
            noffH.code.inFileAddr + virtualAddress - noffH.code.virtualAddr);
    }

    if (noffH.initData.size > 0) {
        executable->ReadAt(&(machine->mainMemory[Translate(virtualAddress)]),
            PageSize,
            noffH.initData.inFileAddr + virtualAddress - noffH.initData.virtualAddr);
    }
}

