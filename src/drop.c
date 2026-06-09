// dropping packet module
#include <stdlib.h>
#include <Windows.h>
#include "iup.h"
#include "common.h"
#define NAME "drop"

static Ihandle *inboundCheckbox, *outboundCheckbox, *chanceInput;

static volatile short dropEnabled = 0,
    dropInbound = 1, dropOutbound = 1,
    chance = 1000; // [0-10000]

// ---- sliding-window byte-rate limiter -------------------------------------
// Configured via the environment at start up:
//   CLUMSY_RL_WINDOW    : W (>0) window size
//   CLUMSY_RL_MAX_BYTES : B (>0) max admitted bytes allowed within a window
static int rlWindow = 0;
static long long rlMaxBytes = 0;
static short rlActive = 0;
static long long *rlSlots = NULL;
static int rlPos = 0;
static int rlCount = 0;
static long long rlWindowSum = 0;


static Ihandle* dropSetupUI() {
    Ihandle *dropControlsBox = IupHbox(
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Chance(%):"),
        chanceInput = IupText(NULL),
        NULL
    );

    IupSetAttribute(chanceInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(chanceInput, "VALUE", "10.0");
    IupSetCallback(chanceInput, "VALUECHANGED_CB", uiSyncChance);
    IupSetAttribute(chanceInput, SYNCED_VALUE, (char*)&chance);
    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&dropInbound);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&dropOutbound);

    // enable by default to avoid confusing
    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized) {
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(chanceInput, "VALUE", NAME"-chance");
    }

    return dropControlsBox;
}

static void dropResetRL() {
    rlWindow = 0;
    rlMaxBytes = 0;
    rlActive = 0;
    free(rlSlots);
    rlSlots = NULL;
    rlPos = 0;
    rlCount = 0;
    rlWindowSum = 0;
}

static void dropStartUp() {
    const char *w = getenv("CLUMSY_RL_WINDOW");
    const char *b = getenv("CLUMSY_RL_MAX_BYTES");
    dropResetRL();
    if (w && b) {
        int win = atoi(w);
        long long maxb = atoll(b);
        if (win > 0 && maxb > 0) {
            rlSlots = (long long*)calloc((size_t)win, sizeof(long long));
            if (rlSlots) {
                rlWindow = win;
                rlMaxBytes = maxb;
                rlActive = 1;
            }
        }
    }
    LOG("drop enabled");
}

static void dropCloseDown(PacketNode *head, PacketNode *tail) {
    UNREFERENCED_PARAMETER(head);
    UNREFERENCED_PARAMETER(tail);
    dropResetRL();
    LOG("drop disabled");
}

static short dropProcess(PacketNode *head, PacketNode* tail) {
    int dropped = 0;
    while (head->next != tail) {
        PacketNode *pac = head->next;
        // chance in range of [0, 10000]
        if (checkDirection(pac->addr.Outbound, dropInbound, dropOutbound)
            && calcChance(chance)) {
            LOG("dropped with chance %.1f%%, direction %s",
                chance/100.0, pac->addr.Outbound ? "OUTBOUND" : "INBOUND");
            freeNode(popNode(pac));
            ++dropped;
        } else if (rlActive) {
            // the existing logic would forward pac; the rate limiter decides.
            long long contrib = (long long)pac->packetLen;
            long long evicted = (rlCount == rlWindow) ? rlSlots[(rlPos + 1) % rlWindow] : 0;
            long long windowBase = rlWindowSum - evicted;
            short admit = (windowBase + contrib <= rlMaxBytes);
            if (admit) {
                rlSlots[rlPos] = contrib;
                rlWindowSum = windowBase + contrib;
                rlPos = (rlPos + 1) % rlWindow;
                if (rlCount < rlWindow) ++rlCount;
                head = head->next;
            } else {
                freeNode(popNode(pac));
                ++dropped;
            }
        } else {
            head = head->next;
        }
    }

    return dropped > 0;
}


Module dropModule = {
    "Drop",
    NAME,
    (short*)&dropEnabled,
    dropSetupUI,
    dropStartUp,
    dropCloseDown,
    dropProcess,
    // runtime fields
    0, 0, NULL
};
