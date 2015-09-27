
#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mboxProc *mboxProcPtr;

struct mailbox {
    int       mboxID;
    int       numSlots;
    int       numFullSlots;
    int       slotSize;
    slotPtr   headPtr;
    int       blockStatus;
    // other items as needed...
};

struct mailSlot {
    int       mboxID;
    int       status;
    slotPtr   nextSlot;
    char      *message;

    // other items as needed...
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
    struct psrBits bits;
    unsigned int integerPart;
};

// for mailbox.blockStatus
#define NOT_BLOCKED 0
#define RECEIVE_BLOCKED 1
#define SEND_BLOCKED 2
