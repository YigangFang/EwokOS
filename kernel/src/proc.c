#include <mmu.h>
#include <proc.h>
#include <kernel.h>
#include <kalloc.h>
#include <lib/string.h>
#include <system.h>
#include <types.h>
#include <elf.h>
#include <kserv.h>

ProcessT _processTable[PROCESS_COUNT_MAX];

__attribute__((__aligned__(PAGE_DIR_SIZE)))
PageDirEntryT _processVM[PROCESS_COUNT_MAX][PAGE_DIR_NUM];

ProcessT* _currentProcess = NULL;

/* proc_init initializes the process sub-system. */
void procInit(void)
{
	for (int i = 0; i < PROCESS_COUNT_MAX; i++)
		_processTable[i].state = UNUSED;
	_currentProcess = NULL;
}

/* proc_exapnad_memory expands the heap size of the given process. */
bool procExpandMemory(void *p, int pageCount)
{
	ProcessT* proc = (ProcessT*)p;	
	for (int i = 0; i < pageCount; i++) {
		char *page = kalloc();
		if(page == NULL) {
			procShrinkMemory(proc, i);
			return false;
		}
		memset(page, 0, PAGE_SIZE);

		mapPage(proc->vm, 
				proc->heapSize,
				V2P(page),
				AP_RW_RW);
		proc->heapSize += PAGE_SIZE;
	}
	return true;
}

/* proc_shrink_memory shrinks the heap size of the given process. */
void procShrinkMemory(void* p, int pageCount)
{
	ProcessT* proc = (ProcessT*)p;	
	for (int i = 0; i < pageCount; i++) {
		uint32_t virtualAddr = proc->heapSize - PAGE_SIZE;
		uint32_t physicalAddr = resolvePhyAddress(proc->vm, virtualAddr);

		//get the kernel address for kalloc/kfree
		uint32_t kernelAddr = P2V(physicalAddr);
		kfree((void *) kernelAddr);

		unmapPage(proc->vm, virtualAddr);

		proc->heapSize -= PAGE_SIZE;
		if (proc->heapSize == 0) {
			break;
		}
	}
}

static void* procGetMemTail(void* p) {
	ProcessT* proc = (ProcessT*)p;	
	return (void*)proc->heapSize;
}

/* proc_creates allocates a new process and returns it. */
ProcessT *procCreate(void)
{
	ProcessT *proc = NULL;
	int pid = -1;
	PageDirEntryT *vm = NULL;
	char *kernelStack = NULL;
	char *userStack = NULL;

	for (int i = 0; i < PROCESS_COUNT_MAX; i++) {
		if (_processTable[i].state == UNUSED) {
			pid = i + 1;
			break;
		}
	}

	if (pid == -1)
		return NULL;

	kernelStack = kalloc();
	userStack = kalloc();

	vm = _processVM[pid - 1];
	setKernelVM(vm);

	mapPage(vm, 
		KERNEL_STACK_BOTTOM,
		V2P(kernelStack),
		AP_RW_R);

	mapPage(vm,
		USER_STACK_BOTTOM,
		V2P(userStack),
		AP_RW_RW);


	proc = &_processTable[pid - 1];
	proc->pid = pid;
	proc->state = CREATED;
	proc->vm = vm;
	proc->heapSize = 0;
	proc->kernelStack = kernelStack;
	proc->userStack = userStack;
	proc->waitPid = -1;

	proc->mallocMan.arg = (void*)proc;
	proc->mallocMan.mHead = 0;
	proc->mallocMan.mTail = 0;
	proc->mallocMan.expand = procExpandMemory;
	proc->mallocMan.shrink = procShrinkMemory;
	proc->mallocMan.getMemTail = procGetMemTail;

	return proc;
}

int *getCurrentContext(void)
{
	return _currentProcess->context;
}

/* proc_free frees all resources allocated by proc. */
void procFree(ProcessT *proc)
{
	kservUnreg(proc);
	kfree(proc->kernelStack);
	kfree(proc->userStack);
	procShrinkMemory(proc, proc->heapSize / PAGE_SIZE);
	freePageTables(proc->vm);
	proc->state = UNUSED;
}

/* proc_load loads the given ELF process image into the given process. */
bool procLoad(ProcessT *proc, const char *procImage)
{
	int progHeaderOffset = 0;
	int progHeaderCount = 0;
	int i = 0;

	struct ElfHeader *header = (struct ElfHeader *) procImage;
	if (header->type != ELFTYPE_EXECUTABLE)
		return false;

	progHeaderOffset = header->phoff;
	progHeaderCount = header->phnum;

	for (i = 0; i < progHeaderCount; i++) {
		uint32_t j = 0;
		struct ElfProgramHeader *header = (void *) (procImage + progHeaderOffset);

		/* make enough room for this section */
		while (proc->heapSize < header->vaddr + header->memsz)
			procExpandMemory(proc, 1);

		/* copy the section */
		for (j = 0; j < header->memsz; j++) {
			int vaddr = header->vaddr + j;
			int paddr = resolvePhyAddress(proc->vm, vaddr);
			char *ptr = (char *) P2V(paddr);
			int imageOff = header->off + j;
			*ptr = procImage[imageOff]; 
		}

		progHeaderOffset += sizeof(struct ElfProgramHeader);
	}

	proc->entry = (EntryFunctionT) header->entry;
	proc->state = READY;

	memset(proc->context, 0, sizeof(proc->context));
	proc->context[CPSR] = 0x10; //CPSR 0x10 for user mode
	proc->context[RESTART_ADDR] = (int) proc->entry;
	proc->context[SP] = USER_STACK_BOTTOM + PAGE_SIZE;

	return true;
}

/* proc_start starts running the given process. */
void procStart(ProcessT *proc)
{
	_currentProcess = proc;

	__setTranslationTableBase((uint32_t) V2P(proc->vm));

	/* clear TLB */
	__asm__ volatile("mov R4, #0");
	__asm__ volatile("MCR p15, 0, R4, c8, c7, 0");

	__switchToContext(proc->context);
}

ProcessT* procGet(int pid) 
{
	if (pid > 0 && pid <= PROCESS_COUNT_MAX)
		return &_processTable[pid - 1];
	else
		return NULL;
}
