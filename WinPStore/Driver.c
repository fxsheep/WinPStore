#include <ntddk.h>
#include <wdf.h>
DRIVER_INITIALIZE DriverEntry;

typedef struct _LOG_BUF {
    PCHAR       Buffer;
    ULONG       Offset;
} LOG_BUF;

typedef VOID
(*DBG_PRINT_CALLBACK)(
    PANSI_STRING    Ansi,
    ULONG           ComponentId,
    ULONG           Level
    );

BOOLEAN           PStoreEnabled = FALSE;
UINT32            RegPStoreEnabled;
PHYSICAL_ADDRESS  PStorePhysicalAddress = { 0L, 0L };
PVOID             PStoreVirtualAddress;
ULONG             PStoreMemorySize;
LOG_BUF           LogBuffer;

MM_MDL_ROUTINE MmMdlRoutine;

void MmMdlRoutine(
    PVOID DriverContext,
    PVOID MappedVa
)
{
    UNREFERENCED_PARAMETER(DriverContext);
    PStoreVirtualAddress = MappedVa;
}

RTL_QUERY_REGISTRY_TABLE PStoreRegConfigTable[] =
{
    {
        NULL,
        RTL_QUERY_REGISTRY_DIRECT,
        L"PStoreEnabled",
        &RegPStoreEnabled,
        REG_DWORD,
        NULL,
        sizeof(UINT32)
    },

    //FIXME use single QWORD instead
#if 0
    {
        NULL,
        RTL_QUERY_REGISTRY_DIRECT,
        L"PStoreMemoryAddress",
        &PStorePhysicalAddress.QuadPart,
        REG_QWORD,
        NULL,
        sizeof(ULONG)
    },
#endif

    {
        NULL,
        RTL_QUERY_REGISTRY_DIRECT,
        L"PStoreMemoryAddrLow",
        &PStorePhysicalAddress.LowPart,
        REG_DWORD,
        NULL,
        sizeof(UINT32)
    },
 
    {
        NULL,
        RTL_QUERY_REGISTRY_DIRECT,
        L"PStoreMemoryAddrHigh",
        &PStorePhysicalAddress.HighPart,
        REG_DWORD,
        NULL,
        sizeof(UINT32)
    },
    
    {
        NULL,
        RTL_QUERY_REGISTRY_DIRECT,
        L"PStoreMemorySize",
        &PStoreMemorySize,
        REG_DWORD,
        NULL,
        sizeof(UINT32)
    },

    //Terminator
    {
        NULL,
        0,
        NULL,
        0,
        REG_DWORD,
        NULL,
        0
    }
};

BOOLEAN
RegQueryPStoreConfigs(
    VOID
)
{
    NTSTATUS status = STATUS_NOT_FOUND;
    status = RtlQueryRegistryValues(RTL_REGISTRY_CONTROL,
        L"WinPStore",
        PStoreRegConfigTable,
        NULL,
        NULL);

    if (NT_SUCCESS(status) == FALSE)
    {
        return FALSE;
    }

    if (RegPStoreEnabled == 1)
    {
        KdPrint(("WinPStore: PStorePhysicalAddressHigh: %x\n", PStorePhysicalAddress.HighPart));
        KdPrint(("WinPStore: PStorePhysicalAddressLow: %x\n", PStorePhysicalAddress.LowPart));
        KdPrint(("WinPStore: PStoreMemorySize: %d\n", PStoreMemorySize));
        if ( PStorePhysicalAddress.QuadPart == 0L || PStoreMemorySize == 0L)
        {
            return FALSE;
        }
        PStoreEnabled = TRUE;
    }

    return TRUE;
}


VOID
LogDebugPrint(
    IN  PANSI_STRING    Ansi,
    IN  ULONG           ComponentId,
    IN  ULONG           Level
)
{
    UNREFERENCED_PARAMETER(ComponentId);
    UNREFERENCED_PARAMETER(Level);

    if (Ansi->Length == 0 || Ansi->Buffer == NULL)
        return;

    //Avoid deadloops
    if (Ansi->Buffer[0] == 'W' &&
        Ansi->Buffer[1] == 'i' &&
        Ansi->Buffer[2] == 'n' &&
        Ansi->Buffer[3] == 'P' &&
        Ansi->Buffer[4] == 'S' &&
        Ansi->Buffer[5] == 't')
        return;

    if (LogBuffer.Offset + Ansi->Length > PStoreMemorySize)
    {
        LogBuffer.Offset = 0;
    }

    KdPrint(("WinPStore: Copying memory: %x to %x\n",
        Ansi->Buffer,
        LogBuffer.Buffer + LogBuffer.Offset));
    RtlCopyMemory(LogBuffer.Buffer + LogBuffer.Offset, Ansi->Buffer, Ansi->Length);
    LogBuffer.Offset += Ansi->Length;

}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT     DriverObject,
    IN PUNICODE_STRING    RegistryPath
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status = STATUS_SUCCESS;
    PMDL Mdl;

    KdPrint(("WinPStore: DriverEntry\n"));

    if (RegQueryPStoreConfigs() == FALSE) {
        KdPrint(("WinPStore: Failed to lookup pstore configs in registry\n"));
        status = STATUS_NOT_FOUND;
    }

    KdPrint(("WinPStore: RegPStoreEnabled: %d\n", RegPStoreEnabled));
    KdPrint(("WinPStore: PStoreEnabled: %d\n", PStoreEnabled));
    //Test
#if 0
    PStoreEnabled = TRUE;
    PStoreMemorySize = 4096;
    PStorePhysicalAddress.HighPart = 0x0L;
    PStorePhysicalAddress.LowPart = 0x02000000L;
    //PStoreVirtualAddress = ExAllocatePoolWithTag(NonPagedPool, PStoreMemorySize, 0);
#endif

    if (PStoreEnabled) {
        Mdl = MmAllocatePagesForMdl(PStorePhysicalAddress,
            PStorePhysicalAddress,
            RtlConvertUlongToLargeInteger(0),
            PStoreMemorySize);

        if (!Mdl) {
            KdPrint(("WinPStore: Failed to allocate physical pages\n"));
            goto fail;
        }

        status = MmMapMdl(Mdl,
            PAGE_READWRITE,
            MmMdlRoutine,
            NULL);

        if (!NT_SUCCESS(status)) {
            KdPrint(("WinPStore: Failed to map physical pages\n"));
        }

        if (PStoreVirtualAddress) {
            LogBuffer.Buffer = PStoreVirtualAddress;
            LogBuffer.Offset = 0;
            KdPrint(("WinPStore: va=0x%x, pa=0x%x\n",PStoreVirtualAddress,PStorePhysicalAddress));
            status = DbgSetDebugPrintCallback(LogDebugPrint, TRUE);
            KdPrint(("WinPStore: DbgSetDebugPrintCallback status: %d\n", status));
        }
        if (!NT_SUCCESS(status)) {
fail:
            KdPrint(("WinPStore: Failed to register pstore driver\n"));
        }
    }
    else {
        KdPrint(("WinPStore: pstore driver disabled\n"));
    }

    return status;
}

VOID
DriverUnload(
    IN PDRIVER_OBJECT     DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DbgSetDebugPrintCallback(LogDebugPrint, FALSE);
    KdPrint(("WinPStore: pstore driver unloaded\n"));
    return;
}