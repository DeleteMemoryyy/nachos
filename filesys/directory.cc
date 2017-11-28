// directory.cc
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//	Fixing this is one of the parts to the assignment.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "directory.h"
#include "copyright.h"
#include "filehdr.h"
#include "utility.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
    table = new DirectoryEntry[size];
    tableSize = size;
    for (int i = 0; i < tableSize; i++)
        table[i].inUse = FALSE;
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{
    delete[] table;
}

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void Directory::FetchFrom(OpenFile *file)
{
    (void)file->ReadAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void Directory::WriteBack(OpenFile *file)
{
    (void)file->WriteAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int Directory::FindIndex(char *name)
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse && !strncmp(table[i].name, name, FileNameMaxLen))
            return i;
    return -1;  // name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int Directory::Find(char *name)
{
    char *nameBuf = new char[FileNameMaxLen + 1];
    memset(nameBuf, 0, sizeof(char) * (FileNameMaxLen + 1));
    int nameLenThisLevel = 0, nameLenNext = 0;
    char *nameCur = name;

    // remove this level name
    while (*nameCur != '/')
        {
            ASSERT(*nameCur != 0);
            nameLenThisLevel++;
            nameCur++;
        }
    nameLenThisLevel++;
    nameCur++;

    // get next level name
    while (*nameCur != 0)
        {
            nameBuf[nameLenNext] = *nameCur;
            nameLenNext++;
            nameCur++;
            if (*(nameCur - 1) == '/')
                break;
        }

    if (*nameCur == 0)  // is last level
        {
            delete[] nameBuf;
            int idx = FindIndex(name + nameLenThisLevel);
            if (idx != -1)
                return table[idx].sector;
            return -1;
        }
    else  // recursive find
        {
            int idx = FindIndex(nameBuf);
            delete[] nameBuf;
            if (idx != -1)
                {
                    ASSERT(table[idx].type == 0);
                    OpenFile *nextDirectoryFile = new OpenFile(table[idx].sector);
                    Directory *nextDirectory = new Directory(NumDirEntries);
                    nextDirectory->FetchFrom(nextDirectoryFile);
                    int result = nextDirectory->Find(name + nameLenThisLevel);
                    delete nextDirectory;
                    delete nextDirectoryFile;
                    return result;
                }
            return FALSE;
        }
}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool Directory::Add(char *name, int newSector)
{
    char *nameBuf = new char[FileNameMaxLen + 1];
    memset(nameBuf, 0, sizeof(char) * (FileNameMaxLen + 1));
    int nameLenThisLevel = 0, nameLenNext = 0;
    char *nameCur = name;

    // remove this level name
    while (*nameCur != '/')
        {
            ASSERT(*nameCur != 0);
            nameLenThisLevel++;
            nameCur++;
        }
    nameLenThisLevel++;
    nameCur++;

    // get next level name
    while (*nameCur != 0)
        {
            nameBuf[nameLenNext] = *nameCur;
            nameLenNext++;
            nameCur++;
            if (*(nameCur - 1) == '/')
                break;
        }

    if (*nameCur == 0)  // is last level
        {
            if (FindIndex(nameBuf) != -1)
                {
                    delete[] nameBuf;
                    return FALSE;
                }
            for (int i = 0; i < tableSize; i++)
                if (!table[i].inUse)
                    {
                        table[i].inUse = TRUE;
                        strncpy(table[i].name, nameBuf, FileNameMaxLen);
                        if (nameBuf[nameLenNext - 1] == '/')
                            table[i].type = 0;
                        else
                            table[i].type = 1;
                        table[i].sector = newSector;
                        delete[] nameBuf;
                        return TRUE;
                    }
            delete[] nameBuf;
            return FALSE;  // no space.  Fix when we have extensible files.
        }
    else  // recursive find
        {
            int idx = FindIndex(nameBuf);
            delete[] nameBuf;
            if (idx != -1)
                {
                    ASSERT(table[idx].type == 0);
                    OpenFile *nextDirectoryFile = new OpenFile(table[idx].sector);
                    Directory *nextDirectory = new Directory(NumDirEntries);
                    nextDirectory->FetchFrom(nextDirectoryFile);
                    bool result = nextDirectory->Add(name + nameLenThisLevel, newSector);
                    delete nextDirectory;
                    delete nextDirectoryFile;
                    return result;
                }
            return FALSE;
        }
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory.
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool Directory::Remove(char *name, BitMap *freeMap)
{
    int i = FindIndex(name);

    if (i == -1)
        return FALSE;  // name not in directory
    table[i].inUse = FALSE;
    return TRUE;

    char *nameBuf = new char[FileNameMaxLen + 1];
    memset(nameBuf, 0, sizeof(char) * (FileNameMaxLen + 1));
    int nameLenThisLevel = 0, nameLenNext = 0;
    char *nameCur = name;

    // remove this level name
    while (*nameCur != '/')
        {
            ASSERT(*nameCur != 0);
            nameLenThisLevel++;
            nameCur++;
        }
    nameLenThisLevel++;
    nameCur++;

    // get next level name
    while (*nameCur != 0)
        {
            nameBuf[nameLenNext] = *nameCur;
            nameLenNext++;
            nameCur++;
            if (*(nameCur - 1) == '/')
                break;
        }

    if (*nameCur == 0)  // is last level
        {
            delete[] nameBuf;
            int idx = FindIndex(name + nameLenThisLevel);
            if (idx == -1)
                {
                    return FALSE;
                }
            if (table[idx].type == 0)
                {
                    OpenFile *deleteDirectoryFile = new OpenFile(table[idx].sector);
                    Directory *deleteDirectory = new Directory(NumDirEntries);
                    deleteDirectory->FetchFrom(deleteDirectoryFile);
                    deleteDirectory->RemoveAllFiles(freeMap);
                    delete deleteDirectory;
                    delete deleteDirectoryFile;
                }
            table[idx].inUse = FALSE;
            return TRUE;
        }
    else
        {
            int idx = FindIndex(nameBuf);
            if (idx != -1)
                {
                    ASSERT(table[idx].type == 0);
                    OpenFile *nextDirectoryFile = new OpenFile(table[idx].type);
                    Directory *nextDirectory = new Directory(NumDirEntries);
                    nextDirectory->FetchFrom(nextDirectoryFile);
                    bool result = nextDirectory->Remove(name + nameLenThisLevel, freeMap);
                    delete nextDirectory;
                    delete nextDirectoryFile;
                    delete[] nameBuf;
                    return result;
                }
        }
}

void Directory::RemoveAllFiles(BitMap *freeMap)
{
    if (fileSystem != NULL)
        for (int i = 0; i < tableSize; ++i)
            {
                if (table[i].inUse && fileSystem->referenceCount[table[i].sector] == 0)
                    {
                        if (table[i].type == 0)
                            {
                                OpenFile *deleteDirectoryFile = new OpenFile(table[i].sector);
                                Directory *deleteDirectory = new Directory(NumDirEntries);
                                deleteDirectory->FetchFrom(deleteDirectoryFile);
                                deleteDirectory->RemoveAllFiles(freeMap);
                                delete deleteDirectory;
                                delete deleteDirectoryFile;
                            }
                        FileHeader *deleteHdr = new FileHeader;
                        deleteHdr->FetchFrom(table[i].sector);
                        deleteHdr->Deallocate(freeMap);
                        freeMap->Clear(table[i].sector);
                        delete deleteHdr;
                    }
            }
}


//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory.
//----------------------------------------------------------------------

void Directory::List()
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse)
            printf("%s\n", table[i].name);
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void Directory::Print()
{
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse)
            {
                printf("Name: %s, Sector: %d\n", table[i].name, table[i].sector);
                hdr->FetchFrom(table[i].sector);
                hdr->Print();
            }
    printf("\n");
    delete hdr;
}
