#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int, int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_HandleYield();

// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

//different processes states
char* statesNames[5] = {"NEW", "READY", "EXECUTING", "BLOCKED", "EXIT"};

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation
int initialPID=PROCESSTABLEMAXSIZE - 1;

// Begin indes for daemons in programList
int baseDaemonsInProgramList; 

// Array that contains the identifiers of the READY processes
heapItem readyToRunQueues[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};

char* queueNames[NUMBEROFQUEUES] = {"USER", "DAEMONS"};
// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code
	programFile=fopen("OperatingSystemCode", "r");
	if (programFile==NULL){
		// Show red message "FATAL ERROR: Missing Operating System!\n"
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing Operating System!\n");
		exit(1);		
	}

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(programFile);

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++){
		processTable[i].busy=0;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list  all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);
	
	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();
	
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);		
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
int OperatingSystem_PrepareStudentsDaemons(int programListDaemonsBase) {

	// Prepare aditionals daemons here
	// index for aditionals daemons program in programList
	// programList[programListDaemonsBase]=(PROGRAMS_DATA *) malloc(sizeof(PROGRAMS_DATA));
	// programList[programListDaemonsBase]->executableName="studentsDaemonNameProgram";
	// programList[programListDaemonsBase]->arrivalTime=0;
	// programList[programListDaemonsBase]->type=DAEMONPROGRAM; // daemon program
	// programListDaemonsBase++

	return programListDaemonsBase;
};


// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// 			command line and daemons programs
int OperatingSystem_LongTermScheduler() {
  
	int PID, i,
		numberOfSuccessfullyCreatedProcesses=0;
	
	for (i=0; programList[i]!=NULL && i<PROGRAMSMAXNUMBER ; i++) {
		PID = OperatingSystem_CreateProcess(i);

		if(PID == NOFREEENTRY)
		{
			ComputerSystem_DebugMessage(103, ERROR, programList[i]->executableName);
		}
		else if(PID == PROGRAMDOESNOTEXIST)
		{
			ComputerSystem_DebugMessage(104, ERROR, programList[i]->executableName, "it does not exist");
		}
		else if(PID == PROGRAMNOTVALID)
		{
			ComputerSystem_DebugMessage(104, ERROR, programList[i]->executableName, "invalid priority or size");
		}
		else if(PID == TOOBIGPROCESS)
		{
			ComputerSystem_DebugMessage(105, ERROR, programList[i]->executableName);
		}
		else{
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type==USERPROGRAM){ 
				numberOfNotTerminatedUserProcesses++;
				// Move process to the ready state
				OperatingSystem_MoveToTheREADYState(PID, USERPROCCESSQUEUE);
			}
			else
			{
				OperatingSystem_MoveToTheREADYState(PID, DAEMONSQUEUE);
			}
		}
	}

	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}


// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();

	if(PID == NOFREEENTRY)
	{
		return NOFREEENTRY;
	}

	// Check if programFile exists
	programFile=fopen(executableProgram->executableName, "r");
	if (programFile==NULL){
		return PROGRAMDOESNOTEXIST;
	}

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(programFile);	
	priority=OperatingSystem_ObtainPriority(programFile);

	if (processSize > MAINMEMORYSECTIONSIZE || processSize <= 0 || priority < 0)
	{
		if(processSize > MAINMEMORYSECTIONSIZE)
		{
			return TOOBIGPROCESS;
		}
		return PROGRAMNOTVALID;
	}

	// Obtain the priority for the process
	
	// Obtain enough memory space
 	loadingPhysicalAddress=OperatingSystem_ObtainMainMemory(processSize, PID);

	// Load program in the allocated memory
	int resultOfLoading = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	if(resultOfLoading == TOOBIGPROCESS)
	{
		return TOOBIGPROCESS;
	}
	
	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	
	// Show message "Process [PID] created from program [executableName]\n"
	ComputerSystem_DebugMessage(70,INIT,PID,executableProgram->executableName);
	
	return PID;
}

//Function to show processes ready to execute in the queue
void OperatingSystem_PrintReadyToRunQueue()
{
	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE);
	for(int i = 0; i < numberOfReadyToRunProcesses[USERPROCCESSQUEUE]; i++)
	{
		int PID = readyToRunQueues[USERPROCCESSQUEUE][i].info;
		if(i == 0 && numberOfReadyToRunProcesses[USERPROCCESSQUEUE] <= 1)
			ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
		else if(i == 0 && numberOfReadyToRunProcesses[USERPROCCESSQUEUE] > 1)
			ComputerSystem_DebugMessage(112, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
		else if(i + 1 == numberOfReadyToRunProcesses[USERPROCCESSQUEUE])
			ComputerSystem_DebugMessage(109, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
		else
			ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
	}

	for(int i = 0; i < numberOfReadyToRunProcesses[DAEMONSQUEUE]; i++)
	{
		int PID = readyToRunQueues[DAEMONSQUEUE][i].info;
		if(i == 0 && numberOfReadyToRunProcesses[DAEMONSQUEUE] <= 1)
			ComputerSystem_DebugMessage(114, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
		else if(i == 0 && numberOfReadyToRunProcesses[DAEMONSQUEUE] > 1)
			ComputerSystem_DebugMessage(113, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
		else if(i + 1 == numberOfReadyToRunProcesses[DAEMONSQUEUE])
			ComputerSystem_DebugMessage(109, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
		else
			ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, PID , processTable[PID].priority);
	}
}

// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {

 	if (processSize>MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;
	
 	return PID*MAINMEMORYSECTIONSIZE;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	processTable[PID].state=NEW;
	ComputerSystem_DebugMessage(111, SYSPROC, PID, programList[processPLIndex]->executableName, "NEW");
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
	} 
	else {
		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
	}

}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID, int typeQueue) {
	
	if (Heap_add(PID, readyToRunQueues[typeQueue] ,typeQueue ,&numberOfReadyToRunProcesses[typeQueue] ,PROCESSTABLEMAXSIZE)>=0) {
		switch (processTable[PID].state)
		{
			case NEW:
				ComputerSystem_DebugMessage(110, SYSPROC, PID, programList[processTable[PID].programListIndex]->executableName, "NEW", "READY");
				break;
			case EXECUTING:
				ComputerSystem_DebugMessage(110, SYSPROC, PID, programList[processTable[PID].programListIndex]->executableName, "EXECUTING", "READY");
				break;
		}
		processTable[PID].state=READY;
	}
	OperatingSystem_PrintReadyToRunQueue();
}


// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess;

	if(numberOfNotTerminatedUserProcesses > 0)
		selectedProcess=OperatingSystem_ExtractFromReadyToRun(USERPROCCESSQUEUE);
	else
		selectedProcess=OperatingSystem_ExtractFromReadyToRun(DAEMONPROGRAM);
	
	return selectedProcess;
}


// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun(int typeQueue) {
  
	int selectedProcess=NOPROCESS;
	selectedProcess=Heap_poll(readyToRunQueues[typeQueue] ,typeQueue ,&numberOfReadyToRunProcesses[typeQueue]);
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	processTable[PID].state=EXECUTING;
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
	ComputerSystem_DebugMessage(110, SYSPROC, PID, programList[processTable[PID].programListIndex]->executableName, "READY", "EXECUTING");
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1,processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2,processTable[PID].copyOfPSWRegister);
	
	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	if(numberOfNotTerminatedUserProcesses == 0)
	{
		OperatingSystem_MoveToTheREADYState(executingProcessID, DAEMONSQUEUE);	
	}
	else
	{
		OperatingSystem_MoveToTheREADYState(executingProcessID, USERPROCCESSQUEUE);
	}
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);
	
}


// Exception management routine
void OperatingSystem_HandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	ComputerSystem_DebugMessage(71,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
	
	OperatingSystem_TerminateProcess();
}


// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;
  	
	processTable[executingProcessID].state=EXIT;
	
	if (programList[processTable[executingProcessID].programListIndex]->type==USERPROGRAM) 
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses==0) {
		if (executingProcessID==sipID) {
			// finishing sipID, change PC to address of OS HALT instruction
			OperatingSystem_TerminatingSIP();
			ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");
			return; // Don't dispatch any process
		}
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			ComputerSystem_DebugMessage(72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			ComputerSystem_DebugMessage(73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateProcess();
			break;
		case SYSCALL_YIELD:
			OperatingSystem_HandleYield();
			break;
	}
}

void OperatingSystem_HandleYield()
{
	int nextProcess;
	if(processTable[executingProcessID].queueID == USERPROCCESSQUEUE)
	{
		int PID_OF_NEXT = Heap_getFirst(readyToRunQueues[USERPROCCESSQUEUE], numberOfReadyToRunProcesses[USERPROCCESSQUEUE]);
		if(processTable[PID_OF_NEXT].priority == processTable[executingProcessID].priority)
		{
			ComputerSystem_DebugMessage(115, SHORTTERMSCHEDULE, executingProcessID, programList[executingProcessID]->executableName, 
			PID_OF_NEXT, programList[PID_OF_NEXT]->executableName);
			nextProcess = OperatingSystem_ShortTermScheduler();
			OperatingSystem_PreemptRunningProcess();
			OperatingSystem_Dispatch(nextProcess);
		}
		else
			return;
	}
	else
	{
		int PID_OF_NEXT = Heap_getFirst(readyToRunQueues[DAEMONSQUEUE], numberOfReadyToRunProcesses[DAEMONSQUEUE]);
		if(processTable[PID_OF_NEXT].priority == processTable[executingProcessID].priority)
		{
			ComputerSystem_DebugMessage(115, SHORTTERMSCHEDULE, executingProcessID, programList[executingProcessID]->executableName, 
			PID_OF_NEXT, programList[PID_OF_NEXT]->executableName);
			nextProcess = OperatingSystem_ShortTermScheduler();
			OperatingSystem_PreemptRunningProcess();
			OperatingSystem_Dispatch(nextProcess);
			Processor_SetPC(processTable[nextProcess].copyOfPCRegister);
		}
		else
			return;
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
	}

}

