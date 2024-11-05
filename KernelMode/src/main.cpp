#include <ntifs.h>

extern "C" {
    NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);
    NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress,
        PEPROCESS TargetProcess, PVOID TargetAddress,
        SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode,
        PSIZE_T ReturnSize);
}

#ifdef DEBUG
void debug_print(PCSTR text) {
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, text));
}
#else
#define debug_print(text)
#endif

namespace driver {
    namespace codes {
        constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    }

    struct Request {
        HANDLE process_id;
        PVOID target;
        PVOID buffer;
        SIZE_T size;
        SIZE_T return_size;
    };

    NTSTATUS create(PDEVICE_OBJECT device_object, PIRP irp) {
        UNREFERENCED_PARAMETER(device_object);
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return irp->IoStatus.Status;
    }

    NTSTATUS close(PDEVICE_OBJECT device_object, PIRP irp) {
        UNREFERENCED_PARAMETER(device_object);
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return irp->IoStatus.Status;
    }

    NTSTATUS device_control(PDEVICE_OBJECT device_object, PIRP irp) {
        UNREFERENCED_PARAMETER(device_object);

        debug_print("[+] Device control called.\n");
        NTSTATUS status = STATUS_INVALID_PARAMETER;
        PIO_STACK_LOCATION stack_irp = IoGetCurrentIrpStackLocation(irp);
        auto request = reinterpret_cast<Request*>(irp->AssociatedIrp.SystemBuffer);

        if (!stack_irp || !request) {
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return status;
        }

        static PEPROCESS target_process = nullptr;
        const ULONG control_code = stack_irp->Parameters.DeviceIoControl.IoControlCode;

        switch (control_code) {
        case codes::attach:
            status = PsLookupProcessByProcessId(request->process_id, &target_process);
            break;
        case codes::read:
            if (target_process != nullptr) {
                status = MmCopyVirtualMemory(target_process, request->target,
                    PsGetCurrentProcess(), request->buffer,
                    request->size, KernelMode, &request->return_size);
            }
            break;
        case codes::write:
            if (target_process != nullptr) {
                status = MmCopyVirtualMemory(PsGetCurrentProcess(), request->buffer,
                    target_process, request->target,
                    request->size, KernelMode, &request->return_size);
            }
            break;
        default:
            break;
        }

        irp->IoStatus.Status = status;
        irp->IoStatus.Information = sizeof(Request);
        IoCompleteRequest(irp, IO_NO_INCREMENT);

        return status;
    }
}

NTSTATUS driver_main(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
    UNREFERENCED_PARAMETER(registry_path);

    UNICODE_STRING device_name = {};
    RtlInitUnicodeString(&device_name, L"\\Device\\SexyDriver");

    PDEVICE_OBJECT device_object = nullptr;
    NTSTATUS status = IoCreateDevice(driver_object, 0, &device_name, FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN, FALSE, &device_object);
    if (!NT_SUCCESS(status)) {
        debug_print("[-] Failed to create driver device.\n");
        return status;
    }

    UNICODE_STRING symbolic_link = {};
    RtlInitUnicodeString(&symbolic_link, L"\\DosDevices\\SexyDriver");
    status = IoCreateSymbolicLink(&symbolic_link, &device_name);

    if (!NT_SUCCESS(status)) {
        debug_print("[-] Failed to establish symbolic link.\n");
        IoDeleteDevice(device_object);
        return status;
    }

    device_object->Flags |= DO_BUFFERED_IO;
    driver_object->MajorFunction[IRP_MJ_CREATE] = driver::create;
    driver_object->MajorFunction[IRP_MJ_CLOSE] = driver::close;
    driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver::device_control;
    device_object->Flags &= ~DO_DEVICE_INITIALIZING;

    debug_print("[+] Driver successfully loaded.\n");
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
    UNREFERENCED_PARAMETER(driver_object);
    UNREFERENCED_PARAMETER(registry_path);

    debug_print("[+] Initializing driver entry.\n");

    UNICODE_STRING driver_name = {};
    RtlInitUnicodeString(&driver_name, L"\\Driver\\SexyDriver");

    return IoCreateDriver(&driver_name, &driver_main);
}
