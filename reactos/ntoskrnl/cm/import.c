/* $Id: import.c,v 1.13 2003/04/01 16:37:14 ekohl Exp $
 *
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/cm/import.c
 * PURPOSE:         Registry functions
 * PROGRAMMERS:     Rex Jolliff
 */

#ifdef WIN32_REGDBG
#include "cm_win32.h"
#else
#include <ctype.h>

#include <ddk/ntddk.h>
#include <roscfg.h>
#include <internal/ob.h>
#include <limits.h>
#include <string.h>
#include <internal/pool.h>
#include <internal/registry.h>
#include <internal/ntoskrnl.h>

#define NDEBUG
#include <internal/debug.h>

#include "cm.h"
#endif

extern PKEY_OBJECT  CmiMachineKey;


static PCHAR 
checkAndSkipMagic (PCHAR  regChunk)
{
  if (strncmp (regChunk, 
               REGISTRY_FILE_MAGIC, 
               strlen (REGISTRY_FILE_MAGIC)) != 0)
  {
    CPRINT ("incorrect magic number in registry chunk. expected: %s got:%.*s\n",
            REGISTRY_FILE_MAGIC,
            strlen (REGISTRY_FILE_MAGIC),
            regChunk);
    return  0;
  }
  regChunk += strlen (REGISTRY_FILE_MAGIC);
  DPRINT ("Found regsitry chunk magic value\n");

  return  regChunk;
}

static PCHAR
skipWhitespaceInChunk (PCHAR regChunk)
{
  while (*regChunk && isspace (*regChunk))
    regChunk++;

  return *regChunk ? regChunk : 0;
}

static int
computeKeyNameSize (PCHAR  regChunk)
{
  int  copyCount = 0;

  while (*regChunk != 0 && *regChunk != ']')
  {
    copyCount++;
    regChunk++;
  }

  return  copyCount;
}

static BOOLEAN
allocateKeyName (PUNICODE_STRING  newKeyName, int  newKeySize)
{
  if (newKeyName->MaximumLength < (newKeySize + 1) * sizeof (WCHAR))
  {
    if (newKeyName->Buffer != 0)
      ExFreePool (newKeyName->Buffer);
    newKeyName->Length = 0;
    newKeyName->MaximumLength = (newKeySize + 1) * sizeof (WCHAR);
    newKeyName->Buffer = ExAllocatePool (NonPagedPool, newKeyName->MaximumLength);
    if (newKeyName->Buffer == 0)
    {
      CPRINT ("Could not allocate space for key name\n");
      return  FALSE;
    }
    newKeyName->Buffer [0] = 0;
  }
  else
  {
    newKeyName->Length = 0;
    newKeyName->Buffer [0] = 0;
  }

  return  TRUE;
}

static PCHAR
skipToNextKeyInChunk (PCHAR  regChunk)
{
  while (*regChunk != 0 && *regChunk != '[')
  {
    while (*regChunk != 0 && *regChunk != '\n')
    {
      regChunk++;
    }
    regChunk++;
  }

  return  *regChunk ? regChunk : 0;
}

static PCHAR
getKeyNameFromChunk (PCHAR  regChunk, PUNICODE_STRING  newKeyName)
{
  int index = 0;

  while (*regChunk != 0 && *regChunk != ']')
  {
    newKeyName->Buffer [index++] = *regChunk++;
  }
  newKeyName->Buffer [index] = '\0';
  newKeyName->Length = index * sizeof (WCHAR);

  return  *regChunk ? regChunk : 0;
}

static HANDLE
createNewKey (PUNICODE_STRING  newKeyName)
{
  NTSTATUS  status;
  OBJECT_ATTRIBUTES  attributes;
  HANDLE  handleToReturn;

  DPRINT ("Creating key (%wZ)\n", newKeyName);
  InitializeObjectAttributes (&attributes,
                              newKeyName,
                              0,
                              0,
                              NULL);
  status = NtCreateKey (&handleToReturn,
                        KEY_ALL_ACCESS,
                        &attributes,
                        0,
                        NULL,
                        REG_OPTION_VOLATILE,
                        NULL);
  if (!NT_SUCCESS(status))
  {
    CPRINT ("Could not crete key (%wZ)\n", newKeyName);
    return  INVALID_HANDLE_VALUE;
  }

  return  handleToReturn;
}

static PCHAR
skipToNextKeyValueInChunk (PCHAR  regChunk)
{
  while (*regChunk != 0 && *regChunk != '\n')
    regChunk++;
  regChunk = skipWhitespaceInChunk (regChunk);
  
  return  regChunk;
}

static int
computeKeyValueNameSize (PCHAR  regChunk)
{
  int size = 0;

  if (*regChunk != '\"')
    return  0;
  regChunk++;
  while (*regChunk != 0 && *regChunk != '\"')
  {
    size++;
    regChunk++;
  }

  return  regChunk ? size : 0;
}

static PCHAR
getKeyValueNameFromChunk (PCHAR  regChunk, PUNICODE_STRING  newKeyName)
{
  int  index = 0;

  regChunk++;
  while (*regChunk != 0 && *regChunk != '\"')
  {
    newKeyName->Buffer [index++] = *regChunk++;
  }
  newKeyName->Buffer [index] = '\0';
  newKeyName->Length = index * sizeof (WCHAR);
  regChunk++;

  return  *regChunk ? regChunk : 0;
}

static PCHAR 
getKeyValueTypeFromChunk (PCHAR  regChunk, PCHAR  dataFormat, int *keyValueType)
{
  if (*regChunk == '\"')
  {
    strcpy (dataFormat, "string");
    *keyValueType = REG_SZ;
  }
  else if (strncmp (regChunk, "hex", 3) == 0)
  {
    strcpy (dataFormat, "hex");
    regChunk += 3;
    if (*regChunk == '(')
    {
      regChunk++;
      *keyValueType = atoi (regChunk);
      while (*regChunk != 0 && *regChunk != ')')
        regChunk++;
      regChunk++;
    }
    else
      *keyValueType = REG_BINARY;
    if (*regChunk == ':')
      regChunk++;
  }
  else if (strncmp (regChunk, "dword", 5) == 0)
  {
    strcpy (dataFormat, "dword");
    *keyValueType = REG_DWORD;
    regChunk += 5;
    if (*regChunk == ':')
      regChunk++;
  }
  else if (strncmp (regChunk, "multi", 5) == 0)
  {
    strcpy (dataFormat, "multi");
    *keyValueType = REG_MULTI_SZ;
    regChunk += 5;
    if (*regChunk == ':')
      regChunk++;
  }
  else if (strncmp (regChunk, "expand", 6) == 0)
  {
    strcpy (dataFormat, "expand");
    *keyValueType = REG_EXPAND_SZ;
    regChunk += 6;
    if (*regChunk == ':')
      regChunk++;
  }
  else
  {
    UNIMPLEMENTED;
  }

  return  *regChunk ? regChunk : 0;
}

static int 
computeKeyValueDataSize (PCHAR  regChunk, PCHAR  dataFormat)
{
  int  dataSize = 0;

  if (strcmp (dataFormat, "string") == 0)
  {
    regChunk++;
    while (*regChunk != 0 && *regChunk != '\"')
    {
      dataSize++;
      dataSize++;
      regChunk++;
    }
    dataSize++;
    dataSize++;
  }
  else if (strcmp (dataFormat, "hex") == 0)
  {
    while (*regChunk != 0 && isxdigit(*regChunk))
    {
      regChunk++;
      regChunk++;
      dataSize++;
      if (*regChunk == ',')
      {
        regChunk++;
        if (*regChunk == '\\')
        {
          regChunk++;
          regChunk = skipWhitespaceInChunk (regChunk);
        }
      }
    }
  }
  else if (strcmp (dataFormat, "dword") == 0)
  {
    dataSize = sizeof(DWORD);
    while (*regChunk != 0 && isxdigit(*regChunk))
    {
      regChunk++;
    }
  }
  else if (strcmp (dataFormat, "multi") == 0)
  {
    while (*regChunk == '\"')
    {
      regChunk++;
      while (*regChunk != 0 && *regChunk != '\"')
      {
        dataSize++;
        dataSize++;
        regChunk++;
      }
      regChunk++;
      dataSize++;
      dataSize++;
      if (*regChunk == ',')
      {
        regChunk++;
        regChunk = skipWhitespaceInChunk (regChunk);
        if (*regChunk == '\\')
        {
          regChunk++;
          regChunk = skipWhitespaceInChunk (regChunk);
        }
      }
      else
        break;
    }
    dataSize++;
    dataSize++;
  }
  else if (strcmp (dataFormat, "expand") == 0)
  {
    regChunk++;
    while (*regChunk != 0 && *regChunk != '\"')
    {
      dataSize++;
      dataSize++;
      regChunk++;
    }
    dataSize++;
    dataSize++;
  }
  else
  {
    UNIMPLEMENTED;
  }

  return  dataSize;
}

static BOOLEAN
allocateDataBuffer (PVOID * data, int * dataBufferSize, int dataSize)
{
  if (*dataBufferSize < dataSize)
  {
    if (*dataBufferSize > 0)
      ExFreePool (*data);
    *data = ExAllocatePool (NonPagedPool, dataSize);
    *dataBufferSize = dataSize;
  }

  return  TRUE;
}

static PCHAR
getKeyValueDataFromChunk (PCHAR  regChunk, PCHAR  dataFormat, PCHAR data)
{
  char  dataValue;
  ULONG ulValue;
  PWCHAR ptr;
  
  if (strcmp (dataFormat, "string") == 0)
  {
    /* convert quoted string to zero-terminated Unicode string */
    ptr = (PWCHAR)data;
    regChunk++;
    while (*regChunk != 0 && *regChunk != '\"')
    {
      *ptr++ = (WCHAR)*regChunk++;
    }
    *ptr = 0;
    regChunk++;
  }
  else if (strcmp (dataFormat, "hex") == 0)
  {
    while (*regChunk != 0 && isxdigit (*regChunk))
    {
      dataValue = (isdigit (*regChunk) ? *regChunk - '0' : 
        tolower(*regChunk) - 'a' + 10) << 4;
      regChunk++;
      dataValue += (isdigit (*regChunk) ? *regChunk - '0' : 
        tolower(*regChunk) - 'a' + 10);
      regChunk++;
      *data++ = dataValue;
      if (*regChunk == ',')
      {
        regChunk++;
        if (*regChunk == '\\')
        {
          regChunk++;
          regChunk = skipWhitespaceInChunk (regChunk);
        }
      }
    }
  }
  else if (strcmp (dataFormat, "dword") == 0)
  {
    ulValue = 0;
    while (*regChunk != 0 && isxdigit(*regChunk))
    {
      dataValue = (isdigit (*regChunk) ? *regChunk - '0' : 
        tolower(*regChunk) - 'a' + 10);
      ulValue = (ulValue << 4) + dataValue;
      regChunk++;
    }
    memcpy(data, &ulValue, sizeof(ULONG));
  }
  else if (strcmp (dataFormat, "multi") == 0)
  {
    ptr = (PWCHAR)data;
    while (*regChunk == '\"')
    {
      regChunk++;
      while (*regChunk != 0 && *regChunk != '\"')
      {
        *ptr++ = (WCHAR)*regChunk++;
      }
      regChunk++;
      *ptr++ = 0;
      if (*regChunk == ',')
      {
        regChunk++;
        regChunk = skipWhitespaceInChunk (regChunk);
        if (*regChunk == '\\')
        {
          regChunk++;
          regChunk = skipWhitespaceInChunk (regChunk);
        }
      }
      else
        break;
    }
    *ptr = 0;
  }
  else if (strcmp (dataFormat, "expand") == 0)
  {
    /* convert quoted string to zero-terminated Unicode string */
    ptr = (PWCHAR)data;
    regChunk++;
    while (*regChunk != 0 && *regChunk != '\"')
    {
      *ptr++ = (WCHAR)*regChunk++;
    }
    *ptr = 0;
    regChunk++;
  }
  else
  {
    UNIMPLEMENTED;
  }

  return  *regChunk ? regChunk : 0;
}

static BOOLEAN
setKeyValue (HANDLE  currentKey,
             PUNICODE_STRING  newValueName,
             ULONG  keyValueType,
             PVOID  data,
             ULONG  dataSize)
{
  NTSTATUS  status;

  DPRINT ("Adding value (%wZ) to current key, with data type %d size %d\n",
          newValueName,
          keyValueType,
          dataSize);
  status = NtSetValueKey (currentKey,
                          newValueName,
                          0,
                          keyValueType,
                          data,
                          dataSize);
  if (!NT_SUCCESS(status))
  {
    CPRINT ("could not set key value, rc:%08x\n", status);
    return  FALSE;
  }

  return  TRUE;
}

VOID
CmImportTextHive(PCHAR  ChunkBase,
		 ULONG  ChunkSize)
{
  HANDLE  currentKey = INVALID_HANDLE_VALUE;
  int  newKeySize;
  UNICODE_STRING  newKeyName = {0, 0, 0};
  char  dataFormat [10];
  int  keyValueType;
  int  dataSize = 0;
  int  dataBufferSize = 0;
  PVOID  data = 0;
  PCHAR regChunk;

  DPRINT("ChunkBase %p  ChunkSize %lx\n", ChunkBase, ChunkSize);

  regChunk = checkAndSkipMagic (ChunkBase);
  if (regChunk == 0)
    return;

  while (regChunk != 0 && *regChunk != 0 && (((ULONG)regChunk-(ULONG)ChunkBase) < ChunkSize))
  {
    regChunk = skipWhitespaceInChunk (regChunk);
    if (regChunk == 0)
      continue;

    if (*regChunk == '[')
    {
      if (currentKey != INVALID_HANDLE_VALUE)
      {
        DPRINT("Closing current key: 0x%lx\n", currentKey);
        NtClose (currentKey);
        currentKey = INVALID_HANDLE_VALUE;
      }

      regChunk++;

      newKeySize = computeKeyNameSize (regChunk);
      if (!allocateKeyName (&newKeyName, newKeySize))
      {
        regChunk = 0;
        continue;
      }

      regChunk = getKeyNameFromChunk (regChunk, &newKeyName);
      if (regChunk == 0)
        continue;

      currentKey = createNewKey (&newKeyName);
      if (currentKey == INVALID_HANDLE_VALUE)
      {
        regChunk = skipToNextKeyInChunk (regChunk);
        continue;
      }

      regChunk++;
    }
    else
    {
      if (currentKey == INVALID_HANDLE_VALUE)
      {
        regChunk = skipToNextKeyInChunk (regChunk);
        continue;
      }

      newKeySize = computeKeyValueNameSize (regChunk);
      if (!allocateKeyName (&newKeyName, newKeySize))
      {
        regChunk = 0;
        continue;
      }

      regChunk = getKeyValueNameFromChunk (regChunk, &newKeyName);
      if (regChunk == 0)
        continue;

      if (*regChunk != '=')
      {
        regChunk = skipToNextKeyValueInChunk (regChunk);
        continue;
      }
      regChunk++;

      regChunk = getKeyValueTypeFromChunk (regChunk, dataFormat, &keyValueType);
      if (regChunk == 0)
        continue;

      dataSize = computeKeyValueDataSize (regChunk, dataFormat);
      if (!allocateDataBuffer (&data, &dataBufferSize, dataSize))
      {
        regChunk = 0;
        continue;
      }
      
      regChunk = getKeyValueDataFromChunk (regChunk, dataFormat, data);
      if (regChunk == 0)
        continue;

      if (!setKeyValue (currentKey, &newKeyName, keyValueType, data, dataSize))
      {
        regChunk = 0;
        continue;
      }
    }
  }

  if (currentKey != INVALID_HANDLE_VALUE)
  {
    NtClose (currentKey);
  }
  if (newKeyName.Buffer != 0)
  {
    ExFreePool (newKeyName.Buffer);
  }
  if (data != 0)
  {
    ExFreePool (data);
  }

  return;
}


static BOOLEAN
CmImportBinarySystemHive(PCHAR ChunkBase,
			 ULONG ChunkSize,
			 PREGISTRY_HIVE *RegistryHive)
{
  PREGISTRY_HIVE Hive;
  PCELL_HEADER FreeBlock;
  BLOCK_OFFSET BlockOffset;
  PHBIN Bin;
  ULONG i, j;
  ULONG FreeOffset;
  NTSTATUS Status;
  ULONG BitmapSize;

  *RegistryHive = NULL;

  if (strncmp (ChunkBase, "regf", 4) != 0)
    {
      DPRINT1("Found invalid '%*s' magic\n", 4, ChunkBase);
      return FALSE;
    }

  /* Create a new hive */
  Hive = ExAllocatePool (NonPagedPool,
			 sizeof(REGISTRY_HIVE));
  if (Hive == NULL)
    {
      return FALSE;
    }
  RtlZeroMemory (Hive,
		 sizeof(REGISTRY_HIVE));

  Hive->HiveHeader = (PHIVE_HEADER)ExAllocatePool (NonPagedPool,
						   sizeof(HIVE_HEADER));
  if (Hive->HiveHeader == NULL)
    {
      DPRINT1 ("Allocating hive header failed\n");
      ExFreePool (Hive);
      return FALSE;
    }

  /* Import the hive header */
  RtlCopyMemory (Hive->HiveHeader,
		 ChunkBase,
		 sizeof(HIVE_HEADER));

  /* Read update counter */
  Hive->UpdateCounter = Hive->HiveHeader->UpdateCounter1;

  /* Set the hive's size */
  Hive->FileSize = ChunkSize;

  /* Set the size of the block list */
  Hive->BlockListSize = (Hive->FileSize / 4096) - 1;

  /* Allocate block list */
  DPRINT1("Space needed for block list describing hive: 0x%x\n",
	 sizeof(PHBIN *) * Hive->BlockListSize);
  Hive->BlockList = ExAllocatePool(NonPagedPool,
				   sizeof(PHBIN *) * Hive->BlockListSize);
  if (Hive->BlockList == NULL)
    {
      DPRINT1 ("Allocating block list failed\n");
      ExFreePool (Hive->HiveHeader);
      ExFreePool (Hive);
      return FALSE;
    }
CHECKPOINT1;

  /* Allocate the hive block */
  Hive->BlockList[0] = ExAllocatePool(PagedPool,
				      Hive->FileSize - 4096);
  if (Hive->BlockList[0] == NULL)
    {
      DPRINT1 ("Allocating the first hive block failed\n");
      ExFreePool (Hive->BlockList);
      ExFreePool (Hive->HiveHeader);
      ExFreePool (Hive);
      return FALSE;
    }

CHECKPOINT1;
  /* Import the hive block */
  RtlCopyMemory (Hive->BlockList[0],
		 ChunkBase + 4096,
		 Hive->FileSize - 4096);

  /* Initialize the free block list */
  Hive->FreeListSize = 0;
  Hive->FreeListMax = 0;
  Hive->FreeList = NULL;

CHECKPOINT1;
  BlockOffset = 0;
  for (i = 0; i < Hive->BlockListSize; i++)
    {
      Hive->BlockList[i] = (PHBIN) (((ULONG_PTR)Hive->BlockList[0]) + BlockOffset);
      Bin = (PHBIN) (((ULONG_PTR)Hive->BlockList[i]));
      if (Bin->BlockId != REG_BIN_ID)
	{
	  DPRINT1("Bad BlockId %x, offset %x\n", Bin->BlockId, BlockOffset);
	  /* FIXME: */
	  assert(FALSE);
//	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      assertmsg((Bin->BlockSize % 4096) == 0, ("BlockSize (0x%.08x) must be multiplum of 4K\n", Bin->BlockSize));

      if (Bin->BlockSize > 4096)
	{
	  for (j = 1; j < Bin->BlockSize / 4096; j++)
	    {
	      Hive->BlockList[i + j] = Hive->BlockList[i];
	    }
	  i = i + j - 1;
	}

      /* Search free blocks and add to list */
      FreeOffset = REG_HBIN_DATA_OFFSET;
      while (FreeOffset < Bin->BlockSize)
	{
	  FreeBlock = (PCELL_HEADER) ((ULONG_PTR)Hive->BlockList[i] + FreeOffset);
	  if (FreeBlock->CellSize > 0)
	    {
	      Status = CmiAddFree(Hive,
				  FreeBlock,
				  Hive->BlockList[i]->BlockOffset + FreeOffset,
				  FALSE);
	      if (!NT_SUCCESS(Status))
		{
		  /* FIXME: */
		  assert(FALSE);
		}

	      FreeOffset += FreeBlock->CellSize;
	    }
	  else
	    {
	      FreeOffset -= FreeBlock->CellSize;
	    }
	}
      BlockOffset += Bin->BlockSize;
    }
CHECKPOINT1;

  /* Calculate bitmap size in bytes (always a multiple of 32 bits) */
  BitmapSize = ROUND_UP(Hive->BlockListSize, sizeof(ULONG) * 8) / 8;
  DPRINT1("Hive->BlockListSize: %lu\n", Hive->BlockListSize);
  DPRINT1("BitmapSize:  %lu Bytes  %lu Bits\n", BitmapSize, BitmapSize * 8);

  /* Allocate bitmap */
  Hive->BitmapBuffer = (PULONG)ExAllocatePool(PagedPool,
					      BitmapSize);
  if (Hive->BitmapBuffer == NULL)
    {
      DPRINT1 ("Allocating the hive bitmap failed\n");
      ExFreePool (Hive->BlockList[0]);
      ExFreePool (Hive->BlockList);
      ExFreePool (Hive->HiveHeader);
      ExFreePool (Hive);
      return FALSE;
    }
CHECKPOINT1;

  /* Initialize bitmap */
  RtlInitializeBitMap(&Hive->DirtyBitMap,
		      Hive->BitmapBuffer,
		      BitmapSize * 8);
  RtlClearAllBits(&Hive->DirtyBitMap);
  Hive->HiveDirty = FALSE;


  /* Initialize the hive's executive resource */
  ExInitializeResourceLite(&Hive->HiveResource);

  /* Acquire hive list lock exclusively */
  ExAcquireResourceExclusiveLite(&CmiHiveListLock, TRUE);

  /* Add the new hive to the hive list */
  InsertTailList(&CmiHiveListHead, &Hive->HiveList);

  /* Release hive list lock */
  ExReleaseResourceLite(&CmiHiveListLock);
CHECKPOINT1;

  *RegistryHive = Hive;

  return TRUE;
}


BOOLEAN
CmImportSystemHive(PCHAR ChunkBase,
		   ULONG ChunkSize)
{
  PREGISTRY_HIVE RegistryHive;
//  UNICODE_STRING ParentKeyName;
//  UNICODE_STRING SubKeyName;
//  NTSTATUS Status;

  OBJECT_ATTRIBUTES ObjectAttributes;
  PKEY_OBJECT NewKey;
  HANDLE KeyHandle;
  UNICODE_STRING KeyName;
  NTSTATUS Status;

  if (strncmp (ChunkBase, "REGEDIT4", 8) == 0)
    {
      DPRINT1("Found 'REGEDIT4' magic\n");
      CmImportTextHive (ChunkBase, ChunkSize);
      return TRUE;
    }
  else if (strncmp (ChunkBase, "regf", 4) == 0)
    {
      DPRINT1("Found '%*s' magic\n", 4, ChunkBase);
      if (!CmImportBinarySystemHive (ChunkBase, ChunkSize, &RegistryHive))
	{
	  return FALSE;
	}



      RtlInitUnicodeString(&KeyName,
			   L"\\Registry\\Machine\\System");
      InitializeObjectAttributes(&ObjectAttributes,
				 &KeyName,
				 0,
				 NULL,
				 NULL);

      Status = ObCreateObject(&KeyHandle,
			      STANDARD_RIGHTS_REQUIRED,
			      &ObjectAttributes,
			      CmiKeyType,
			      (PVOID*)&NewKey);
      if (!NT_SUCCESS(Status))
	{
	  DPRINT1("ObCreateObject() failed (Status %lx)\n", Status);
	  KeBugCheck(0);
//	  CmiRemoveRegistryHive(RegistryHive);
//	  return(Status);
	}

      NewKey->RegistryHive = RegistryHive;
      NewKey->BlockOffset = RegistryHive->HiveHeader->RootKeyCell;
      NewKey->KeyCell = CmiGetBlock(RegistryHive, NewKey->BlockOffset, NULL);
      NewKey->Flags = 0;
      NewKey->NumberOfSubKeys = 0;
      NewKey->SubKeys = ExAllocatePool(PagedPool,
      NewKey->KeyCell->NumberOfSubKeys * sizeof(DWORD));

      if ((NewKey->SubKeys == NULL) && (NewKey->KeyCell->NumberOfSubKeys != 0))
	{
	  DPRINT1("NumberOfSubKeys %d\n", NewKey->KeyCell->NumberOfSubKeys);
	  KeBugCheck(0);
//	  ZwClose(NewKey);
//	  CmiRemoveRegistryHive(RegistryHive);
//	  return(STATUS_INSUFFICIENT_RESOURCES);
	}

      NewKey->SizeOfSubKeys = NewKey->KeyCell->NumberOfSubKeys;
      NewKey->NameSize = strlen("System");
      NewKey->Name = ExAllocatePool(PagedPool, NewKey->NameSize);

      if ((NewKey->Name == NULL) && (NewKey->NameSize != 0))
	{
	  DPRINT1("NewKey->NameSize %d\n", NewKey->NameSize);
	  if (NewKey->SubKeys != NULL)
	    ExFreePool(NewKey->SubKeys);
	  KeBugCheck(0);
//	  ZwClose(NewKey);
//	  CmiRemoveRegistryHive(RegistryHive);
//	  return(STATUS_INSUFFICIENT_RESOURCES);
	}

      memcpy(NewKey->Name, "System", NewKey->NameSize);
      CmiAddKeyToList(CmiMachineKey, NewKey);


#if 0
      RtlInitUnicodeString (&ParentKeyName,
			    REG_MACHINE_KEY_NAME);
      RtlInitUnicodeString (&SubKeyName,
			    L"System");

      Status = CmiLinkHive (&ParentKeyName,
			    &SubKeyName,
			    Hive);
      if (!NT_SUCCESS(Status))
	{
	  return FALSE;
	}
#endif
      return TRUE;
    }

  return FALSE;
}


BOOLEAN
CmImportHardwareHive(PCHAR ChunkBase,
		     ULONG ChunkSize)
{
  DPRINT1("CmImportHardwareHive() called\n");

  return TRUE;
}

/* EOF */
