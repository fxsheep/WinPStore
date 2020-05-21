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

//RTL_QUERY_REGISTRY_TABLE ConfigTable[] =
//{

//};

BOOLEAN
RegQueryPStoreConfigs(
    VOID
)
{
    NTSTATUS status = STATUS_NOT_FOUND;
#if 0
    status = RtlQueryRegistryValues(RTL_REGISTRY_CONTROL,
        L"WinPStore",
        ConfigTable,
        NULL,
        NULL);
#endif

    if (NT_SUCCESS(status) == FALSE)
    {
        return FALSE;
    }


    PStoreEnabled = FALSE;

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
    _In_ PDRIVER_OBJECT     DriverObject,
    _In_ PUNICODE_STRING    RegistryPath
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status = STATUS_NOT_FOUND;
    PMDL Mdl;
#if 0
    if (RegQueryPStoreConfigs() == FALSE) {
        KdPrint(("WinPStore: Failed to lookup pstore configs in registry\n"));
        status = STATUS_NOT_FOUND;
    }
#endif

    //Test
    PStoreEnabled = TRUE;
    PStoreMemorySize = 4096;
    PStorePhysicalAddress.HighPart = 0x0L;
    PStorePhysicalAddress.LowPart = 0x90000000L;
    //PStoreVirtualAddress = ExAllocatePoolWithTag(NonPagedPool, PStoreMemorySize, 0);

    KdPrint(("WinPStore: Driver 1530 init start!\n"));


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
            KdPrint(("WinPStore: Failed to allocate physical pages\n"));
        }

        if (PStoreVirtualAddress) {
            LogBuffer.Buffer = PStoreVirtualAddress;
            LogBuffer.Offset = 0;
            status = DbgSetDebugPrintCallback(LogDebugPrint, TRUE);
            KdPrint(("WinPStore:DbgSetDebugPrintCallback status: %d\n", status));
        }
    }

    if (!NT_SUCCESS(status)) {
fail:
        KdPrint(("WinPStore: Failed to register pstore driver\n"));
    }
    
    return status;
}