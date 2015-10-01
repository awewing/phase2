/*------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

  ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "message.h"

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
int getNextID();
int getSlot();
void removeSlot(int mboxID);
void static clockHandler(int dev, void *args);
void static diskHandler(int dev, void *args);
void static terminalHandler(int dev, void *args);
static void enableInterrupts();
static void disableInterrupts();
void check_kernel_mode(char* name);
void sendToSlot(mailbox *mbox, void *msg_ptr, int msg_size);
void sendToBlocked(mailbox *mbox, void *msg_ptr, int msg_size);
int blockReceiver(mailbox *mbox, void *msg_ptr, int msg_size);
process *findFirstBlocked(mailbox *mbox, int status);
/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 1;

int numBoxes = 0;
int nextFreeBox = 0;

int numSlots = 0;

// the mail boxes 
mailbox MailBoxTable[MAXMBOX];
mailSlot MailSlots[MAXSLOTS];
mailbox clockBox;
mailbox termBoxes[TERMBOXMAX];
mailbox diskBoxes[DISKBOXMAX];

process processTable[MAXPROC];

int clockTicks = 0;
// also need array of mail slots, array of function ptrs to system call 
// handlers, ...

/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): at beginning\n");

    // check we are in kernel
    check_kernel_mode("start1");

    // Disable interrupts
    disableInterrupts();

    // Initialize the mail box table, slots, & other data structures.
    for (int i = 0; i < MAXMBOX; i++) {
        MailBoxTable[i].mboxID = -1;
        MailBoxTable[i].numSlots = -1;
        MailBoxTable[i].numSlotsUsed = -1;
        MailBoxTable[i].slotSize = -1;
        MailBoxTable[i].headPtr = NULL;
        MailBoxTable[i].endPtr = NULL;
        MailBoxTable[i].blockStatus = 1;
    }

    for (int i = 0; i < MAXSLOTS; i++) {
        MailSlots[i].mboxID = -1;
        MailSlots[i].status = -1;
        MailSlots[i].nextSlot = NULL;
        MailSlots[i].message[0] = '\0';
        MailSlots[i].size = -1;
    }

    for (int i = 0; i < MAXPROC; i++) {
        processTable[i].pid = -1;
        processTable[i].blockStatus = NOT_BLOCKED;
        // processTable[i].message = NULL;
        processTable[i].size = -1;
        processTable[i].mboxID = -1;
        processTable[i].timeAdded = -1;
    }

    // Initialize USLOSS_IntVec and system call handlers,
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;
    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;
    USLOSS_IntVec[USLOSS_TERM_INT] = terminalHandler;

    // allocate mailboxes for interrupt handlers.  Etc... 
    clockBox = MailBoxTable[MboxCreate(0, 0)];

    for (int i = 0; i < TERMBOXMAX; i++) {
        termBoxes[i] = MailBoxTable[MboxCreate(0, 0)];
    }

    for (int i = 0; i < DISKBOXMAX; i++) {
        diskBoxes[i] = MailBoxTable[MboxCreate(0, 0)];
    }

    // all done creating stuff, re enable interrupts
    enableInterrupts();

    // Create a process for start2, then block on a join until start2 quits
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): fork'ing start2 process\n");
    int kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
    int status;
    if ( join(&status) != kid_pid ) {
        USLOSS_Console("start2(): join returned something other than ");
        USLOSS_Console("start2's pid\n");
    }

    return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
                mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
    check_kernel_mode("MboxCreate");

    // find an id
    int ID = getNextID();

    // check slot_size isn't too big
    if (slot_size > MAXSLOTS) {
        return -1;
    }

    // no open mailboxes
    if (ID == -1) {
      return -1;
    }

    // initialize Values
    mailbox *mbox = &(MailBoxTable[ID]);
    mbox->mboxID = ID;
    mbox->numSlots = slots;
    mbox->numSlotsUsed = 0;
    mbox->headPtr = NULL;
    mbox->slotSize = slot_size;
    mbox->blockStatus = NOT_BLOCKED;

    return ID;
} /* MboxCreate */

int MBoxRelease(int mailboxID) {
    check_kernel_mode("MboxRelease");

    // get the mailbox
    mailbox *mbox = &(MailBoxTable[mailboxID]);

    // check to make sure the mail box is in use
    if (mbox->mboxID == -1) {
        return -1;
    }

    // null out the removed mailbox
    MailBoxTable[mailboxID].mboxID = -1;
    MailBoxTable[mailboxID].numSlots = -1;
    MailBoxTable[mailboxID].numSlotsUsed = -1;
    MailBoxTable[mailboxID].slotSize = -1;
    MailBoxTable[mailboxID].headPtr = NULL;
    MailBoxTable[mailboxID].endPtr = NULL;
    MailBoxTable[mailboxID].blockStatus = 1;

    // unblock all processes blocked on this mailbox
    for (int i = 0; i < MAXPROC; i++) {
        // find all processes blocked on said mailbox
        if (processTable[i].mboxID == mailboxID) {
            // zap those processes and unblock them
            zap(processTable[i].pid);
            unblockProc(processTable[i].pid);

            // remove their info from the procTable
            processTable[i].pid = -1;
            processTable[i].blockStatus = NOT_BLOCKED;
            processTable[i].message[0] = '\0';
            processTable[i].size = -1;
            processTable[i].mboxID = -1;
            processTable[i].timeAdded = -1;
        }
    }

    // check if zapped
    if (isZapped()) {
        return -3;
    }

    return 0;
}

/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
    check_kernel_mode("MboxSend");
    disableInterrupts();

    // get the mail box
    mailbox *mbox = &(MailBoxTable[mbox_id]);

    // check message size isn't too big
    if (msg_size > MAX_MESSAGE) {
        return -1;
    }
    else if (msg_size > mbox->slotSize) {
        return -1;
    }

    // checked to make sure process wasn't zapped
    if (isZapped()) {
        return -3;
    }

    // check to make mailbox still exists
    if (mbox->mboxID == -1) {
        return -3;
    }

    // figure out what to do & use helper functions
    if (mbox->numSlots > mbox->numSlotsUsed) {
        if (mbox->blockStatus == RECEIVE_BLOCKED) {
            // there are processes blocked on receive
            if (DEBUG2 && debugflag2) {
                USLOSS_Console("MboxSend(): calling sendToBlocked\n");
            }
            sendToBlocked(mbox, msg_ptr, msg_size);
        }
        else {
            // Simply add message to the mailbox
            if (DEBUG2 && debugflag2) {
                USLOSS_Console("MboxSend(): calling sendToSlot\n");
            } 
            sendToSlot(mbox, msg_ptr, msg_size);
        }
    }
    else {
        // No open slots, block sender
        if (DEBUG2 && debugflag2) {
            USLOSS_Console("MboxSend(): todo: block the sender");
        }
        // TODO:
        for (int i = 0; i < MAXPROC; i++) {

        }

    }

    //blockMe(SENDBLOCK);

    enableInterrupts();
    return 0;
} /* MboxSend */

/*
 * Helper function for send
 *  - put message in a slot
 */
void sendToSlot(mailbox *mbox, void *msg_ptr, int msg_size)
{
    // allowed to send a message
    // get the slot to put a message in
    mbox->numSlotsUsed++;
    slotPtr slot = &MailSlots[getSlot()];

    // assign the slot to a mailbox
    // check if this is the first slot in the mailbox, if so set this slot as the first slot in the box
    if (mbox->headPtr == NULL) {
        mbox->headPtr = slot;
    }
    // otherwise adjust who the old endptr's next is
    else {
        mbox->endPtr->nextSlot = slot;
    }

    // set the box's endptr to this new slot and inc the mailbox's size
    mbox->endPtr = slot;

    // put info in the slot
    slot->mboxID = mbox->mboxID;
    slot->status = 1;
    slot->nextSlot = NULL;
    memcpy(slot->message , msg_ptr, msg_size);
    slot->size = msg_size;

    if (DEBUG2 && debugflag2) {
        USLOSS_Console("sendToSlot() :headPtr: %s endPtr: %s\n", mbox->headPtr->message, mbox->endPtr->message);
    }

    // increment numSlotsUsed
    mbox->numSlotsUsed++;
}

/*
 * Helper function for send
 *  - give message to a proc blocked on receive
 */
void sendToBlocked(mailbox *mbox, void *msg_ptr, int msg_size) {
    process *earliestBlocked = findFirstBlocked(mbox, RECEIVE_BLOCKED);

    // Put msg into a place that the other process can reach.
    memcpy(earliestBlocked->message, msg_ptr, msg_size);
    if (DEBUG2 && debugflag2) {
        USLOSS_Console("sendToBlocked(): copying message '%s' to ptable element\n", earliestBlocked->message);
    }


    if (DEBUG2 && debugflag2) {
        USLOSS_Console("sendToBlocked(): message: %s\n", earliestBlocked->message);
        USLOSS_Console("sendToBlocked(): changing size from %d to %d\n", earliestBlocked->size, msg_size);
        USLOSS_Console("Unblocking process %d\n", earliestBlocked->pid);
    }

    earliestBlocked->size = msg_size;

    // Unblock the process
    earliestBlocked->blockStatus = NOT_BLOCKED;
    unblockProc(earliestBlocked->pid);
}

/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
    check_kernel_mode("MboxReceive");

    // get the mailbox
    mailbox *mbox = &(MailBoxTable[mbox_id]);

    // check message size isn't too big
    if (msg_size > MAX_MESSAGE) {
        return -1;
    }

    // no longer blocked
    // checked to make sure process wasn't zapped
    if (isZapped()) {
        return -3;
    }

    // check to make sure mailbox still exists
    if (mbox->mboxID == -1) {
        return -3;
    }

    // Different receive cases
    if (mbox->numSlotsUsed == 0) {
        mbox->blockStatus = RECEIVE_BLOCKED;
        int newSize = blockReceiver(mbox, msg_ptr, msg_size);
        
        // no longer blocked
        // checked to make sure process wasn't zapped
        if (isZapped()) {
            return -3;
        }

        return newSize;
    }
    else {
        // get message
        if (DEBUG2 && debugflag2) {
            USLOSS_Console("MboxReceive: getting message from slot\n");
        }
        slotPtr slot = mbox->headPtr;
        memcpy(msg_ptr, slot->message, msg_size);
        int returnSize = slot->size;

        // free the slot
        mbox->numSlotsUsed--;
        mbox->headPtr = mbox->headPtr->nextSlot;

        // removeSlot(mbox->mboxID);

        // check for blocked senders
        if (mbox->blockStatus == SEND_BLOCKED){
            process *blockedProc = findFirstBlocked(mbox, SEND_BLOCKED);
            unblockProc(blockedProc->pid);
            // after this sender will put its message into an open slot
        }

        return returnSize;
    }

} /* MboxReceive */

/*
 * Helper func for receive
 *  - puts proc info on ptable and blocks
 *  - returns size of message received
 */
int blockReceiver(mailbox *mbox, void *msg_ptr, int msg_size) {
    mbox->blockStatus = RECEIVE_BLOCKED;
    process *newEntry;
    for (int i = 0; i < MAXPROC; i++) {
        if (processTable[i].pid == -1) {
            newEntry = &processTable[i];
        }
    }

    if (DEBUG2 && debugflag2)

    if (newEntry == NULL) {
        // there are more than 50 procs in table, impossible, check removal of procs
        USLOSS_Halt(1);
    }

    newEntry->pid = getpid();
    newEntry->blockStatus = RECEIVE_BLOCKED;
    newEntry->size = msg_size;
    newEntry->mboxID = mbox->mboxID;
    newEntry->timeAdded = USLOSS_Clock();
    blockMe(RECEIVEBLOCK);

    //after block
    memcpy(msg_ptr, newEntry->message, msg_size);

    if (DEBUG2 && debugflag2) {
        USLOSS_Console("new message is %s\n", newEntry->message);
        USLOSS_Console("new message for receive is %s\n", msg_ptr);
    }

    return newEntry->size;
}

int MboxCondSend(int mailboxID, void *message, int message_size) {
    return 0;
}


/* ------------------------------------------------------------------------
   Name - findFirstBlocked
   Purpose - unblock the process that has been waiting the longest
                for sending, unblock process RECEIVE_BLOCKED and for
                receiving, unblock process SEND_BLOCKED
   Parameters - mbox, and status
                status will be whether we are unblocking a RECEIVE_BLOCKED
                process or a SEND_BLOCKED process
   Returns - void
   ----------------------------------------------------------------------- */
process *findFirstBlocked(mailbox *mbox, int status) {
    if (DEBUG2 && debugflag2) {
        USLOSS_Console("findFirstBlocked(): started\n");
    }

    process *currProc;
    process *earliestBlocked = NULL;
    int element = 0;

    for (int i = 0; i < MAXPROC; i++) {
        currProc = &processTable[i];
        
        if (currProc->mboxID == mbox->mboxID && currProc->blockStatus == status) {
            if (earliestBlocked == NULL) {
                earliestBlocked = currProc;
                element = i;
            }
            else {
                //find which has been waiting longer
                if (earliestBlocked->timeAdded > currProc->timeAdded) {
                    earliestBlocked = currProc;
                    element = i;
                }
            }
        }
    }

    if (DEBUG2 && debugflag2) {
        USLOSS_Console("findFirstBlocked: at element %d, out of loop, returning\n", element);
    }

    return earliestBlocked;
}

/* ------------------------------------------------------------------------
   Name - getNextID
   Purpose - get next mailbox id
   returns - mbox id or -1 if none are open.
   ----------------------------------------------------------------------- */
int getNextID() {
    int initial = nextFreeBox;

    // check for number of boxes in use
    if (numBoxes >= MAXMBOX) {
        return -1;
    }

    // go through all the boxes until you find a free one
    while (MailBoxTable[nextFreeBox].mboxID != -1) {
        nextFreeBox++;

        // if you get to the final box, reset
        if (nextFreeBox == MAXMBOX) {
            nextFreeBox = 0;
        }

        // if you loop all the way around quit
        if (nextFreeBox == initial) {
            return -1;
        }
    }

    // inc numboxes
    numBoxes++;

    return nextFreeBox;
} /* getNextID */

/* ------------------------------------------------------------------------
   Name - getSlot
   Purpose - find next open mail slot
   Parameters - none
   Returns - the address to the next open mail slot
   ----------------------------------------------------------------------- */
int getSlot() {
    int i; // loop variable

    // find a free slot in the slot array
    for (i = 0; i < MAXSLOTS; i++) {
        if (MailSlots[i].status != 1) {
            break;
        }
    }

    // if it got to this point without finding a free slot, halt
    if (i == MAXSLOTS) {
        USLOSS_Console("Out of free mailbox slots.\n");
        USLOSS_Halt(1);
    }

    // return the free slot index
    return i;
} /* mailSlot */

void removeSlot(int mboxID) {
    // get the mailbox and slot
    mailbox *mbox = &(MailBoxTable[mboxID]);
    slotPtr slot = mbox->endPtr;

    // reorder the mailboxes linked list
    // check to see if this was the only slot in use
    if (mbox->headPtr == slot) {
        // set both head and end to null
        mbox->headPtr = NULL;
        mbox->endPtr = NULL;
    }
    // otherwise, loop through the the linked list to find the end
    else {
        slotPtr newLast;
        for (newLast = mbox->headPtr; newLast->nextSlot->nextSlot != NULL; newLast = newLast->nextSlot) {
            ;       
        }

        // newLast is now the second to last slot
        // cut its link to the old last and set it as the new last
        newLast->nextSlot = NULL;
        mbox->endPtr = newLast;
    }

    // dec how many slots it is using
    mbox->numSlotsUsed--;

    // null out the slot
    slot->mboxID = -1;
    slot->status = -1;
    slot->nextSlot = NULL;
    slot->message[0] = '\0';
}

static void clockHandler(int dev, void *arg) {
    // check if dispatcher should be called
    if (readCurStartTime() >= 80000) {
        timeSlice();
    }

    // inc that a clock interrupt happened
    clockTicks++;

    // every fith interrupt do a conditional send to its mailbox
    if (clockTicks % 5 == 0) {
        MboxCondSend(clockBox.mboxID, NULL, 0);
    }
}

static void diskHandler(int dev, void *arg) {

}

static void terminalHandler(int dev, void *arg) {

}

/*
 * Enables the interrupts 
 */
static void enableInterrupts(int dev, void *args)
{
  /* turn the interrupts ON iff we are in kernel mode */
  if ( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
    //not in kernel mode
    USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
    USLOSS_Console("disable interrupts\n");
    USLOSS_Halt(1);
  } else {
    /* We ARE in kernel mode */
    USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
  }
}

/*
 * Disables the interrupts.
 */
static void disableInterrupts()
{
    /* turn the interrupts OFF iff we are in kernel mode */
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("disable interrupts\n");
        USLOSS_Halt(1);
    } else
        /* We ARE in kernel mode */
        USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
} /* disableInterrupts */

/*
 * Checks if we are in Kernel mode
 */
void check_kernel_mode(char *name) {
    if ((USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0) {
        USLOSS_Console("%s(): Called while in user mode by process %d. Halting...\n", name, getpid());
        USLOSS_Halt(1);
    }
}
