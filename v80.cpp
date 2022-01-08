/**
 @file v80.cpp

 @brief based on TRS-80 Virtual Disk Kit v1.7 for Windows by Miguel Dutra
 Linux port VDK-80-Linux done by Mike Gore, 2016

 @par Tools to Read and Write files inside common TRS-80 emulator images

 @par Copyright &copy; 2016 Miguel Dutra, GPL License
 @par You are free to use this code under the terms of GPL
   please retain a copy of this notice in any code you use it in.

 This is free software: you can redistribute it and/or modify it under the 
 terms of the GNU General Public License as published by the Free Software 
 Foundation, either version 3 of the License, or (at your option) any later version.

 The software is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 @par Original Windows Code
  @see http://www.trs-80.com/wordpress/category/contributors/miguel-dutra/
  @see http://www.trs-80.com/wordpress/dsk-and-dmk-image-utilities/
  @see Miguel Dutra www.mdutra.com
*/
//---------------------------------------------------------------------------------
// TRS-80 Virtual Disk Kit                                  Written by Miguel Dutra
//---------------------------------------------------------------------------------

#define DEFINE_ERRORS_MSG 

#include "windows.h"
#include <typeinfo>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>


#include "v80.h"
#include "vdi.h"
#include "jv1.h"
#include "jv3.h"
#include "dmk.h"
#include "osi.h"
#include "td4.h"
#include "td3.h"
#include "td1.h"
#include "rd.h"
#include "md.h"
#include "nd.h"
#include "dd.h"
#include "cpm.h"

#define MAX_FILE_SIZE (40L*2L*18L*256L)

//---------------------------------------------------------------------------------
// Function Definitions
//---------------------------------------------------------------------------------

// Core operations

DWORD   Dir();
DWORD   Get();
DWORD   Put();
DWORD   Ren();
DWORD   Del();
DWORD   DumpDisk();
DWORD   DumpFile();

// Auxiliary functions

DWORD   LoadVDI();
DWORD   LoadOSI();
void    Dump(unsigned char* pBuffer, int nSize);
bool    WildComp(const char* pSource, const char* pMask, BYTE nLength);
void    WildCopy(const char* pSource, char* pTarget, const char* pMask, BYTE nLength);
void    FmtName(const char szName[9], const char szType[4], const char* szDivider, char szNewName[13]);
void    Win2TRS(const char* pWinName, char cTRSName[11]);
void    Trim(char cField[8]);

// Command-line related

DWORD   ParseCmdLine(int argc, char* argv[]);
DWORD   SetCmd(void* pParam);
DWORD   SetOpt(void* pParam);
DWORD   SetVDI(void* pParam);
DWORD   SetOSI(void* pParam);

void    PrintHelp();
void    PrintError(DWORD dwError);

//---------------------------------------------------------------------------------
// Data structures
//---------------------------------------------------------------------------------

char*   gpFileSpec[4] = {};
DWORD   (*gpCommand)() = NULL;
FILE    *ghFile = NULL;
CVDI*   gpVDI = NULL;
COSI*   gpOSI = NULL;
DWORD   gdwFlags = 0;

struct SWITCH
{
    const char* cName;
    DWORD       (*pCall)(void*);
    void*       pParam;
    const char* cDescr;
};

SWITCH  gSwitches[] =
{
    { "-l",     SetCmd, (void*)Dir,                 "List directory (default)"                          },
    { "-r",     SetCmd, (void*)Get,                 "Read files"                                        },
    { "-w",     SetCmd, (void*)Put,                 "Write files"                                       },
    { "-n",     SetCmd, (void*)Ren,                 "Rename files"                                      },
    { "-k",     SetCmd, (void*)Del,                 "Delete files"                                      },
    { "-f",     SetCmd, (void*)DumpFile,            "Dump file contents"                                },
    { "-d",     SetCmd, (void*)DumpDisk,            "Dump disk contents"                                },
    { "-s",     SetOpt, (void*)V80_FLAG_SYSTEM,     "Include system files"                              },
    { "-i",     SetOpt, (void*)V80_FLAG_INVISIBLE,  "Include invisible files"                           },
    { "-x",     SetOpt, (void*)V80_FLAG_INFO,       "Show extra information"                            },
    { "-p",     SetOpt, (void*)V80_FLAG_CHKDSK,     "Skip the disk parameters check"                    },
    { "-c",     SetOpt, (void*)V80_FLAG_CHKDIR,     "Skip the directory structure check"                },
    { "-g",     SetOpt, (void*)V80_FLAG_GATFIX,     "Skip the GAT auto-fix in TRSDOS system disks"      },
    { "-b",     SetOpt, (void*)V80_FLAG_READBAD,    "Read as much as possible from bad files"           },
    { "-ss",    SetOpt, (void*)V80_FLAG_SS,         "Force the disk as single-sided"                    },
    { "-ds",    SetOpt, (void*)V80_FLAG_DS,         "Force the disk as double-sided"                    },
    { "-dmk",   SetVDI, (void*)new CDMK,            "Force the DMK disk interface"                      },
    { "-jv1",   SetVDI, (void*)new CJV1,            "Force the JV1 disk interface"                      },
    { "-jv3",   SetVDI, (void*)new CJV3,            "Force the JV3 disk interface"                      },
    { "-cpm",   SetOSI, (void*)new CCPM,            "Force the CP/M system interface (INCOMPLETE)"      },
    { "-dd",    SetOSI, (void*)new CDD,             "Force the DoubleDOS system interface"              },
    { "-md",    SetOSI, (void*)new CMD,             "Force the MicroDOS/OS-80 III system interface"     },
    { "-nd",    SetOSI, (void*)new CND,             "Force the NewDOS/80 system interface"              },
    { "-rd",    SetOSI, (void*)new CRD,             "Force the RapiDOS system interface"                },
    { "-td1",   SetOSI, (void*)new CTD1,            "Force the TRSDOS Model I system interface"         },
    { "-td3",   SetOSI, (void*)new CTD3,            "Force the TRSDOS Model III system interface"       },
    { "-td4",   SetOSI, (void*)new CTD4,            "Force the TRSDOS Model 4 system interface"         }
};

const char* gCategories[4] = { "Commands", "Options", "Disk Interfaces", "DOS Interfaces" };

//---------------------------------------------------------------------------------
// Main
//---------------------------------------------------------------------------------

int main(int argc, char* argv[])
{

    DWORD   dwError;

    // Print authoring information
    puts("VDK-80, The TRS-80 Virtual Disk Kit v1.7");
    puts("Written by Miguel Dutra (www.mdutra.com)");
    puts("Updated for Linux by Mike Gore github.com/magore");

    // Print usage instructions when no parameters are provided
    if (argc == 1)
    {
        PrintHelp();
        goto Exit_1;
    }

    // Process command line parameters
    if ((dwError = ParseCmdLine(argc, argv)) != 0)
	{
		printf("ParseCmdLine: error\n");
        goto Exit_1;
	}

    // Check whether user has provided a disk image filename
    if (gpFileSpec[1] == NULL)
    {
        puts("Missing the disk image filename.");
        goto Exit_1;
    }

    // Open disk image
    if ( (ghFile = fopen(gpFileSpec[1], "r+")) == NULL )
    {
		printf("Can not open: %s\n", gpFileSpec[1]);
        goto Exit_2;
    }

    // Set a command to execute if the user hasn`t indicated one (first one in the array)
    if (gpCommand == NULL)
        gpCommand = (DWORD (*)())gSwitches[0].pParam;

    // Execute the command with parameters previously parsed from the command-line
    dwError = gpCommand();

    // Close the file
    Exit_3:
    if (ghFile != NULL)
        fclose(ghFile);

    // Print error message, if any
    Exit_2:

    // Exit
    Exit_1:
    return 0;

}

//---------------------------------------------------------------------------------
// List disk files
//---------------------------------------------------------------------------------

DWORD Dir()
{

    OSI_FILE    File;
    char        cMask[11];
    char        szFile[13];
    void*       pFile = NULL;
    WORD        wFiles = 0;
    DWORD       dwSize = 0;
    DWORD       dwError = 0;
    DIR *dir;
    dirent *ent;
    struct stat st;

    // Initialize the disk interface
    if ((dwError = LoadVDI()) != 0)
        goto Exit_0;

    // Initialize the DOS interface
    if ((dwError = LoadOSI()) != 0)
        goto Exit_1;

    // Print operation objective
    printf("\r\nListing directory contents:\r\n\r\n");

    // Print header
    printf("Filename\t    Size\tDate\t\tAttr\r\n");
    printf("----------------------------------------------------\r\n");

    // Convert Windows filespec to TRS standard
    Win2TRS((gpFileSpec[2] != NULL ? gpFileSpec[2] : "*.*"), cMask);

    // While OSI::Dir() returns a valid file pointer
    while ((dwError = gpOSI->Dir(&pFile, (pFile == NULL ? OSI_DIR_FIND_FIRST : OSI_DIR_FIND_NEXT))) == 0)
    {

        // Get file properties
        gpOSI->GetFile(pFile, File);

        // Compare file attributes against user requests
        if ((File.bSystem && !(gdwFlags & V80_FLAG_SYSTEM)) || (File.bInvisible && !(gdwFlags & V80_FLAG_INVISIBLE)))
            continue;

        // Compare the filename against the source filespec
        if (!WildComp(File.szName, cMask, 8) || !WildComp(File.szType, &cMask[8], 3))
            continue;

        // Format the filename
        FmtName(File.szName, File.szType, (strcmp(typeid(*gpOSI).name()+2, "CPM") ? "/" : "."), szFile);

        // Print file information
        printf("%-12s\t%8u\t%04d/%02d/%02d\t%c%c%c%d\r\n", szFile, File.dwSize, File.Date.wYear, File.Date.nMonth, File.Date.nDay, (File.bSystem?'S':'-'), (File.bInvisible?'I':'-'), (File.bModified?'M':'-'), File.nAccess);

        // Update operation status variables
        wFiles++;
        dwSize += File.dwSize;

    }

    // Print operation summary
    printf("\r\nTotal of %d bytes in %d files listed.\r\n\r\n", dwSize, wFiles);

    // If exited on "No More Files" then "No Error"
    if (dwError == ERROR_NO_MORE_FILES)
        dwError = 0;

    // Release the OSI object
    if (gpOSI != NULL)
        delete gpOSI;

    // Release the VDI object
    Exit_1:
    if (gpVDI != NULL)
        delete gpVDI;

    Exit_0:
    return dwError;

}

//---------------------------------------------------------------------------------
// Extract files from the disk
//---------------------------------------------------------------------------------

DWORD Get()
{

    OSI_FILE    File;
    FILE		*hFile;
    char        cMask[11];
    char        szTRSFile[13];
    char        szWinFile[13];
    char        szFile[MAX_PATH];
    void*       pFile = NULL;
    WORD        wFiles = 0;
    DWORD       dwSize = 0;
    BYTE*       pBuffer = NULL;
    DWORD       dwBytes;
    DWORD       dwError = 0;

    // Initialize the disk interface
    dwError = LoadVDI();
    if (dwError)
        goto Exit_0;

    // Initialize the DOS interface
    dwError = LoadOSI();
    if (dwError)
        goto Exit_1;

    // 360KB should be more than enough for any TRS-80 file
    dwBytes = (MAX_FILE_SIZE) + (V80_MEM - (MAX_FILE_SIZE) % V80_MEM);

    // Allocate memory
    if ((pBuffer = (BYTE*)calloc(dwBytes,1)) == NULL)
    {
        perror("Get Memory");
		dwError = ERROR_OUTOFMEMORY;
        goto Exit_2;
    }

    // Print operation objective
    printf("\r\nReading files from disk:\r\n\r\n");

    // Convert Windows filespec to TRS standard
    Win2TRS((gpFileSpec[2] != NULL ? gpFileSpec[2] : "*.*"), cMask);

    // While OSI::Dir() returns a valid file pointer
    while ((dwError = gpOSI->Dir(&pFile, (pFile == NULL ? OSI_DIR_FIND_FIRST : OSI_DIR_FIND_NEXT))) == 0)
    {

        // Get file properties
        gpOSI->GetFile(pFile, File);

        // Compare file attributes against user requests
        if ((File.bSystem && !(gdwFlags & V80_FLAG_SYSTEM)) || (File.bInvisible && !(gdwFlags & V80_FLAG_INVISIBLE)))
            continue;

        // Compare the filename against the source filespec
        if (!WildComp(File.szName, cMask, 8) || !WildComp(File.szType, &cMask[8], 3))
            continue;

        // Format the filenames
        FmtName(File.szName, File.szType, (strcmp(typeid(*gpOSI).name()+2, "CPM") ? "/" : "."), szTRSFile);
        FmtName(File.szName, File.szType, ".", szWinFile);

        // Add the user specified path (if any) to the Windows-based filename
        sprintf(szFile, "%s/%s", (gpFileSpec[3] ? gpFileSpec[3] : "."), szWinFile);

        // Print filenames
        printf("%-12s -> %-12s\t", szTRSFile, szFile);

        // Check if the file is not empty
        if (File.dwSize == 0)
        {
            printf("%8d bytes\tSkipped\r\n", File.dwSize);
            continue;
        }

        // Check whether the file size is valid (<360KB)
        if (File.dwSize > MAX_FILE_SIZE)
        {
            printf("%8d bytes\tInvalid Size!\r\n", File.dwSize);
            continue;
        }

        // Set file pointer
        if ((dwError = gpOSI->Seek(pFile, 0)) != 0)
        {
            printf("Get seek error\n");
            continue;
        }

        // Read file contents
        if ((dwError = gpOSI->Read(pFile, pBuffer, File.dwSize)) != 0 && !(gdwFlags & V80_FLAG_READBAD))
        {
            printf("Get read error\n");
            continue;
        }

        // Create Windows file
        if ((hFile = fopen(szFile, "w")) == NULL)
        {
			printf("Can't open: %s\n", szFile);
            continue;
        }

		dwBytes = fwrite(pBuffer, 1, File.dwSize, hFile);

        // Write buffer contents to Windows file
        if (dwBytes != File.dwSize)
        {
			printf("Write error: %s\n", szFile);
            fclose(hFile);
            continue;
        }

        // Close file handle
        fclose(hFile);

        // Print total number of bytes extracted
        printf("%8d bytes\tOK\r\n", File.dwSize);

        // Update operation status variables
        wFiles++;
        dwSize += File.dwSize;

    }

    // Print operation summary
    printf("\r\nTotal of %d bytes read from %d files.\r\n\r\n", dwSize, wFiles);

    // If exited on "No More Files" then "No Error"
    if (dwError == ERROR_NO_MORE_FILES)
        dwError = 0;

    // Release allocated memory
    if (pBuffer != NULL)
        free(pBuffer);

    // Release the OSI object
    Exit_2:
    if (gpOSI != NULL)
        delete gpOSI;

    // Release the VDI object
    Exit_1:
    if (gpVDI != NULL)
        delete gpVDI;

	if(dwError)
		printf("Get dwError:%d\n", dwError);

    // Return
    Exit_0:
    return dwError;

}

//---------------------------------------------------------------------------------
// Write files to the disk
//---------------------------------------------------------------------------------

DWORD Put()
{

    FILE 	    *hFile;

    char            szFileSpec[MAX_PATH];
    char*           pFileName;
    char            cFile[11];
    char            szFile[13];
    OSI_FILE        File;
    void*           pFile;
    WORD            wFiles = 0;
    DWORD           dwSize = 0;
    BYTE*           pBuffer = NULL;
    DWORD           dwBytes;
    DWORD           dwError = 0;
    DIR *dir;
    class dirent *ent;
    class stat st;


    // Check whether the user informed a FROM filespec
    if (gpFileSpec[2] == NULL)
    {
        dwError = ERROR_BAD_ARGUMENTS;
        goto Exit_0;
    }

    // Initialize the disk interface
    if ((dwError = LoadVDI()) != 0)
        goto Exit_0;

    // Initialize the DOS interface
    if ((dwError = LoadOSI()) != 0)
        goto Exit_1;

    // 360KB should be more than enough for any TRS-80 file
    dwBytes = (MAX_FILE_SIZE) + (V80_MEM - (MAX_FILE_SIZE) % V80_MEM);

    // Allocate memory
    if ((pBuffer = (BYTE*)calloc(1,dwBytes)) == NULL)
    {

		perror("Put");
        dwError = ERROR_OUTOFMEMORY;
        goto Exit_2;
    }

    // Print operation objective
    printf("\r\nWriting files to disk:\r\n\r\n");

    // Get the target file path
	if( realpath(gpFileSpec[2], szFileSpec) == NULL )
    {
		perror("Put");
        goto Exit_3;
    }

    pFileName = basename(szFileSpec);

	dir = opendir(gpFileSpec[2]);

    // Get the first file in the list
    if (!dir)
    {
		perror("opendir failed");
        goto Exit_3;
    }

    // While there are files pending for writing
    while ((ent = readdir(dir)) != NULL) {
		struct tm *t;
        const char *file_name = ent->d_name;

        if (file_name[0] == '.')
            continue;

        if (stat(file_name, &st) == -1)
            continue;

        if(st.st_mode & S_IFDIR) 
            continue;

		t = gmtime((const time_t *)&st.st_mtime);

        // Convert Windows filename to the TRS standard
        Win2TRS(file_name, cFile);

        // Set target filename
        memcpy(File.szName, cFile, 8);
        memcpy(File.szType, &cFile[8], 3);

        // Set target file size
        File.dwSize = st.st_size;

        // Set target file date
        File.Date.nDay = t->tm_wday;
        File.Date.nMonth = t->tm_mon + 1;
        File.Date.wYear = t->tm_year + 1900;

        // Set target file attributes
        File.bSystem = false;
        File.bInvisible = false;
        File.bModified = true;
        File.nAccess = OSI_PROT_FULL;

        // Format the filename for printing purposes
        FmtName(File.szName, File.szType, (strcmp(typeid(*gpOSI).name()+2, "CPM") ? "/" : "."), szFile);

        // Print the filenames
        printf("%-12s/%-12s -> %-12s\t", szFileSpec, file_name, szFile);

        // Check whether the file size is valid
        if (File.dwSize > MAX_FILE_SIZE )
        {
            puts("Invalid size!");
            continue;
        }

        // Open the Windows file
        if ( (hFile = fopen(file_name, "r")) == NULL)
        {
            printf("Can't open: %s\n", file_name);
            continue;
        }

		dwBytes = fread(pBuffer, 1, File.dwSize, hFile);
        // Read file contents to the buffer
        if (dwBytes != File.dwSize)
        {
			fprintf(stderr,"Read error: %s - bytes read %d, expected %d \n", szFile, dwBytes,File.dwSize);
                        perror("Returned error ");
            fclose(hFile);
            continue;
        }

        // Close file handle
		fclose(hFile);

        // Create a TRS file with the properties defined above
        if ((dwError = gpOSI->Create(&pFile, File)) != 0)
            goto Exit_4;

        // Move the file pointer to the beginning
        if ((dwError = gpOSI->Seek(pFile, 0)) != 0)
        {
            gpOSI->Delete(pFile);
            goto Exit_4;
        }

        // Write the buffer contents to the new file
        if ((dwError = gpOSI->Write(pFile, pBuffer, File.dwSize)) != 0)
        {
            gpOSI->Delete(pFile);
            goto Exit_4;
        }

        // Print the total number of bytes written
        printf("%8d bytes OK\r\n", File.dwSize);

        // Update operation status variables
        wFiles++;
        dwSize += File.dwSize;

    }

    // Print operation summary
    printf("\r\nTotal of %d bytes written in %d files.\r\n\r\n", dwSize, wFiles);

    // Close find file handle
    Exit_4:
    if (dir != NULL)
        closedir(dir);

    // Release the allocated memory
    Exit_3:
    if (pBuffer != NULL)
        free(pBuffer);

    // Release the OSI object
    Exit_2:
    if (gpOSI != NULL)
        delete gpOSI;

    // Release the VDI object
    Exit_1:
    if (gpVDI != NULL)
        delete gpVDI;

    // Return
    Exit_0:
    return dwError;

}

//---------------------------------------------------------------------------------
// Rename files
//---------------------------------------------------------------------------------

DWORD Ren()
{

    OSI_FILE    File;
    char        cName[9];
    char        cType[4];
    char        cSource[11];
    char        cTarget[11];
    char        szFromFile[13];
    char        szToFile[13];
    void*       pFile = NULL;
    WORD        wFiles = 0;
    DWORD       dwError = 0;

    // Clear variables cName and cType
    memset(cName, 0, sizeof(cName));
    memset(cType, 0, sizeof(cType));

    // Check whether the user informed both the FROM and TO filespecs
    if (gpFileSpec[2] == NULL || gpFileSpec[3] == NULL)
    {
        dwError = ERROR_BAD_ARGUMENTS;
        goto Exit_0;
    }

    // Initialize the disk interface
    if ((dwError = LoadVDI()) != 0)
        goto Exit_0;

    // Initialize the DOS interface
    if ((dwError = LoadOSI()) != 0)
        goto Exit_1;

    // Print operation objective
    printf("\r\nRenaming files:\r\n\r\n");

    // Convert Windows filespecs to TRS standards
    Win2TRS(gpFileSpec[2], cSource);
    Win2TRS(gpFileSpec[3], cTarget);

    // While OSI::Dir() returns a valid file pointer
    while ((dwError = gpOSI->Dir(&pFile, (pFile == NULL ? OSI_DIR_FIND_FIRST : OSI_DIR_FIND_NEXT))) == 0)
    {

        // Get file properties
        gpOSI->GetFile(pFile, File);

        // Compare file attributes against user options
        if ((File.bSystem && !(gdwFlags & V80_FLAG_SYSTEM)) || (File.bInvisible && !(gdwFlags & V80_FLAG_INVISIBLE)))
            continue;

        // Compare the filename against the source filespec
        if (!WildComp(File.szName, cSource, 8) || !WildComp(File.szType, &cSource[8], 3))
            continue;

        // Copy the filename to the internal variable checking it against the target filespec
        WildCopy(File.szName, cName, cTarget, 8);
        WildCopy(File.szType, cType, &cTarget[8], 3);

        // Format "FROM" filename
        FmtName(File.szName, File.szType, (strcmp(typeid(*gpOSI).name()+2, "CPM") ? "/" : "."), szFromFile);

        // Replace current filename with the new one
        memcpy(File.szName, cName, sizeof(cName));
        memcpy(File.szType, cType, sizeof(cType));

        // Format "TO" filename
        FmtName(cName, cType, (strcmp(typeid(*gpOSI).name()+2, "CPM") ? "/" : "."), szToFile);

        // Print filenames
        printf("%-12s -> %-12s\t", szFromFile, szToFile);

        // Update file properties
        if ((dwError = gpOSI->SetFile(pFile, File)) != 0)
            goto Exit_2;

        // Print OK if the file has been successfully renamed
        printf("OK\r\n");

        // Update operation status variable
        wFiles++;

    }

    // Print operation summary
    printf("\r\nTotal of %d files renamed.\r\n\r\n", wFiles);

    // If exited on "No More Files" then "No Error"
    if (dwError == ERROR_NO_MORE_FILES)
        dwError = 0;

    // Release the OSI object
    Exit_2:
    if (gpOSI != NULL)
        delete gpOSI;

    // Release the VDI object
    Exit_1:
    if (gpVDI != NULL)
        delete gpVDI;

    // Return
    Exit_0:
    return dwError;

}

//---------------------------------------------------------------------------------
// Delete files
//---------------------------------------------------------------------------------

DWORD Del()
{

    OSI_FILE    File;
    char        cMask[11];
    char        szFile[13];
    void*       pFile = NULL;
    WORD        wFiles = 0;
    DWORD       dwError = 0;

    // Check whether the user informed a filespec
    if (gpFileSpec[2] == NULL)
    {
        dwError = ERROR_BAD_ARGUMENTS;
        goto Exit_0;
    }

    // Initialize the disk interface
    if ((dwError = LoadVDI()) != 0)
        goto Exit_0;

    // Initialize the DOS interface
    if ((dwError = LoadOSI()) != 0)
        goto Exit_1;

    // Print operation objective
    printf("\r\nDeleting files:\r\n\r\n");

    // Convert Windows filespec to TRS standard
    Win2TRS(gpFileSpec[2], cMask);

    // While OSI::Dir() returns a valid file pointer
    while ((dwError = gpOSI->Dir(&pFile, (pFile == NULL ? OSI_DIR_FIND_FIRST : OSI_DIR_FIND_NEXT))) == 0)
    {

        // Get file properties
        gpOSI->GetFile(pFile, File);

        // Compare file attributes against user options
        if ((File.bSystem && !(gdwFlags & V80_FLAG_SYSTEM)) || (File.bInvisible && !(gdwFlags & V80_FLAG_INVISIBLE)))
            continue;

        // Compare the filename against the source filespec
        if (!WildComp(File.szName, cMask, 8) || !WildComp(File.szType, &cMask[8], 3))
            continue;

        // Format filename
        FmtName(File.szName, File.szType, (strcmp(typeid(*gpOSI).name()+2, "CPM") ? "/" : "."), szFile);

        // Print filename
        printf("%-12s\t", szFile);

        // Delete the file
        dwError = gpOSI->Delete(pFile);

        // Print error message
        if (dwError != 0)
        {
			printf("Delete dwError:%d\n", dwError);
            continue;
        }

        // Print OK if the file has been successfully deleted
        printf("OK\r\n");

        // Update operation status variable
        wFiles++;

    }

    // Print operation summary
    printf("\r\nTotal of %d files deleted.\r\n\r\n", wFiles);

    // If exited on "No More Files" then "No Error"
    if (dwError == ERROR_NO_MORE_FILES)
        dwError = 0;

    // Release the OSI object
    if (gpOSI != NULL)
        delete gpOSI;

    // Release the VDI object
    Exit_1:
    if (gpVDI != NULL)
        delete gpVDI;

	if (dwError)
		printf("Delete dwError:%d\n", dwError);

    // Return
    Exit_0:
    return dwError;

}

//---------------------------------------------------------------------------------
// Dump file contents
//---------------------------------------------------------------------------------

DWORD DumpFile()
{

    OSI_FILE    File;
    char        cMask[11];
    char        szFile[13];
    void*       pFile = NULL;
    BYTE*       pBuffer = NULL;
    DWORD       dwBytes;
    DWORD       dwError = 0;

    // Check whether the user informed a filespec
    if (gpFileSpec[2] == NULL)
    {
        dwError = ERROR_BAD_ARGUMENTS;
        goto Exit_0;
    }

    // Initialize the disk interface
    if ((dwError = LoadVDI()) != 0)
        goto Exit_0;

    // Initialize the DOS interface
    if ((dwError = LoadOSI()) != 0)
        goto Exit_1;

    // 360KB should be more than enough for any TRS-80 file
    dwBytes = (MAX_FILE_SIZE) + (V80_MEM - (MAX_FILE_SIZE) % V80_MEM);

    // Allocate memory
    if ((pBuffer = (BYTE*)calloc(dwBytes,1)) == NULL)
    {
        perror("Dump File");
        dwError = ERROR_OUTOFMEMORY;
        goto Exit_2;
    }

    // Convert Windows filespec to TRS standard
    Win2TRS(gpFileSpec[2], cMask);

    // While OSI::Dir() returns a valid file pointer
    while ((dwError = gpOSI->Dir(&pFile, (pFile == NULL ? OSI_DIR_FIND_FIRST : OSI_DIR_FIND_NEXT))) == 0)
    {

        // Get file properties
        gpOSI->GetFile(pFile, File);

        // Compare file attributes against user options
        if ((File.bSystem && !(gdwFlags & V80_FLAG_SYSTEM)) || (File.bInvisible && !(gdwFlags & V80_FLAG_INVISIBLE)))
            continue;

        // Compare the filename against the source filespec
        if (!WildComp(File.szName, cMask, 8) || !WildComp(File.szType, &cMask[8], 3))
            continue;

        // Format filename
        FmtName(File.szName, File.szType, (strcmp(typeid(*gpOSI).name()+2, "CPM") ? "/" : "."), szFile);

        // Print operation objective
        printf("\r\nDumping contents of %s:\r\n\r\n", szFile);

        // Set the file pointer
        if ((dwError = gpOSI->Seek(pFile, 0)) != 0)
        {
            printf("Dump File Seek: dwError:%d\n", dwError);
            continue;
        }

        // Read the file contents
        if ((dwError = gpOSI->Read(pFile, pBuffer, File.dwSize)) != 0 && !(gdwFlags & V80_FLAG_READBAD))
        {
            printf("Dump File Read: dwError:%d\n", dwError);
            continue;
        }

        // Dump the file contents
        Dump(pBuffer, File.dwSize);

        // Print operation summary
        printf("\r\nTotal of %d bytes dumped.\r\n\r\n", File.dwSize);

    }

    // If exited on "No More Files" then "No Error"
    if (dwError == ERROR_NO_MORE_FILES)
        dwError = 0;

    // Release the allocated memory
    if (pBuffer != NULL)
        free(pBuffer);

    // Release the OSI object
    Exit_2:
    if (gpOSI != NULL)
        delete gpOSI;

    // Release the VDI object
    Exit_1:
    if (gpVDI != NULL)
        delete gpVDI;

    // Return
    Exit_0:
    return dwError;

}

//---------------------------------------------------------------------------------
// Dump disk sectors
//---------------------------------------------------------------------------------

DWORD DumpDisk()
{

    VDI_GEOMETRY    DG;
    VDI_TRACK*      pTrack;
    BYTE            Buffer[1024];
    WORD            wSectors = 0;
    DWORD           dwError;

    // Initialize the disk interface
    if ((dwError = LoadVDI()) != 0)
        goto Done;

    // Print operation objective
    printf("\r\nDumping disk contents:\r\n\r\n");

    // Get the disk geometry
    gpVDI->GetDG(DG);

    // For each track in the disk
    for (int nTrack = DG.FT.nTrack; nTrack <= DG.LT.nTrack; nTrack++)
    {
        // Makes pTrack point to FirstTrack or LastTrack accordingly
        pTrack = (nTrack == 0 ? &DG.FT : &DG.LT);

        // For each side in the disk
        for (int nSide = pTrack->nFirstSide; nSide <= pTrack->nLastSide; nSide++)
        {   // For each sector in the track
            for (int nSector = pTrack->nFirstSector; nSector <= pTrack->nLastSector; nSector++)
            {   // Read sector
                if (gpVDI->Read(nTrack, nSide, nSector, Buffer, sizeof(Buffer)) == 0)
                {   // Dump sector data
                    printf("\r\n[%02d:%d:%02d]\r\n", nTrack, nSide, nSector);
                    Dump(Buffer, pTrack->wSectorSize);
                    wSectors++;
                }
            }
        }

    }

    // Print operation summary
    printf("\r\nTotal of %d sectors dumped.\r\n\r\n", wSectors);

    // Release the VDI object
    delete gpVDI;

    // Return
    Done:
    return dwError;

}

//---------------------------------------------------------------------------------
// Initialize the Virtual Disk Interface
//---------------------------------------------------------------------------------

DWORD LoadVDI()
{

	int dwError = 0;
    // Check whether the user indicated a disk interface
    if (gpVDI != NULL)
    {
        dwError = gpVDI->Load(ghFile, gdwFlags);
        if (!dwError)
            goto Done;
        else
            goto Error;
    }

    // Try the DMK format
    gpVDI = new CDMK;

    dwError = gpVDI->Load(ghFile, gdwFlags);
    if (!dwError)
        goto Done;

    delete gpVDI;

    // Try the JV3 format

    gpVDI = new CJV3;

    dwError = gpVDI->Load(ghFile, gdwFlags);
    if (!dwError)
        goto Done;

    delete gpVDI;

    // Try the JV1 format

    gpVDI = new CJV1;

    dwError = gpVDI->Load(ghFile, gdwFlags);
    if (!dwError)
        goto Done;

    delete gpVDI;

    Error:
    gpVDI = NULL;

    Done:
    // If requested by the user, print disk data
    if ((gdwFlags & V80_FLAG_INFO) && gpVDI != NULL)
    {
        VDI_GEOMETRY DG;
        gpVDI->GetDG(DG);
        printf("\r\nVDI: %-3s (%02d:%d:%02d,%s)\r\n", typeid(*gpVDI).name()+2, (DG.LT.nTrack-DG.FT.nTrack+1), (DG.LT.nLastSide-DG.LT.nFirstSide+1), (DG.LT.nLastSector-DG.LT.nFirstSector+1), (DG.FT.nDensity!=DG.LT.nDensity?"MD":(DG.LT.nDensity==VDI_DENSITY_SINGLE?"SD":"DD")));
    }

    return dwError;

}

//---------------------------------------------------------------------------------
// Initialize the Operating System Interface
//---------------------------------------------------------------------------------

DWORD LoadOSI()
{

    int dwError = 0;

    // Check whether the user indicated a DOS interface
    if (gpOSI != NULL)
    {
        if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
            goto Done;
        else
            goto Error;
    }

    // Try TRSDOS Model 4

    gpOSI = new CTD4;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    // Try TRSDOS Model III

    gpOSI = new CTD3;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    // Try TRSDOS Model I

    gpOSI = new CTD1;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    // Try RapiDOS

    gpOSI = new CRD;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    // Try MicroDOS/OS-80 III

    gpOSI = new CMD;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    // Try NewDOS/80

    gpOSI = new CND;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    // Try DoubleDOS

    gpOSI = new CDD;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    // Try CP/M

    gpOSI = new CCPM;

    if ((dwError = gpOSI->Load(gpVDI, gdwFlags)) == 0)
        goto Done;

    delete gpOSI;

    Error:
    gpOSI = NULL;

    Done:
    // If requested by the user, print DOS data
    if ((gdwFlags & V80_FLAG_INFO) && gpOSI != NULL)
    {
        OSI_DOS DOS;
        gpOSI->GetDOS(DOS);
        Trim(DOS.szName);
        Trim(DOS.szDate);
        printf("OSI: %-3s (%s,%s,%02X)\r\n", typeid(*gpOSI).name()+2, DOS.szName, DOS.szDate, DOS.nVersion);
    }

    return dwError;

}

//---------------------------------------------------------------------------------
// Print data in hex and ASCII
//---------------------------------------------------------------------------------

void Dump(unsigned char* pBuffer, int nSize)
{

    char szLine[48+16+1];
    char szTemp[4];
    int  x;

    // Clear variables szLine and szTemp
    memset(szLine, 0, sizeof(szLine));
    memset(szTemp, 0, sizeof(szTemp));

    // For each byte in the buffer
    for (x = 0; x < nSize; x++)
    {
        // Convert byte to HEX in szTemp
        sprintf(szTemp, "%02X ", pBuffer[x]);

        // Append szTemp to szLine
        memcpy(&szLine[(x % 16) *3], szTemp, 3);

        // Set the corresponding ASCII value in szLine
        szLine[(16 * 3) + (x % 16)] = (pBuffer[x] < ' ' ? '.' : pBuffer[x]);

        // When a 16-byte line has been filled, print a CR/LF
        if ((x % 16) == 15)
        {
            printf("%s\r\n", szLine);
            memset(szLine, ' ', sizeof(szLine)-1);
        }

    }

    // If exited in the middle of a 16-byte line, print a CR/LF
    if (x % 16 != 0)
        printf("%s\r\n", szLine);

}

//---------------------------------------------------------------------------------
// Compare a string against a wildcard-based mask (case insensitive)
//---------------------------------------------------------------------------------

bool WildComp(const char* pSource, const char* pMask, BYTE nLength)
{

    for (int x = 0; x < nLength; x++)
    {
        if (pMask[x] == '?')
            continue;
        if (pMask[x] == '*')
            break;
        if ((pMask[x] | ('a'-'A')) != (pSource[x] | ('a'-'A')))
            return false;
    }

    return true;

}

//---------------------------------------------------------------------------------
// Compare a string against a wildcard-based mask (case insensitive)
//---------------------------------------------------------------------------------

void WildCopy(const char* pSource, char* pTarget, const char* pMask, BYTE nLength)
{
    for (int x = 0; x < nLength; x++)
    {
        if (pMask[x] == '?')
        {
            pTarget[x] = pSource[x];
            continue;
        }
        if (pMask[x] == '*')
        {
            memcpy(&pTarget[x], &pSource[x], nLength - x);
            break;
        }
        pTarget[x] = toupper(pMask[x]);
    }
}

//---------------------------------------------------------------------------------
// Format a new filename from separated parts
//---------------------------------------------------------------------------------

void FmtName(const char szName[9], const char szType[4], const char* szDivider, char szNewName[13])
{

    char szTmpName[9];
    char szTmpType[4];

    // Copy szName and szType to temporary variables
    memcpy(szTmpName, szName, sizeof(szTmpName));
    memcpy(szTmpType, szType, sizeof(szTmpType));

    // Remove trailing spaces from name
    for (int x = 8; x > 0 && szTmpName[x - 1] == ' '; x--)
        szTmpName[x - 1] = 0;

    // Remove trailing spaces from extension
    for (int x = 3; x > 0 && szTmpType[x - 1] == ' '; x--)
        szTmpType[x - 1] = 0;

    // Assemble the new name from the two temporary variables
    sprintf(szNewName, "%s%s%s", szTmpName, (szTmpType[0] ? szDivider : ""), szTmpType);

}

//---------------------------------------------------------------------------------
// Convert Windows-based filename to TRS standard
//---------------------------------------------------------------------------------

void Win2TRS(const char* pWinName, char cTRSName[11])
{

    int x, y, z, k = strlen(pWinName);

    for (x = k; x > 0 && pWinName[x - 1] != '/'; x--)
		;

    if (pWinName[x] == '/')
        x++;

    for (y = 0; y < 8 && pWinName[x] != '.' && pWinName[x] != '/' && pWinName[x] != 0; x++, y++)
        cTRSName[y] = toupper(pWinName[x]);

    for ( ; y < 8; y++)
        cTRSName[y] = ' ';

    for (z = 0; z < (k - x); z++)
    {
        if (pWinName[x+z] == '.' || pWinName[x+z] == '/')
        {
            x += z + 1;
            break;
        }
    }

    for (; y < 11 && pWinName[x] != 0; x++, y++)
        cTRSName[y] = toupper(pWinName[x]);

    for ( ; y < 11; y++)
        cTRSName[y] = ' ';

}

//---------------------------------------------------------------------------------
// Remove spaces from disk name/date
//---------------------------------------------------------------------------------

void Trim(char cField[8])
{
    for (int x = 8; x > 0; x--)
    {
        if (cField[x-1] != ' ')
            break;
        cField[x-1] = 0;
    }
}

//---------------------------------------------------------------------------------
// Processes command line parameters
//---------------------------------------------------------------------------------

DWORD ParseCmdLine(int argc, char* argv[])
{

    BYTE    x, y, z;
    DWORD   dwError = 0;

    for (x = 0, z = 0; x < argc; x++)
    {

        if (argv[x][0] == '-')
        {

            for (y = 0; y < (sizeof(gSwitches) / sizeof(SWITCH)); y++)
            {
                if (strcasecmp(argv[x], gSwitches[y].cName) == 0)
                {
                    if ((dwError = (*gSwitches[y].pCall)(gSwitches[y].pParam)) != 0)
                        goto Done;
                    goto Next;
                }
            }

            printf("Unknown switch:%s\n", argv[x]);
            dwError = ERROR_BAD_ARGUMENTS;
            goto Done;

            Next:;

        }
        else
        {
            if (z < 4)
            {
                gpFileSpec[z++] = argv[x];
            }
            else
            {
                printf("Too many filespecs: %s ignored\n", argv[x]);
                dwError = ERROR_BAD_ARGUMENTS;
                goto Done;
            }
        }

    }

    Done:
    return dwError;

}

//---------------------------------------------------------------------------------
// Set the function pointer according to the command requested by the user
//---------------------------------------------------------------------------------

DWORD SetCmd(void* pParam)
{

    DWORD dwError = 0;

    if (gpCommand != NULL)
    {
        puts("Attempt to set multiple commands.");
        dwError = ERROR_BAD_ARGUMENTS;
        goto Done;
    }

    gpCommand = (DWORD (*)())pParam;

    Done:
    return dwError;

}

//---------------------------------------------------------------------------------
// Turn on option flags according to selected user switches
//---------------------------------------------------------------------------------

DWORD SetOpt(void* pParam)
{

    DWORD dwError = 0;
    DWORD p = (unsigned long) pParam & 0xffffffff;

// FIXME if (((gdwFlags | (DWORD)pParam) & V80_FLAG_SS) && ((gdwFlags | (DWORD)pParam) & V80_FLAG_DS))
    if (((gdwFlags | p) & V80_FLAG_SS) && ((gdwFlags | p) & V80_FLAG_DS))
    {
        puts("Attempt to set conflicting disk parameters.");
        dwError = ERROR_BAD_ARGUMENTS;
        goto Done;
    }

// FIXME gdwFlags |= (DWORD)pParam;
    gdwFlags |= p;

    Done:
    return dwError;

}

//---------------------------------------------------------------------------------
// Set the VDI function pointer according to the file format requested by the user
//---------------------------------------------------------------------------------

DWORD SetVDI(void* pParam)
{

    DWORD dwError = 0;

    if (gpVDI != NULL)
    {
        puts("Attempt to set multiple file formats.");
        dwError = ERROR_BAD_ARGUMENTS;
        goto Done;
    }

    gpVDI = (CVDI*)pParam;

    Done:
    return dwError;

}

//---------------------------------------------------------------------------------
// Set the OSI function pointer according to the DOS requested by the user
//---------------------------------------------------------------------------------

DWORD SetOSI(void* pParam)
{

    DWORD dwError = 0;

    if (gpOSI != NULL)
    {
        puts("Attempt to set multiple operating systems.");
        dwError = ERROR_BAD_ARGUMENTS;
        goto Done;
    }

    gpOSI = (COSI*)pParam;

    Done:
    return dwError;

}

//---------------------------------------------------------------------------------
// Print usage instructions
//---------------------------------------------------------------------------------

void PrintHelp()
{

    DWORD (*pLastFnCall)(void*) = NULL;

    printf("\r\nSyntax: V80 [switches] <disk_image> [source_filespec] [target_filespec]\r\n");

    for (BYTE x = 0, y = 0; x < (sizeof(gSwitches) / sizeof(SWITCH)); x++)
    {

        if (gSwitches[x].pCall != pLastFnCall && y < sizeof(gCategories))
        {
            printf("\r\n%s:\r\n", gCategories[y++]);
            pLastFnCall = gSwitches[x].pCall;
        }

        printf("\t%s\t\t%s\r\n", gSwitches[x].cName, gSwitches[x].cDescr);

    }

}

//---------------------------------------------------------------------------------
// Print error message
//---------------------------------------------------------------------------------

void PrintError(DWORD dwError)
{

    char szMessage[128];

	if(dwError >= 0 && dwError < ERROR_LAST)
		printf("dwError: %s\n", errors_msg[dwError]);
	else
		printf("dwError: unknown (%d)\n", dwError);

}
