// filehdr.cc
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector,
//
//      Unlike in a real system, we do not keep track of file permissions,
//	ownership, last modification date, etc., in the file header.
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "filehdr.h"
#include "system.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool FileHeader::Allocate(BitMap *freeMap, int fileSize)
{
    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);

    if (numSectors <= NumPrimarySector)
        {
            if (freeMap->NumClear() < numSectors)
                return FALSE;  // not enough space
            for (int i = 0; i < numSectors; i++)
                primarySectors[i] = freeMap->Find();
        }
    else
        {
            int restNumSectors = numSectors - NumPrimarySector;
            int numSecondaryIndex = divRoundUp(restNumSectors, NumDirectPerSector);
            if (freeMap->NumClear() < numSectors + numSecondaryIndex)
                return FALSE;  // not enough space
            for (int i = 0; i < NumPrimarySector; ++i)
                primarySectors[i] = freeMap->Find();
            for (int i = 0; i < numSecondaryIndex; ++i)
                {
                    secondarySectors[i] = freeMap->Find();
                    int *writtenSector = new int[NumDirectPerSector];
                    for (int j = 0; j < NumDirectPerSector && restNumSectors > 0;
                         ++j, --restNumSectors)
                        writtenSector[j] = freeMap->Find();
                    synchDisk->WriteSector(secondarySectors[i], (char *)writtenSector);
                    delete[] writtenSector;
                }
        }
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void FileHeader::Deallocate(BitMap *freeMap)
{
    if (numSectors <= NumPrimarySector)
        for (int i = 0; i < numSectors; i++)
            {
                ASSERT(freeMap->Test((int)primarySectors[i]));  // ought to be marked!
                freeMap->Clear((int)primarySectors[i]);
            }
    else
        {
            for (int i = 0; i < NumPrimarySector; ++i)
                {
                    ASSERT(freeMap->Test((int)primarySectors[i]));  // ought to be marked!
                    freeMap->Clear((int)primarySectors[i]);
                }
            int restNumSectors = numSectors - NumPrimarySector;
            int numSecondaryIndex = divRoundUp(restNumSectors, NumDirectPerSector);
            int *writtenSector = new int[NumDirectPerSector];
            for (int i = 0; i < numSecondaryIndex; ++i)
                {
                    ASSERT(freeMap->Test((int)secondarySectors[i]));  // ought to be marked!
                    synchDisk->ReadSector(secondarySectors[i], (char *)writtenSector);
                    for (int j = 0; j < NumDirectPerSector && restNumSectors > 0;
                         ++j, --restNumSectors)
                        {
                            ASSERT(freeMap->Test((int)writtenSector[j]));  // ought to be marked!
                            freeMap->Clear((int)writtenSector[j]);
                        }
                    freeMap->Clear((int)secondarySectors[i]);
                }
            delete[] writtenSector;
        }
}

bool FileHeader::ExtentAllocate(BitMap *freeMap, int fileSize)
{
    int extBytes = fileSize - (SectorSize - (numBytes % SectorSize));
    numBytes += fileSize;
    if (extBytes <= 0)
        {
            return TRUE;
        }
    int extNumSectors = divRoundUp(extBytes, SectorSize);
    int newNumSectors = extNumSectors + numSectors;
    for (int vSector = numSectors; vSector < newNumSectors; ++vSector)
        {
            if (vSector < NumPrimarySector)
                {
                    int new_sector = freeMap->Find();
                    if (new_sector == -1)
                        return FALSE;
                    primarySectors[vSector] = new_sector;
                }
            else
                {
                    int restNumSectors = vSector - NumPrimarySector;
                    int numSecondaryIndex = restNumSectors / NumDirectPerSector;
                    int *writtenSector = new int[NumDirectPerSector];
                    if (restNumSectors % NumDirectPerSector == 0)
                        {
                            int new_sector = freeMap->Find();
                            if (new_sector == -1)
                                return FALSE;
                            secondarySectors[restNumSectors] = new_sector;
                        }
                    else
                        {
                            freeMap->Test(numSecondaryIndex);
                            synchDisk->ReadSector(secondarySectors[numSecondaryIndex],
                                                  (char *)writtenSector);
                        }
                    int new_sector = freeMap->Find();
                    if (new_sector == -1)
                        return FALSE;
                    writtenSector[restNumSectors - NumDirectPerSector * numSecondaryIndex] =
                        new_sector;
                    synchDisk->WriteSector(secondarySectors[numSecondaryIndex],
                                           (char *)writtenSector);
                    delete[] writtenSector;
                }
        }
    numSectors = newNumSectors;
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk.
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int FileHeader::ByteToSector(int offset)
{
    int vSector = offset / SectorSize;
    if (vSector < NumPrimarySector)
        return (primarySectors[vSector]);
    else
        {
            int restNumSectors = vSector - NumPrimarySector;
            int numSecondaryIndex = restNumSectors / NumDirectPerSector;
            int *writtenSector = new int[NumDirectPerSector];
            synchDisk->ReadSector(secondarySectors[numSecondaryIndex], (char *)writtenSector);
            // printf("        second number %d, in secondary:%d \n", numSecondaryIndex,
            //        restNumSectors - NumDirectPerSector * numSecondaryIndex);
            int sector = writtenSector[restNumSectors - NumDirectPerSector * numSecondaryIndex];
            delete writtenSector;
            // printf("     secondary sector:%d\n", sector);
            return sector;
        }
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int FileHeader::FileLength()
{
    return numBytes;
}


void FileHeader::SetCreatedTime()
{
    time_t now;
    char *dateTime;
    time(&now);
    dateTime = ctime(&now);

    strncpy(timeCreated, dateTime, TimeInfoSize);
}

void FileHeader::UpdateLastUsedTime()
{
    time_t now;
    char *dateTime;
    time(&now);
    dateTime = ctime(&now);

    strncpy(timeLastUsed, dateTime, TimeInfoSize);
}

void FileHeader::UpdateLastModifiedTime()
{
    time_t now;
    char *dateTime;
    time(&now);
    dateTime = ctime(&now);

    strncpy(timeLastModified, dateTime, TimeInfoSize);
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void FileHeader::Print()
{
    int i, j, k, l;
    char *data = new char[SectorSize];

    printf("\tFileHeader contents.  File size: %d.  File blocks: %d\n", numBytes, numSectors);
    // printf("\tCreation time: %s", timeCreated);
    // printf("\tLast open time: %s", timeLastUsed);
    // printf("\tLast modified time: %s", timeLastModified);
    if (numSectors <= NumPrimarySector)
        {
            printf("\tPrimary sectorys:\n\t Sector number: ");
            for (i = 0; i < numSectors; i++)
                {
                    printf("%d ", primarySectors[i]);
                    // synchDisk->ReadSector(primarySectors[i], data);
                    // printf("Contents: ");
                    // for (k = 0; (k < SectorSize) && (l < numBytes); j++, l++)
                    //     {
                    //         if ('\040' <= data[k] && data[k] <= '\176')  // isprint(data[j])
                    //             printf("%c", data[k]);
                    //         else
                    //             printf("\\%x", (unsigned char)data[k]);
                    //     }
                }
            printf("\n");
        }
    else
        {
            printf("\tPrimary sectorys:\n\t Sector number: ");
            for (i = 0; i < NumPrimarySector; i++)
                printf("%d ", primarySectors[i]);
            printf("\n");
            int restNumSectors = numSectors - NumPrimarySector;
            int numSecondaryIndex = divRoundUp(restNumSectors, NumDirectPerSector);
            int *writtenSector = new int[NumDirectPerSector];
            for (i = 0; i < numSecondaryIndex; ++i)
                {
                    printf("\tSecondary sectory %d:\n\t Sector number: ", i);
                    synchDisk->ReadSector(secondarySectors[i], (char *)writtenSector);
                    for (j = 0; j < NumDirectPerSector && restNumSectors > 0; ++j, --restNumSectors)
                        {
                            printf("%d ", writtenSector[j]);
                            // synchDisk->ReadSector(writtenSector[j], data);
                            // printf("Contents: ");
                            // for (k = 0; (k < SectorSize) && (l < numBytes); j++, l++)
                            //     {
                            //         if ('\040' <= data[k] && data[k] <= '\176')  //
                            //         isprint(data[j])
                            //             printf("%c", data[k]);
                            //         else
                            //             printf("\\%x", (unsigned char)data[k]);
                            //     }
                        }
                    printf("\n");
                }
            delete[] writtenSector;
        }
    delete[] data;
}
